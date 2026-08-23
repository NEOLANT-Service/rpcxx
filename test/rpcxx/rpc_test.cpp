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

struct RecordingTransport : IAsyncTransport {
    using IAsyncTransport::IAsyncTransport;
    std::vector<std::string> sent;
    void Send(JsonView msg) override {
        sent.push_back(jv::DumpJson(msg));
    }
};

// "id": null is an invalid Request object, not a notification: JSON-RPC 2.0
// answers it with an Invalid Request error (id null). Only a request with NO
// 'id' member is a notification.
TEST_CASE("rpc: null id is an invalid request, not a notification") {
    rc::Strong<TestServer> server = rc::MakeStrong<TestServer>();
    bool notified = false;
    server->Notify("ping_notif", [&]{ notified = true; });
    rc::Strong<RecordingTransport> tr = rc::MakeStrong<RecordingTransport>(
        Protocol::json_v2_compliant, server);

    jv::DefaultArena arena;
    tr->Receive(jv::ParseJson(R"({"jsonrpc":"2.0","method":"ping_notif","id":null})", arena));
    CHECK(!notified);
    REQUIRE(tr->sent.size() == 1);
    jv::DefaultArena arena2;
    auto resp = jv::ParseJson(tr->sent.back(), arena2);
    CHECK(resp.At("id").Is(t_null));
    CHECK(resp.At("error").At("code").Get<int>() == int(ErrorCode::invalid_request));

    // No 'id' member: a notification — handler called, no response sent.
    tr->Receive(jv::ParseJson(R"({"jsonrpc":"2.0","method":"ping_notif"})", arena));
    CHECK(notified);
    CHECK(tr->sent.size() == 1);
}

// Route cycles (a -> ... -> a through other handlers) are rejected at
// SetRoute time; a direct self-route stays allowed.
TEST_CASE("rpc: SetRoute rejects route cycles") {
    auto a = rc::MakeStrong<Server>();
    auto b = rc::MakeStrong<Server>();
    auto c = rc::MakeStrong<Server>();

    a->SetRoute("b", b);
    CHECK_THROWS(b->SetRoute("a", a));        // 2-cycle: b.a -> a -> b
    b->SetRoute("c", c);
    CHECK_THROWS(c->SetRoute("a", a));        // 3-cycle: c.a -> a -> b -> c

    // Direct self-route is a documented feature and must keep working.
    CHECK_NOTHROW(a->SetRoute("self", a));
    // Routes to expired/unset handlers remove the route as before.
    CHECK_NOTHROW(a->SetRoute("b", nullptr));
    // Non-cyclic chaining is fine.
    CHECK_NOTHROW(b->SetRoute("a", a));       // a no longer routes to b
}

// Async completions used to capture the Server by raw pointer: rejecting an
// async method after the server was destroyed was a use-after-free in the
// exception-handler path (Server::Wrap and Server::OnForward).
TEST_CASE("rpc: async completion after server destruction") {
    fut::SharedPromise<std::string> pending;
    // An executor that outlives the server, so the async continuation still
    // runs after the server is gone (the default server executor is stopped
    // in ~Server and would just drop the job).
    struct ExtExecServer : TestServer {
        rc::Strong<fut::Executor> ext;
        ExtExecServer(rc::MakeStrongRef key, rc::Strong<fut::Executor> e)
            : TestServer(key), ext(std::move(e)) {}
    protected:
        fut::Executor* GetExecutor() const noexcept override { return ext.get(); }
    };
    auto extExec = rc::MakeStrong<fut::StoppableExecutor>();
    auto router = rc::MakeStrong<Server>();
    rc::Strong<MockTransport> tr = rc::MakeStrong<MockTransport>(
        Protocol::json_v2_compliant, router);
    tr->fmt = json;
    Future<std::string> f;
    {
        rc::Strong<TestServer> server = rc::MakeStrong<ExtExecServer>(extExec);
        std::reference_wrapper<fut::SharedPromise<std::string>> ref(pending);
        server->Method("deferred", [ref]() -> Future<std::string> {
            return ref.get().GetFuture();
        });
        router->SetRoute("r", server);
        Client cli;
        cli.SetTransport(tr);
        // Issue the request while the method server is alive, so the method
        // actually runs and suspends on the pending promise.
        f = cli.Request<std::string>(Method{"r/deferred", NoTimeout});
    } // the method server is destroyed with the request still in flight

    // Wrap runs with a dead server: must not touch it, and must not leak the
    // raw exception text to the client — exception handlers exist to hide
    // such details, so a sanitized generic error is sent instead.
    pending(std::runtime_error("late failure"));
    try {
        ToStdFuture(std::move(f)).get();
        FAIL("expected the late rejection to surface as an error");
    } catch (RpcException& e) {
        CHECK(e.message == "Server dead");
        CHECK(e.code == ErrorCode::internal);
    }
}

// rc::foreign_owned opts a weakable object out of Strong ownership (Qt
// parent/child style): created with plain `new`, deleted by its owner, and
// rc::Weak expires it via the destructor. Single-threaded by contract.
TEST_CASE("rc: foreign-owned object is weak-referenceable without Strong") {
    struct ForeignServer : Server {
        ForeignServer() : Server(rc::foreign_owned) {}
        ~ForeignServer() override = default; // public: the foreign owner deletes
    };
    rc::Weak<IHandler> weak;
    auto* server = new ForeignServer();
    weak = server;
    {
        rc::Strong<IHandler> locked = weak.lock();
        CHECK(locked.get() != nullptr); // locks although no Strong owns the object
    } // Unref drops the refcount to zero — must NOT delete
    CHECK(weak.lock().get() != nullptr); // still alive, still lockable
    delete server; // the "Qt parent" deletes it
    CHECK(weak.lock().get() == nullptr); // expired via the destructor
}
