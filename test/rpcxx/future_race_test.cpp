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

// Cross-thread races between Promise resolution and continuation attachment.
// Build with -DRPCXX_TEST_TSAN=ON to let TSAN verify the synchronization.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "future/future.hpp"
#include "future/multi_future.hpp"
#include "future/cancel_token.hpp"
#include "future/to_std_fut.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace fut;
using namespace std::chrono_literals;

namespace {

constexpr int kIters = 3000;

// Spins both threads until each is ready, to maximize collision probability
struct StartGate {
    std::atomic<int> ready{0};
    void arrive_and_wait(int total) {
        ready.fetch_add(1);
        while (ready.load(std::memory_order_acquire) < total) {}
    }
};

struct PoolExecutor final : Executor {
    explicit PoolExecutor(unsigned n) {
        for (auto i = 0u; i < n; ++i) {
            workers.emplace_back([this]{ run(); });
        }
    }
    Status Execute(Job job) noexcept override {
        {
            std::lock_guard lock(mut);
            if (stopped) return Cancel;
            jobs.push_back(std::move(job));
        }
        cv.notify_one();
        return Defer;
    }
    void Stop() {
        {
            std::lock_guard lock(mut);
            stopped = true;
        }
        cv.notify_all();
        for (auto& w: workers) w.join();
        workers.clear();
    }
    ~PoolExecutor() override {
        if (!workers.empty()) Stop();
    }
private:
    void run() {
        std::unique_lock lock(mut);
        while (true) {
            cv.wait(lock, [&]{ return stopped || !jobs.empty(); });
            if (jobs.empty()) return;
            auto job = std::move(jobs.front());
            jobs.pop_front();
            lock.unlock();
            job();
            lock.lock();
        }
    }
    std::mutex mut;
    std::condition_variable cv;
    std::deque<Job> jobs;
    bool stopped = false;
    std::vector<std::thread> workers;
};

template<typename T>
T waitFor(Future<T> fut, std::chrono::seconds timeout = 20s) {
    auto std_fut = ToStdFuture(std::move(fut));
    REQUIRE(std_fut.wait_for(timeout) == std::future_status::ready);
    return std_fut.get();
}

} // anonymous

TEST_CASE("race: resolve value vs attach continuation") {
    for (int i = 0; i < kIters; ++i) {
        Promise<std::string> prom;
        auto fut = prom.GetFuture();
        StartGate gate;
        std::string out;
        std::thread resolver([&, prom = std::move(prom)]() mutable {
            gate.arrive_and_wait(2);
            prom(std::string("payload long enough to require a heap allocation"));
        });
        std::thread attacher([&]{
            gate.arrive_and_wait(2);
            fut.AtLastSync([&](Result<std::string> r){
                out = r.get();
            });
        });
        resolver.join();
        attacher.join();
        CHECK(out == "payload long enough to require a heap allocation");
    }
}

TEST_CASE("race: reject vs attach continuation") {
    for (int i = 0; i < kIters; ++i) {
        Promise<int> prom;
        auto fut = prom.GetFuture();
        StartGate gate;
        std::string what;
        std::thread resolver([&, prom = std::move(prom)]() mutable {
            gate.arrive_and_wait(2);
            prom(std::runtime_error("expected failure"));
        });
        std::thread attacher([&]{
            gate.arrive_and_wait(2);
            fut.AtLastSync([&](Result<int> r){
                try {
                    (void)r.get();
                } catch (std::exception& e) {
                    what = e.what();
                }
            });
        });
        resolver.join();
        attacher.join();
        CHECK(what == "expected failure");
    }
}

TEST_CASE("race: concurrent SharedPromise resolution") {
    constexpr int kThreads = 4;
    for (int i = 0; i < kIters / 2; ++i) {
        SharedPromise<std::string> prom;
        auto fut = prom.GetFuture();
        StartGate gate;
        std::atomic<int> wins{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t]{
                gate.arrive_and_wait(kThreads);
                bool won = (t % 2) ? prom(std::string("value ") + std::to_string(t))
                                   : prom(std::runtime_error("error"));
                wins += won;
            });
        }
        std::string out;
        fut.AtLastSync([&](Result<std::string> r){
            try {
                out = r.get();
            } catch (std::exception& e) {
                out = e.what();
            }
        });
        for (auto& t: threads) t.join();
        CHECK(wins.load() == 1);
        CHECK(!out.empty());
    }
}

