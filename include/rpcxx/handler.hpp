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

#ifndef RPCXX_HANDLER_HPP
#define RPCXX_HANDLER_HPP

#include "utils.hpp"
#include "context.hpp"
#include "protocol.hpp"

namespace rpcxx {

struct Request {
    Arena &alloc;
    Method method {};
    ContextPtr context {};
    JsonView params {};
};

// Route is a json pointer
struct IHandler : rc::WeakableVirtual {
    // The passkey is constructible only by rc::MakeStrong, which is therefore
    // the only way to create an IHandler (heap-allocated, Strong-owned).
    IHandler(rc::WeakableKey);
    template<typename T>
    void SetTransport(T* tr) {tr->SetHandler(this);}
    rc::Weak<IHandler> GetRoute(string_view route);
    void SetRoute(string_view route, rc::Weak<IHandler> handler);
    void Handle(Request& request, Promise<JsonView> cb) noexcept;
    void HandleNotify(Request& request);
    IHandler(IHandler&&) = delete;
protected:
    // Protected: handlers must live on the heap, owned by rc::Strong, so that
    // rc::Weak references (routes, transports) can be locked safely. Use
    // rc::MakeStrong<T>() to create them. Deletion goes through rc::Strong.
    virtual ~IHandler();
    virtual void OnForward(string_view route, Request& req, Promise<JsonView>& cb) noexcept;
    virtual void OnForwardNotify(string_view route, Request& req);

    virtual void DoHandle(Request& request, Promise<JsonView> cb) noexcept = 0;
    virtual void DoHandleNotify(Request& request) = 0;
private:
    struct Impl;
    FastPimpl<Impl, 64> d;
};

}

#endif //RPCXX_HANDLER_HPP
