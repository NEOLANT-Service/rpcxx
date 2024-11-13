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
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "future/to_std_fut.hpp"
#include <thread>

using namespace rpcxx;

struct TestBig {
    size_t vals[10] = {};
};

inline auto Ignore() {
    return [](auto&&...) {};
}

using namespace std::chrono_literals;

auto in = [](auto time) {
    SharedPromise<void> prom;
    std::thread([=]{
        std::this_thread::sleep_for(time);
        prom();
    }).detach();
    return prom.GetFuture();
};

struct TestExecutor : Executor {
    Status Execute(Job job) noexcept override {
        in(0.1s).AtLastSync([MV(job)](auto){
            job();
        });
        return Defer;
    }
};

TEST_CASE("rc") {
    rc::Strong p = new fut::Data<int>;
    rc::Strong<fut::Base> p2 = new fut::Data<int>;
    auto p3 = std::move(p);
    rc::Strong e = new StoppableExecutor;
    for (auto mode: {true, false}) {
        SharedPromise<void> prom;
        bool hit = false;
        if (!mode) {
            e->Stop();
        }
        (void)prom.GetFuture().Then(e, [&]{
            hit = true;
        });
        prom();
        CHECK(hit == mode);
    }
}

TEST_CASE("memory") {
    SharedPromise<int> prom;
    auto fut = prom.GetFuture();
}

TEST_CASE("thread safety") {
    int counter = 0;
    auto fut = GatherTuple(in(0.5s), in(0.25s), in(0.15s))
        .ThenSync([&]{
            counter++;
        })
        .Then(new TestExecutor, [&]{
            counter++;
        })
        .ThenSync([&]{
            counter++;
            return in(0.15s);
        })
        .ThenSync([&]{
            counter++;
        })
        .Then(new TestExecutor, [&]{
            counter++;
        });
    ToStdFuture(std::move(fut)).get();
    CHECK(counter == 5);
}

TEST_CASE("move func")
{
    auto test = [](auto functor) {
        std::vector<MoveFunc<int(int)>> fs;
        for (auto i = 0; i < 30; ++i) {
            fs.push_back(functor);
        }
        int acc = 0;
        for (auto& f: fs) {
            acc += f(1);
        }
        CHECK(acc == 60);
        std::vector<MoveFunc<int(int)>> lol;
        for (auto& f: fs) {
            lol.push_back(std::move(f));
        }
        for (auto& f: lol) {
            acc += f(1);
        }
        CHECK(acc == 120);
    };
    GIVEN("invalid") {
        CHECK_THROWS(MoveFunc<void()>()());
    }
    GIVEN("small functor") {
        auto small = [](int a) {
            return a + 1;
        };
        test(small);
    }
    GIVEN("big functor") {
        auto big = [a = TestBig{{1}}](int b) {
            return b + a.vals[0];
        };
        test(big);
    }
}

