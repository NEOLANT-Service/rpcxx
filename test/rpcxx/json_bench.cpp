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
#include <benchmark/benchmark.h>
#include "json_samples.hpp"

using namespace rpcxx;

static void Dump(benchmark::State& state, string_view sample)
{
    auto json = Json::Parse(sample);
    for (auto _: state) {
        benchmark::DoNotOptimize(json->Dump());
    }
}
BENCHMARK_CAPTURE(Dump, books, BooksSample);
BENCHMARK_CAPTURE(Dump, big, BigSample);
BENCHMARK_CAPTURE(Dump, rpc, RPCSample);

static void Parse(benchmark::State& state, string_view sample)
{
    for (auto _: state) {
        DefaultArena alloc;
        try {
            benchmark::DoNotOptimize(ParseJson(sample, alloc));
        } catch (...) {}
    }
}

static void ParseInSitu(benchmark::State& state, std::string_view sample)
{
    std::string orig{sample};
    std::string curr = orig;
    for (auto _: state) {
        std::string curr = orig;
        DefaultArena alloc;
        try {
            benchmark::DoNotOptimize(ParseJsonInPlace(curr.data(), curr.size(), alloc));
        } catch (...) {}
    }
}

BENCHMARK_CAPTURE(Parse, books, BooksSample);
BENCHMARK_CAPTURE(Parse, big, BigSample);
BENCHMARK_CAPTURE(Parse, rpc, RPCSample);
BENCHMARK_CAPTURE(Parse, rpc_mini, MinifiedRPCSample);
BENCHMARK_CAPTURE(Parse, early_fail, EarlyFailSample);
BENCHMARK_CAPTURE(Parse, late_fail, LateFailSample);

BENCHMARK_CAPTURE(ParseInSitu, books, BooksSample);
BENCHMARK_CAPTURE(ParseInSitu, big, BigSample);
BENCHMARK_CAPTURE(ParseInSitu, rpc, RPCSample);
BENCHMARK_CAPTURE(ParseInSitu, rpc_mini, MinifiedRPCSample);
BENCHMARK_CAPTURE(ParseInSitu, early_fail, EarlyFailSample);
BENCHMARK_CAPTURE(ParseInSitu, late_fail, LateFailSample);

static void Parse_MsgPack(benchmark::State& state, const void* data, size_t len) {
    for (auto _: state) {
        DefaultArena alloc;
        benchmark::DoNotOptimize(ParseMsgPackInPlace(data, len, alloc));
    }
}
BENCHMARK_CAPTURE(Parse_MsgPack, rpc, MsgPackRPC, sizeof(MsgPackRPC));
BENCHMARK_CAPTURE(Parse_MsgPack, books, MsgPackBooks, sizeof(MsgPackBooks));

struct TestChild
{
    int a;
    bool b;
    std::string kek;
};
DESCRIBE(TestChild, &_::a, &_::b, &_::kek)

struct TestData
{
    int a;
    bool b;
    std::string kek;
    std::map<std::string, TestChild> children;
};
DESCRIBE(TestData, &_::a, &_::b, &_::kek, &_::children);

static TestChild testDataPart {
    1, true, "lol"
};

static TestData testData {
    2, false, "asdasdasdadasda",
    {
     {"a", testDataPart},
     {"c", testDataPart},
    }
};


static void Serialize(benchmark::State& state) {
    DefaultArena alloc;
    for (auto _: state) {
        benchmark::DoNotOptimize(JsonView::From(testData, alloc));
    }
}
BENCHMARK(Serialize);

static void DeSerialize(benchmark::State& state) {
    DefaultArena alloc;
    JsonView json = JsonView::From(testData, alloc);
    for (auto _: state) {
        benchmark::DoNotOptimize(json.Get<TestData>());
    }
}
BENCHMARK(DeSerialize);

BENCHMARK_MAIN();
