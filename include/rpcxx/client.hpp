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

#ifndef RPCXX_CLIENT_HPP
#define RPCXX_CLIENT_HPP

#include "common.hpp"
#include "protocol.hpp"
#include "transport.hpp"
#include "rpcxx/utils.hpp"

namespace rpcxx
{

struct ClientTransportMissing : public std::exception {
    const char* what() const noexcept override {
        return "Client Tranport Not Set";
    }
};

struct Client
{
    using millis = rpcxx::millis;
    Client(rc::Weak<IClientTransport> t = nullptr);
    virtual ~Client();
    Client(const Client&) = delete;
    Client(Client&&) = delete;

    struct [[nodiscard]] BatchGuard;

    BatchGuard StartBatch();
    rc::Weak<IClientTransport> GetTransport() noexcept;
    rc::Weak<IClientTransport> SetTransport(rc::Weak<IClientTransport> t) noexcept;
    template<typename Ret, typename Args>
    Future<Ret> RequestPack(Method method, const Args& pack) {
        DefaultArena arena;
        return RequestRaw<Ret>(method, JsonView::From(pack, arena));
    }
    template<typename Args>
    void NotifyPack(string_view method, const Args& pack) {
        DefaultArena arena;
        return NotifyRaw(method, JsonView::From(pack, arena));
    }
    template<typename Ret>
    Future<Ret> RequestRaw(Method method, JsonView params) {
        auto cb = Promise<JsonView>();
        auto result = cb.GetFuture().ThenSync([](JsonView res) -> Ret {
            TraceFrame root;
            auto frame = TraceFrame("(rpc.result)", root);
            if constexpr (std::is_void_v<Ret>) {
                res.AssertType(t_null, frame);
            } else {
                return res.Get<Ret>(frame);
            }
        });
        sendRequest(std::move(cb), method, params);
        return result;
    }
    template<typename Ret, typename...Args>
    Future<Ret> Request(Method method, const Args&...args) {
        IntoParams<Args...> wrap(args...);
        return RequestRaw<Ret>(method, wrap.Result);
    }
    void NotifyRaw(string_view method, JsonView params);
    template<typename...Args>
    void Notify(string_view method, const Args&...args) {
        IntoParams<Args...> wrap(args...);
        NotifyRaw(method, wrap.Result);
    }
    void SetPrefix(string prefix);
private:
    void sendRequest(Promise<JsonView> cb, Method method, JsonView params);
    void batchDone();
    IClientTransport& tr();

    bool batchActive = false;
    Batch currentBatch;
    rc::Weak<IClientTransport> transport = nullptr;
    string prefix = "";
};

struct Client::BatchGuard {
    BatchGuard(Client& cli) : cli(cli) {}
    void Finish() {
        if (std::exchange(valid, false))
            cli.batchDone();
    }
    ~BatchGuard() noexcept(false) {
        Finish();
    }
private:
    Client& cli;
    bool valid = true;
};

inline Client::BatchGuard Client::StartBatch() {
    if (meta_Unlikely(std::exchange(batchActive, true))) {
        throw std::runtime_error("Cannot StartBatch() while one is active");
    }
    return {*this};
}

} //rpcxx

#endif // RPCXX_CLIENT_HPP
