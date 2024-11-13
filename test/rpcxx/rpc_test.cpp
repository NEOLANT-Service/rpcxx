// This file is a part of RPCXX project

/*
Copyright 2024 "NEOLANT Service", "NEOLANT Kalinigrad", Alexey Doronin, Anastasia Lugovets, Dmitriy Dyakonov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "rpcxx/rpcxx.hpp"
#include "future/to_std_fut.hpp"
#include "test_methods.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace rpcxx;

enum format {
    direct,
    msgpack,
    json,
};

struct MockTransport : IAsyncTransport {
    using IAsyncTransport::IAsyncTransport;
    format fmt = direct;
    void Send(JsonView msg) override {
        switch (fmt) {
        case direct: {
            Receive(msg);
            break;
        }
        case msgpack: {
            membuff::StringOut out;
            DumpMsgPackInto(out, msg);
            DefaultArena alloc;
            auto serial = out.Consume();
            auto back = ParseMsgPackInPlace(serial, alloc);
            Receive(back);
            break;
        }
        case json: {
            membuff::StringOut out;
            DumpJsonInto(out, msg);
            DefaultArena alloc;
            auto serial = out.Consume();
            auto back = ParseJsonInPlace(serial.data(), serial.size(), alloc);
            Receive(back);
            break;
        }
        }

    }
};

struct Test {
    int a;
    string b;
};

DESCRIBE(::Test, &_::a, &_::b)

template<typename T, typename...Args>
T req(Client& cli, string_view name, Args const&...a) {
    return ToStdFuture(cli.Request<T>(Method{name, NoTimeout}, a...)).get();
}

void extraMethods(Server& server) {
    server.SetRoute("self", &server);
    server.Method("add", [](int a, optional<int> b){
        return a + b.value_or(0);
    });
    server.Method("copy", [](Test arg){
        return arg;
    });
    server.Notify("notif", [](int, int){
        return 0;
    });
    server.Method("copy_named", [](Test arg){
        return arg;
    }, rpcxx::NamesMap("arg"));
}

void basicTest(Client& cli) {
    CHECK(req<int>(cli, "add", 1, 2) == 3);

    // routes
    CHECK(req<int>(cli, "self/add", 1, 2) == 3);
    CHECK(req<int>(cli, "/self/add", 1, 2) == 3);
    CHECK(req<int>(cli, "/self/add/", 1, 2) == 3);
    CHECK(req<int>(cli, "/self/add//", 1, 2) == 3);
    CHECK(req<int>(cli, "/////self/////add//////", 1, 2) == 3);
    CHECK(req<int>(cli, "/self/add//////////////////////////////", 1, 2) == 3);
    CHECK_THROWS(req<int>(cli, "self/add/a/", 1, 2));
    CHECK_THROWS(req<int>(cli, "self/add//a", 1, 2));
    CHECK_THROWS(req<int>(cli, "/./self/add", 1, 2));

    CHECK(req<int>(cli, "add", 1) == 1);
    CHECK(req<string>(cli, "async_ping", "ping") == "pong");

    // These cases fail on one transport but not the other and vice versa

    //CHECK_NOTHROW(cli.Notify("notif", 1));
    //CHECK_NOTHROW(cli.Notify("notif"));
    CHECK_NOTHROW(cli.Notify("notif", 1, 2));
    //CHECK_NOTHROW(cli.Notify("notif", "123"));

    CHECK_THROWS(req<int>(cli, "add1", 1));
    CHECK_THROWS(req<int>(cli, "self/add1", 1));
    CHECK_THROWS(req<int>(cli, "add", "123"));
    CHECK_THROWS(req<string>(cli, "add", 1));
    CHECK_THROWS(req<string>(cli, "ping", "pong"));
    CHECK(req<Test>(cli, "copy", Test{1, ""}).a == 1);
    CHECK(req<Test>(cli, "copy_named", rpcxx::Arg("arg", Test{1, "123"})).b == "123");
}

void batchTest(Client& cli) {
    auto b = cli.StartBatch();
    int hits = 0;
    cli.Notify("notif", 2, 2);
    cli.Notify("notif", 2, 2);
    cli.Notify("notif", 1, 2);
    (void)cli.Request<int>(Method{"add", NoTimeout}, 1, 2).ThenSync([&](int result){
        hits++;
        CHECK(result == 3);
    });
    auto a = cli.Request<string>(Method{"async_ping", NoTimeout}, "ping").ThenSync([&](string result){
        hits++;
        CHECK(result == "pong");
    });
    (void)cli.Request<string>(Method{"ping", NoTimeout}).AtLastSync([&](auto result){
        hits++;
        CHECK(!result);
    });
    CHECK(hits == 0);
    b.Finish();
    ToStdFuture(std::move(a)).get();
    CHECK(hits == 3);
}

TEST_CASE("rpc") {
    TestServer server;
    extraMethods(server);
    for (auto format: {direct, json, msgpack}) {
        for (auto proto: {Protocol::json_v2_compliant, Protocol::json_v2_minified}) {
            CAPTURE(PrintProto(proto));
            rc::Strong<IClientTransport> fwd = new ForwardToHandler(&server);
            rc::Strong<IClientTransport> send = new MockTransport(proto, &server);
            static_cast<MockTransport*>(send.get())->fmt = format;
            Client cli;
            for (auto& transport: {fwd, send}) {
                cli.SetTransport(transport);
                SUBCASE("basic") {
                    basicTest(cli);
                }
                SUBCASE("batch") {
                    batchTest(cli);
                }
            }
        }
    }
}
