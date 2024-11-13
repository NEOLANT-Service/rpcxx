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

#ifndef FUT_EXECUTOR_HPP
#define FUT_EXECUTOR_HPP

#include "move_func.hpp"
#include "rc/rc.hpp"

namespace fut {
    
struct Executor : rc::SingleVirtualBase {
    using Job = MoveFunc<void()>;
    enum Status : int32_t {
        Defer, // (maybe) will be called from another thread
        Cancel, // will not be called
        Done, // called
    };
    virtual Status Execute(Job job) noexcept = 0;
    virtual ~Executor() = default;
};

struct StoppableExecutor final : fut::Executor {
    StoppableExecutor() noexcept = default;
    void Stop() noexcept {
        dead = true;
    }
    Status Execute(Job job) noexcept override {
        if (dead.load(std::memory_order_acquire)) {
            return Cancel;
        }
        job();
        return Done;
    }
protected:
    std::atomic_bool dead = false;
};


} //fut

#endif //FUT_EXECUTOR_HPP