TEST_CASE("future")
{
    SUBCASE("try") {
        bool got = false;
        fut::Rejected<void>(std::runtime_error("123"))
            .TrySync([](auto exc){
                CHECK(!exc);
            })
            .AtLastSync([&](auto res){
                CHECK(res);
                got = true;
            });
        CHECK(got);
        got = false;
        fut::Rejected<void>(std::runtime_error("original"))
            .TrySync([](auto exc){
                CHECK(!exc);
                throw std::runtime_error("changed");
            })
            .AtLastSync([&](auto res){
                CHECK(!res);
                try {
                    res.get();
                } catch (std::exception& e) {
                    CHECK(string_view{e.what()} == string_view{"changed"});
                }
                got = true;
            });
        CHECK(got);
    }
    SUBCASE("basic") {
        Promise<int> prom;
        Future<int> fut = prom.GetFuture();
        prom(1);
        CHECK(fut.IsValid());
        fut.AtLastSync(Ignore());
        CHECK(!fut.IsValid());
    }
    SUBCASE("small_1") {
        Promise<int> prom;
        Future<int> fut = prom.GetFuture();
        prom(1);
        int res = 0;
        CHECK(fut.IsValid());
        fut
            .ThenSync([&](int passed){
                res = passed;
            })
            .AtLastSync(Ignore());
        CHECK(!fut.IsValid());
        CHECK_EQ(res, 1);
    }
    SUBCASE("small_2") {
        rpcxx::Promise<int> prom2;
        int res = 0;
        CHECK_EQ(res, 0);
        prom2.GetFuture()
            .ThenSync([&](int passed){
                res = passed;
            })
            .AtLastSync(Ignore());
        prom2(2);
        CHECK_EQ(res, 2);
    }
    SUBCASE("string") {
        auto fut = Future<string>::FromFunction([](auto prom){
                       prom("123");
                   }).ThenSync([](string s){
                           return s;
                    });
        auto res = ToStdFuture(std::move(fut)).get();
        CHECK_EQ(res, "123");
    }
    SUBCASE("chaining") {
        int first = 0, second = 0, third = 0;
        rpcxx::Promise<int> prom;
        prom.GetFuture()
            .ThenSync([&](int a){
                return first = a;
            })
            .ThenSync([&](int b){
                return second = b + 5;
            })
            .ThenSync([&](int c){
                return third = c + 5;
            })
            .AtLastSync(Ignore());
        prom(30);
        CHECK_EQ(first, 30);
        CHECK_EQ(second, 35);
        CHECK_EQ(third, 40);
    }
    SUBCASE("deferred") {
        size_t hits = 0;
        auto fut = fut::Resolved()
            .ThenSync([&]{
                hits++;
                return fut::Resolved();
            })
            .ThenSync([&]{
                hits++;
                return in(0.2s);
            })
            .ThenSync([&]{
                hits++;
                return fut::Resolved();
            })
            .ThenSync([&]{
                hits++;
                return in(0.2s);
            })
            .ThenSync([&]{
                hits++;
                return fut::Resolved();
            })
            .ThenSync([&]{
                CHECK_EQ(hits, 5);
            });
        fut::ToStdFuture(std::move(fut)).get();
        CHECK_EQ(hits, 5);
    }
    SUBCASE("exception") {
        try {
            auto p = Promise<void>();
            auto f = p.GetFuture();
            throw 1;
        } catch (...) {}
    }
    SUBCASE("gather") {
        SUBCASE("ok") {
            Promise<int> one;
            Promise<int> two;
            int first = 0, second = 0;
            Promise<void> three;
            GatherTuple(one.GetFuture(), two.GetFuture(), three.GetFuture())
                .ThenSync([&](std::tuple<int, int, empty> res){
                    auto [a, b, _] = res;
                    first = a;
                    second = b;
                })
                .AtLastSync(Ignore());
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            one(1);
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            two(2);
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            three();
            CHECK_EQ(first, 1);
            CHECK_EQ(second, 2);
        }
        SUBCASE("error") {
            Promise<int> one;
            Promise<int> two;
            Promise<void> three;
            int first = 0, second = 0;
            bool errCaught = false;
            GatherTuple(one.GetFuture(), two.GetFuture(), three.GetFuture())
                .AtLastSync([&](Result<std::tuple<int, int, empty>> res){
                    if (auto&& err = res.get_exception()) {
                        errCaught = true;
                        (void)err;
                    } else {
                        auto [a, b, _] = res.get();
                        first = a;
                        second = b;
                    }
                });
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            one(1);
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            two(std::runtime_error("err!"));
            CHECK_EQ(first, 0);
            CHECK_EQ(second, 0);
            CHECK(errCaught);
            three();
        }
    }
    SUBCASE("big") {
        rpcxx::Promise<TestBig> setter;
        rpcxx::Promise<TestBig> setter2;
        setter(TestBig{{1, 2, 3}});
        int res = 0;
        setter.GetFuture()
            .ThenSync([&](TestBig passed){res = passed.vals[2];})
            .AtLastSync(Ignore());
        CHECK_EQ(res, 3);
        setter2.GetFuture()
            .ThenSync([&](TestBig passed){res = passed.vals[2];})
            .AtLastSync(Ignore());
        setter2(TestBig{{3, 2, 1}});
        CHECK_EQ(res, 1);
    }
    SUBCASE("gather_vec") {
        GIVEN("voids") {
            std::vector<Future<void>> futs;
            std::vector<Promise<void>> proms;
            bool hit = false;
            GIVEN("ok") {
                for (auto i = 0; i < 10; ++i) {
                    futs.push_back(proms.emplace_back().GetFuture());
                }
                Gather(std::move(futs)).AtLastSync([&](auto res){
                    CHECK(res);
                    hit = true;
                });
                CHECK(!hit);
                for (auto& p: proms) {
                    CHECK(!hit);
                    p();
                }
                CHECK(hit);
            }
            GIVEN("err") {
                for (auto i = 0; i < 10; ++i) {
                    futs.push_back(proms.emplace_back().GetFuture());
                }
                Gather(std::move(futs)).AtLastSync([&](auto res){
                    CHECK(!res);
                    hit = true;
                });
                CHECK(!hit);
                for (auto& p: proms) {
                    p(std::bad_alloc());
                    CHECK(hit);
                }
                CHECK(hit);
            }
        }
        GIVEN("empty") {
            int hits = 0;
            Gather(std::vector<Future<void>>{}).AtLastSync([&](auto res){
                hits++;
                CHECK(res);
            });
            Gather(std::vector<Future<int>>{}).AtLastSync([&](auto res){
                hits++;
                CHECK(res);
            });
            CHECK(hits == 2);
        }
        std::vector<Future<int>> futs;
        std::vector<Promise<int>> proms;
        bool hit = false;
        auto checkOnes = [&]{
            Gather(std::move(futs)).AtLastSync([&](auto vec){
                hit = true;
                for (auto& r: vec.get()) {
                    CHECK(r == 1);
                }
            });
        };
        auto checkExc = [&]{
            Gather(std::move(futs)).AtLastSync([&](auto vec){
                hit = true;
                CHECK(vec.get_exception());
            });
        };
        GIVEN("from result") {
            for (auto i = 0; i < 100; ++i) {
                futs.push_back(fut::Resolved<int>(1));
            }
            checkOnes();
            CHECK(hit);
        }
        GIVEN("from err") {
            for (auto i = 0; i < 100; ++i) {
                futs.push_back(fut::Resolved<int>(1));
            }
            futs.push_back(fut::Rejected<int>(std::runtime_error("1")));
            checkExc();
            CHECK(hit);
        }
        GIVEN("from promise") {
            for (auto i = 0; i < 10; ++i) {
                futs.push_back(proms.emplace_back().GetFuture());
            }
            checkOnes();
            CHECK(!hit);
            for (auto& p: proms) {
                CHECK(!hit);
                p(1);
            }
            CHECK(hit);
        }
        GIVEN("from promise error") {
            for (auto i = 0; i < 10; ++i) {
                futs.push_back(proms.emplace_back().GetFuture());
            }
            checkExc();
            CHECK(!hit);
            for (auto& p: proms) {
                p(std::runtime_error(""));
                CHECK(hit);
            }
            CHECK(hit);
        }

    }
}

