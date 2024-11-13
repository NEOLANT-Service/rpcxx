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

#ifndef FUT_CANCEL_HPP
#define FUT_CANCEL_HPP

#include "multi_future.hpp"

namespace fut
{

struct Cancel {
    std::string reason;
};

struct CancelSignal {
    CancelSignal(MultiFuture<Cancel> sig = {}) : sig(std::move(sig)) {}
    bool IsValid() const noexcept {
        return sig.IsValid();
    }
    template<typename Fn>
    void OnCancel(Fn f) {
        OnCancel(nullptr, std::move(f));
    }
    template<typename Fn>
    void OnCancel(rc::Strong<Executor> exec, Fn f) {
        sig.AtLast(exec, [MV(f)](auto res) mutable {
            try {
                [[maybe_unused]] auto c = res.get();
                if constexpr (std::is_invocable_v<Fn>) {
                    f();
                } else {
                    f(std::move(c));
                }
            } catch (...) {}
        });
    }
private:
    MultiFuture<Cancel> sig;
};

struct CancelController {
    CancelController() {
        fut = prom.GetFuture();
    }
    void operator()(std::string reason) {
        if (prom.IsValid()) {
            prom(Cancel{std::move(reason)});
        }
    }
    CancelSignal Signal() {
        return fut;
    }
private:
    Promise<Cancel> prom;
    fut::MultiFuture<Cancel> fut;
};

}

#endif //FUT_CANCEL_HPP
