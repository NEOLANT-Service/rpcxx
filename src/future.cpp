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

#include "future/future.hpp"

#include <cstdio>

fut::Base::~Base() {
    if (notify) {
        notify(this, false);
    }
    while (chain && chain->_refs.load(std::memory_order_acquire) == 1) {
        auto next = std::move(chain->chain);
        chain = {};
        chain = std::move(next);
    }
}

namespace {
// Carries a claimed continuation onto an Executor. If the executor drops the
// job (Cancel) instead of running it, the destructor still cleans up the
// type-erased functor via notif(self, false).
struct NotifyCtx {
    fut::Base::Notify notif;
    rc::Strong<fut::Base> data;
    rc::Strong<fut::Base> chain;

    NotifyCtx(fut::Base::Notify notif,
              rc::Strong<fut::Base> data,
              rc::Strong<fut::Base> chain) noexcept :
        notif(notif), data(std::move(data)), chain(std::move(chain))
    {}
    NotifyCtx(const NotifyCtx&) = delete;
    NotifyCtx(NotifyCtx&& o) noexcept :
        notif(std::exchange(o.notif, nullptr)),
        data(std::move(o.data)),
        chain(std::move(o.chain))
    {}

    ~NotifyCtx() {
        if (!notif) return;
        // The job never ran (executor returned Cancel). Clean up the
        // type-erased functor and reject the chain, otherwise futures
        // downstream of the cancelled continuation hang forever.
        notif(data.get(), false);
        if (chain) {
            fut::d::fulfilExc(chain.get(), std::make_exception_ptr(
                fut::FutureError("continuation cancelled by executor")));
            fut::d::continueChain(std::move(chain));
        }
    }
};
}

void fut::d::continueChain(rc::Strong<Base> data) noexcept
{
    while (data) {
        Base::Notify notif = nullptr;
        rc::Strong<Executor> exec;
        rc::Strong<Base> chain;
        {
            std::lock_guard<std::mutex> lk(data->mtx);
            if (!(data->flags & Base::fullfilled)) {
                return; // not resolved yet: the producer will drive us later
            }
            notif = data->notify;
            if (!notif) {
                return; // no continuation, or another thread already claimed it
            }
            data->notify = nullptr;
            exec = data->exec;
            chain = data->chain;
        }
        if (exec) {
            exec->Execute([ctx = NotifyCtx{notif, std::move(data), std::move(chain)}]() mutable {
                auto n = std::exchange(ctx.notif, nullptr);
                auto self = std::move(ctx.data);
                auto next = std::move(ctx.chain);
                n(self.get(), true);
                continueChain(std::move(next));
            });
            // Done: ran inline (and continued the chain). Defer: will run later.
            // Cancel: dropped, NotifyCtx cleaned it up. In all cases we are done.
            return;
        }
        notif(data.get(), true);
        data = std::move(chain);
    }
}

fut::Future<void> fut::Resolved() {
    fut::Promise<void> prom;
    prom();
    return prom.GetFuture();
}

void fut::d::onLastExc()
{
    fputs("-- Future.AtLast handler exception thrown\n", stderr);
    std::terminate();
}
