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

#ifndef RPCXX_TRANSPORT_HPP
#define RPCXX_TRANSPORT_HPP

#include "common.hpp"
#include "protocol.hpp"
#include "handler.hpp"

namespace rpcxx
{

struct RequestNotify {
    string method;
    Json params;
};

struct RequestMethod : RequestNotify {
    millis timeout;
    Promise<JsonView> cb;
};

struct Batch {
    std::vector<RequestNotify> notifs;
    std::vector<RequestMethod> methods;
};

struct IClientTransport : IHandler {
    virtual void SendBatch(Batch batch) = 0;
    virtual void SendNotify(string_view method, JsonView params) = 0;
    virtual void SendMethod(Method method, JsonView params, Promise<JsonView> cb) = 0;
    virtual ~IClientTransport() = default;
protected:
    void DoHandle(Request& req, Promise<JsonView> cb) noexcept override;
    void DoHandleNotify(Request& req) noexcept override;
};

struct ForwardToHandler final : IClientTransport {
    ForwardToHandler(rc::Weak<IHandler> h = nullptr) noexcept : h(h) {}
    rc::Weak<IHandler> SetHandler(rc::Weak<IHandler> handler);
protected:
    rc::Weak<IHandler> h;
    void SendBatch(Batch batch) override;
    void SendNotify(string_view method, JsonView params) override;
    void SendMethod(Method method, JsonView params, Promise<JsonView> cb) override;
};

//! Bidirectional transport for both server (any IHandler) and Client
struct IAsyncTransport : IClientTransport {
    IAsyncTransport(Protocol proto, rc::Weak<IHandler> h = nullptr);

    rc::Weak<IHandler> SetHandler(rc::Weak<IHandler> handler);
    void ClearAllPending();
    void CheckTimeouts();
    void Receive(JsonView msg, ContextPtr ctx);
    void Receive(JsonView msg);

    ~IAsyncTransport() override;
    IAsyncTransport(const IAsyncTransport&) = delete;
    IAsyncTransport(IAsyncTransport&&) = delete;
protected:
    // These should be used/implemented by subclass
    virtual void Send(JsonView msg) = 0;
    virtual void TimeoutHappened(string_view method, Promise<JsonView>& target);
    virtual void NoServerFound();
private:
    void SendBatch(Batch batch) final;
    void SendNotify(string_view method, JsonView params) final;
    void SendMethod(Method method, JsonView params, Promise<JsonView> cb) final;

    struct Impl;
    FastPimpl<Impl, 144> d;
};

struct Transport final : IAsyncTransport {
    using Sender = MoveFunc<void(JsonView)>;

    Transport(Protocol proto = Protocol::json_v2_compliant);

    void OnReply(Sender callback);
protected:
    // These should be used/implemented by subclass
    void Send(JsonView msg) override;

    Sender sender;
};

}

#endif //RPCXX_TRANSPORT_HPP
