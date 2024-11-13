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
    if (auto notif = notify.exchange(nullptr)) {
        notif(this, false);
    }
    while (chain && chain->_refs.load(std::memory_order_acquire) == 1) {
        auto next = std::move(chain->chain);
        chain = {};
        chain = std::move(next);
    }
}

namespace {
struct NotifyCtx {
    fut::Base::Notify notif;
    rc::Strong<fut::Base> data;

    NotifyCtx(fut::Base::Notify notif, rc::Strong<fut::Base> data) noexcept :
        notif(notif), data(data)
    {}
    NotifyCtx(const NotifyCtx&) noexcept = delete;
    NotifyCtx(NotifyCtx&& o) noexcept :
        notif(std::exchange(o.notif, nullptr)),
        data(std::exchange(o.data, nullptr))
    {}

    ~NotifyCtx() {
        if (notif) notif(data.get(), false);
    }
};
}

void fut::d::continueChain(rc::Strong<Base> data, bool once) noexcept
{
    do {
        auto fs = data->flags.load(std::memory_order_acquire);
        if (!(fs & Base::fullfilled) || fs & Base::in_continue) {
            break;
        }
        auto notif = data->notify.exchange(nullptr);
        if (!notif) {
            break;
        }
        auto exec = data->exec;
        if (exec) {
            data->flags.fetch_or(Base::in_continue);
            auto status = exec->Execute([ctx = NotifyCtx{notif, data}]() mutable {
                auto n = std::exchange(ctx.notif, nullptr);
                auto d = std::exchange(ctx.data, nullptr);
                assert(n && d && "Executor::Job executed more than once");
                n(d.get(), true);
            });
            data->flags.fetch_and(~Base::in_continue);
            if (status != Executor::Done) {
                break; // Stop chain
            }
        } else {
            data->flags.fetch_or(Base::in_continue);
            notif(data.get(), true);
            data->flags.fetch_and(~Base::in_continue);
        }
        auto ch = std::move(data->chain);
        data = std::move(ch);
    } while(data && !once);
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