TEST_CASE("race: chain through deferred executor") {
    rc::Strong pool = new PoolExecutor(3);
    for (int i = 0; i < kIters / 4; ++i) {
        Promise<int> prom;
        auto fut = prom.GetFuture();
        StartGate gate;
        std::thread resolver([&, prom = std::move(prom)]() mutable {
            gate.arrive_and_wait(2);
            prom(1);
        });
        gate.arrive_and_wait(2);
        auto chained = fut
            .Then(pool, [](int v){ return v + 1; })
            .ThenSync([](int v){ return v + 1; })
            .Then(pool, [](int v){ return std::to_string(v + 1); });
        auto res = waitFor(std::move(chained));
        resolver.join();
        CHECK(res == "4");
    }
    pool->Stop();
}

TEST_CASE("race: MultiFuture resolve vs GetFuture attach") {
    for (int i = 0; i < kIters / 2; ++i) {
        Promise<int> prom;
        MultiFuture<int> mf(prom.GetFuture());
        StartGate gate;
        std::atomic<int> resolved{0};
        std::thread resolver([&, prom = std::move(prom)]() mutable {
            gate.arrive_and_wait(2);
            prom(42);
        });
        std::thread consumer([&]{
            gate.arrive_and_wait(2);
            for (int j = 0; j < 20; ++j) {
                mf.GetFuture().AtLastSync([&](Result<int> r){
                    try {
                        if (r.get() == 42) resolved++;
                    } catch (...) {}
                });
            }
        });
        resolver.join();
        consumer.join();
        // late subscriber must still receive the published value
        int late = 0;
        mf.GetFuture().AtLastSync([&](Result<int> r){ late = r.get(); });
        CHECK(late == 42);
        CHECK(resolved.load() == 20);
    }
}

TEST_CASE("MultiFuture: reentrant GetFuture from continuation") {
    Promise<int> prom;
    MultiFuture<int> mf(prom.GetFuture());
    int count = 0;
    mf.GetFuture().AtLastSync([&](Result<int> r){
        if (r) count++;
        // re-enter while the first batch of waiters is being resolved
        mf.GetFuture().AtLastSync([&](Result<int> r2){ if (r2) count++; });
    });
    mf.GetFuture().AtLastSync([&](Result<int> r){ if (r) count++; });
    prom(7);
    CHECK(count == 3);
}

TEST_CASE("race: CancelController cancel vs OnCancel subscribe") {
    for (int i = 0; i < kIters / 4; ++i) {
        CancelController ctrl;
        auto sig = ctrl.Signal();
        StartGate gate;
        std::atomic<int> fired{0};
        std::thread canceller([&, ctrl = std::move(ctrl)]() mutable {
            gate.arrive_and_wait(2);
            ctrl("stop");
        });
        std::thread subscriber([&]{
            gate.arrive_and_wait(2);
            for (int j = 0; j < 10; ++j) {
                sig.OnCancel([&]{ fired++; });
            }
        });
        canceller.join();
        subscriber.join();
        // subscriptions after cancellation still fire (MultiFuture replays)
        for (int j = 0; j < 5; ++j) {
            sig.OnCancel([&]{ fired++; });
        }
        CHECK(fired.load() == 15);
    }
}

TEST_CASE("race: future-returning continuation across threads") {
    rc::Strong pool = new PoolExecutor(3);
    for (int i = 0; i < kIters / 4; ++i) {
        Promise<void> outer;
        Promise<int> inner;
        auto fut = outer.GetFuture();
        auto innerFut = inner.GetFuture();
        StartGate gate;
        std::thread t1([&, outer = std::move(outer)]() mutable {
            gate.arrive_and_wait(3);
            outer();
        });
        std::thread t2([&, inner = std::move(inner)]() mutable {
            gate.arrive_and_wait(3);
            inner(41);
        });
        gate.arrive_and_wait(3);
        auto chained = fut
            .Then(pool, [f = std::make_shared<Future<int>>(std::move(innerFut))]() mutable {
                return std::move(*f);
            })
            .ThenSync([](int v){ return v + 1; });
        CHECK(waitFor(std::move(chained)) == 42);
        t1.join();
        t2.join();
    }
    pool->Stop();
}
