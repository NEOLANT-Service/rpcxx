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

DESCRIBE("Test", Test) {
    MEMBER("a", &_::a);
    MEMBER("b", &_::b);
}

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
    rc::Strong<TestServer> server = rc::MakeStrong<TestServer>();
    extraMethods(*server);
    for (auto format: {direct, json, msgpack}) {
        for (auto proto: {Protocol::json_v2_compliant, Protocol::json_v2_minified}) {
            CAPTURE(PrintProto(proto));
            rc::Strong<IClientTransport> fwd = rc::MakeStrong<ForwardToHandler>(server);
            rc::Strong<IClientTransport> send = rc::MakeStrong<MockTransport>(proto, server);
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

// Multiple client threads share one serialized transport while async handlers
// resolve on their own threads and a timer thread pumps CheckTimeouts().
// Stresses IAsyncTransport's pending-request map; run under TSAN.
TEST_CASE("rpc: cross-thread transport stress") {
    rc::Strong<TestServer> server = rc::MakeStrong<TestServer>();
    extraMethods(*server);
    for (auto proto: {Protocol::json_v2_compliant, Protocol::json_v2_minified}) {
        CAPTURE(PrintProto(proto));
        rc::Strong<IClientTransport> send = rc::MakeStrong<MockTransport>(proto, server);
        auto* mock = static_cast<MockTransport*>(send.get());
        mock->fmt = json;
        std::atomic<bool> stop{false};
        std::atomic<int> done{0};
        std::thread timer([&]{
            while (!stop.load(std::memory_order_acquire)) {
                mock->CheckTimeouts();
                std::this_thread::yield();
            }
        });
        std::vector<std::thread> clients;
        for (int t = 0; t < 4; ++t) {
            clients.emplace_back([&]{
                Client cli; // per-thread client, shared transport
                cli.SetTransport(send);
                for (int i = 0; i < 50; ++i) {
                    CHECK(req<int>(cli, "add", 1, 2) == 3);
                    CHECK(req<string>(cli, "async_ping", "ping") == "pong");
                    done++;
                }
            });
        }
        for (auto& c: clients) c.join();
        stop.store(true);
        timer.join();
        CHECK(done == 200);
    }
}

struct FailTransport : IAsyncTransport {
    using IAsyncTransport::IAsyncTransport;
    void Send(JsonView) override {
        throw std::runtime_error("simulated send failure");
    }
};

// If Send() throws, the request never reached the wire: the pending promise
// must be rejected immediately instead of hanging until the timeout.
TEST_CASE("rpc: send failure rejects pending requests") {
    rc::Strong<TestServer> server = rc::MakeStrong<TestServer>();
    extraMethods(*server);
    rc::Strong<IClientTransport> tr = rc::MakeStrong<FailTransport>(
        Protocol::json_v2_compliant, server);
    Client cli;
    cli.SetTransport(tr);

    CHECK_THROWS(req<int>(cli, "add", 1, 2));

    int hits = 0;
    {
        auto b = cli.StartBatch();
        cli.Request<int>(Method{"add", NoTimeout}, 1, 2)
            .AtLastSync([&](Result<int> res){
                hits++;
                CHECK(!res);
            });
        cli.Notify("notif", 1, 2);
        b.Finish();
    }
    CHECK(hits == 1);
}

// A throwing notify handler must not let the exception escape Receive():
// notifications produce no response, so the error is logged and dropped.
TEST_CASE("rpc: notify handler exceptions are contained") {
    rc::Strong<TestServer> server = rc::MakeStrong<TestServer>();
    extraMethods(*server);
    server->Notify("boom", [](){ throw std::runtime_error("notify boom"); });
    rc::Strong<MockTransport> tr = rc::MakeStrong<MockTransport>(
        Protocol::json_v2_compliant, server);
    tr->fmt = json;
    Client cli;
    cli.SetTransport(tr);

    CHECK_NOTHROW(cli.Notify("boom"));

    // A batch with a throwing notify part still answers its method parts.
    int hits = 0;
    {
        auto b = cli.StartBatch();
        cli.Notify("boom");
        cli.Request<int>(Method{"add", NoTimeout}, 1, 2)
            .AtLastSync([&](Result<int> res){
                hits++;
                CHECK(res);
                CHECK(res.get() == 3);
            });
        b.Finish();
    }
    CHECK(hits == 1);
}
