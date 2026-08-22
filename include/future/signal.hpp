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

#ifndef FUT_SIGNAL_HPP
#define FUT_SIGNAL_HPP

#include "executor.hpp"
#include <mutex>
#include <memory>

namespace fut {

struct unit{};

template<typename T = unit>
struct Signal {
    Signal() {
        impl = new Impl;
    }
    bool Invoke(T value = {}) noexcept {
        // Snapshot the current callback + executor under the lock. A queued
        // (deferred) invocation must not read impl->func later: it races with
        // re-subscription swapping the MoveFunc.
        std::shared_ptr<fut::MoveFunc<void(T)>> fn;
        rc::Strong<Executor> ex;
        {
            std::lock_guard lock(mut);
            fn = impl->func;
            ex = exec;
        }
        if (!fn || !*fn) return false;
        if (ex) {
            ex->Execute([fn = std::move(fn), value = std::move(value)]() mutable {
                (*fn)(std::move(value));
            });
        } else {
            (*fn)(std::move(value));
        }
        return true;
    }

    void operator()(rc::Strong<Executor> _exec, fut::MoveFunc<void(T)> cb) {
        std::lock_guard lock(mut);
        impl->func = std::make_shared<fut::MoveFunc<void(T)>>(std::move(cb));
        std::swap(_exec, exec);
    }
    void operator()(fut::MoveFunc<void(T)> cb) {
        std::lock_guard lock(mut);
        impl->func = std::make_shared<fut::MoveFunc<void(T)>>(std::move(cb));
        exec = nullptr;
    }
protected:
    struct Impl : rc::DefaultBase {
        std::shared_ptr<fut::MoveFunc<void(T)>> func;
    };
    rc::Strong<Executor> exec;
    rc::Strong<Impl> impl;
    std::recursive_mutex mut;
};

}

#endif //FUT_SIGNAL_HPP
