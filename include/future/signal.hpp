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
        std::lock_guard lock(mut);
        if (!impl->func) return false;
        if (exec) {
            exec->Execute([impl = impl, value = std::move(value)]() mutable {
                impl->func(std::move(value));
            });
        } else {
            impl->func(std::move(value));
        }
        return true;
    }

    void operator()(rc::Strong<Executor> _exec, fut::MoveFunc<void(T)> cb) {
        std::lock_guard lock(mut);
        std::swap(cb, impl->func);
        std::swap(_exec, this->exec);
    }
    void operator()(fut::MoveFunc<void(T)> cb) {
        std::lock_guard lock(mut);
        std::swap(cb, impl->func);
        this->exec = nullptr;
    }
protected:
    struct Impl : rc::DefaultBase {
        fut::MoveFunc<void(T)> func;
    };
    rc::Strong<Executor> exec;
    rc::Strong<Impl> impl;
    std::recursive_mutex mut;
};

}

#endif //FUT_SIGNAL_HPP
