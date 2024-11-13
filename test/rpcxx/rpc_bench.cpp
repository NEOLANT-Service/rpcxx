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

#include <benchmark/benchmark.h>
#include <functional>
#include <rpcxx/rpcxx.hpp>
#include <thread>
#include "test_methods.hpp"

using namespace rpcxx;

enum format {
    direct,
    msgpack,
    json
};

struct MsgPackTr : IAsyncTransport {
    MsgPackTr(Protocol proto, IHandler* h) : IAsyncTransport(proto, h) {}
    void Send(JsonView msg) override {
        membuff::StringOut out;
        DumpMsgPackInto(out, msg);
        DefaultArena alloc;
        auto serial = out.Consume();
        auto back = ParseMsgPackInPlace(serial, alloc);
        Receive(back);
    }
};

struct JsonTr : IAsyncTransport {
    JsonTr(Protocol proto, IHandler* h) : IAsyncTransport(proto, h) {}
    void Send(JsonView msg) override {
        membuff::StringOut out;
        DumpJsonInto(out, msg);
        DefaultArena alloc;
        auto serial = out.Consume();
        auto back = ParseJsonInPlace(serial.data(), serial.size(), alloc);
        Receive(back);
    }
};

template<typename...Args>
static void Notify(benchmark::State& state, std::string_view method, Args&&...args) {
    auto proto = Protocol(state.range(0));
    auto serv = TestServer();
    auto cli = Client([&]() -> IClientTransport* {
        switch (format(state.range(1))) {
        case direct: return new ForwardToHandler(&serv);
        case msgpack: return new MsgPackTr(proto, &serv);
        case json: return new JsonTr(proto, &serv);
        }
        return nullptr;
    }());
    for (auto _ : state) {
        try {
            cli.Notify(method, args...);
        }
        catch (...) {}
    }
}

template<typename Ret, typename...Args>
static void RunMethod(benchmark::State& state, Ret&&, string_view method, Args&&...args) {
    auto proto = Protocol(state.range(0));
    auto serv = TestServer();
    auto cli = Client([&]() -> IClientTransport* {
        switch (format(state.range(1))) {
        case direct: return new ForwardToHandler(&serv);
        case msgpack: return new MsgPackTr(proto, &serv);
        case json: return new JsonTr(proto, &serv);
        }
        return nullptr;
    }());
    for (auto _ : state) {
        try {
            (void)cli.Request<Ret>(Method{method, NoTimeout}, args...);
        }
        catch (...) {}
    }
}

struct Big {
    int lol[10] = {};
};

static void MoveFuncConstruction(benchmark::State& state) {
    for (auto _: state) {
        MoveFunc<int()> f{[]{
            return 1;
        }};
        benchmark::DoNotOptimize(f);
    }
}
static void MoveFuncCall(benchmark::State& state) {
    MoveFunc<int()> f{[]{
        return 1;
    }};
    for (auto _: state) {
        benchmark::DoNotOptimize(f());
    }
}

BENCHMARK(MoveFuncConstruction);
BENCHMARK(MoveFuncCall);

static void StdFuncConstruction(benchmark::State& state) {
    for (auto _: state) {
        std::function<int()> f{[]{
            return 1;
        }};
        benchmark::DoNotOptimize(f);
    }

}
static void StdFuncCall(benchmark::State& state) {
    std::function<int()> f{[]{
        return 1;
    }};
    for (auto _: state) {
        benchmark::DoNotOptimize(f());
    }

}

BENCHMARK(StdFuncConstruction);
BENCHMARK(StdFuncCall);

static void prepareBench(benchmark::internal::Benchmark* bench)
{
    bench
        ->ArgNames({"minified", "transport"})
        ->ArgPair(int(Protocol::json_v2_compliant), int(direct))
        ->ArgPair(int(Protocol::json_v2_compliant), int(msgpack))
        ->ArgPair(int(Protocol::json_v2_compliant), int(json))
        ->ArgPair(int(Protocol::json_v2_minified), int(direct))
        ->ArgPair(int(Protocol::json_v2_minified), int(msgpack))
        ->ArgPair(int(Protocol::json_v2_minified), int(json));
}

BENCHMARK_CAPTURE(Notify,
                  positional,
                  "notification_silent",
                  1, 2
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(Notify,
                  named,
                  "notification_silent_named",
                  Arg("a", 12), Arg("b", 13)
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(Notify,
                  missing_param,
                  "notification_silent",
                  1
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(Notify,
                  named_type_missmatch,
                  "notification_silent_named",
                  Arg("a", 12), Arg("b", "a")
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
    positional, int{},
    "calc",
    1, 2
    )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
                  named, int{},
                  "calc_named",
                  Arg("a", 1), Arg("b", 2)
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
                  missing_param, int{},
                  "calc",
                  1
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
                  missing_param, int{},
                  "calc_template",
                  1
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
                  user_error, string{},
                  "throws_error",
                  "123"
                  )->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod,
                  named_type_missmatch, int{},
                  "calc_named",
                  Arg("a", 1), Arg("b", "2")
                  )->Apply(prepareBench);

static Small part {
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
    "asdasdsadasdads_12312312312312312312312",
};
static RpcBig big {
    {part, part, part, part, part, part, part, part,},
    {123123123, 1231231, 1231231, 1231231, 1231231, 1231231, 1231231}
};

BENCHMARK_CAPTURE(RunMethod, big, RpcBig{}, "big", big)->Apply(prepareBench);
BENCHMARK_CAPTURE(RunMethod, big_named, RpcBig{}, "big_named", Arg("body", big))->Apply(prepareBench);

BENCHMARK_MAIN();
