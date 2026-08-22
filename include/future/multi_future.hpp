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

#ifndef FUT_MULTI_FUT_HPP
#define FUT_MULTI_FUT_HPP

#include "future.hpp"
#include <mutex>
#include <vector>

namespace fut {

// All fields except `mut` are guarded by `mut`. Once `done` is set to true
// (under the lock), `exc`/`buff` are never modified again, so after observing
// done==true under the lock they may safely be read without it.
template<typename T>
struct MultiState : rc::DefaultBase {
    std::mutex mut;
    bool done = false;
    alignas(T) char buff[sizeof(T)];
    std::exception_ptr exc;
    std::vector<SharedPromise<T>> proms;
    ~MultiState() {
        if (done && !exc) {
            std::launder(reinterpret_cast<T*>(buff))->~T();
        }
    }
};

template<>
struct MultiState<void> : rc::DefaultBase {
    std::mutex mut;
    bool done = false;
    std::exception_ptr exc;
    std::vector<SharedPromise<void>> proms;
};

template<typename T>
struct MultiFuture {
    MultiFuture() noexcept = default;
    MultiFuture(Future<T> fut) : state(new MultiState<T>) {
        fut.AtLastSync([state = state](auto res) noexcept {
            std::vector<SharedPromise<T>> waiters;
            {
                std::lock_guard lk(state->mut);
                if (!res) {
                    state->exc = std::move(res).get_exception();
                } else {
                    if constexpr (!std::is_void_v<T>) {
                        new (state->buff) T{res.get()};
                    }
                }
                state->done = true;
                waiters.swap(state->proms);
            }
            // Resolve outside the lock: continuations run inline from here and
            // may re-enter GetFuture() on this same MultiFuture.
            for (auto& p: waiters) {
                tryRes(*state, p);
            }
        });
    }
    bool IsValid() const noexcept {
        return bool(state);
    }
    template<typename Fn>
    auto Then(rc::Strong<Executor> exec, Fn&& f) {
        return GetFuture().Then(exec, std::forward<Fn>(f));
    }
    template<typename Fn>
    auto Try(rc::Strong<Executor> exec, Fn&& f) {
        return GetFuture().Try(exec, std::forward<Fn>(f));
    }
    template<typename Fn>
    auto AtLast(rc::Strong<Executor> exec, Fn&& f) {
        return GetFuture().AtLast(exec, std::forward<Fn>(f));
    }
    Future<T> GetFuture() {
        return prom().GetFuture();
    }
protected:
    Promise<T> prom() {
        if (!state) throw FutureError("Invalid Multi Future");
        Promise<T> p;
        bool ready;
        {
            std::lock_guard lk(state->mut);
            ready = state->done;
            if (!ready) {
                state->proms.push_back(SharedPromise<T>(p));
            }
        }
        // done was published under the lock, so exc/buff are stable here.
        // Resolve outside the lock (continuations may run inline).
        if (ready) {
            tryRes(*state, p);
        }
        return p;
    }
    // Precondition: state.done == true (published under state.mut).
    static void tryRes(MultiState<T>& state, SharedPromise<T>& p) {
        if (state.exc) {
            p(state.exc);
        } else {
            if constexpr (std::is_void_v<T>) {
                p();
            } else {
                auto* data = std::launder(reinterpret_cast<T*>(state.buff));
                p(*data);
            }
        }
    }
    rc::Strong<MultiState<T>> state;
};
    
}

#endif //FUT_MULTI_FUT_HPP
