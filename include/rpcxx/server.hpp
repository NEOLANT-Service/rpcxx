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

#pragma once
#ifndef RPCXX_SERVER_HPP
#define RPCXX_SERVER_HPP

#include "rpcxx/utils.hpp"
#include "common.hpp"
#include "protocol.hpp"
#include "transport.hpp"
#include "meta/visit.hpp"
#include "handler.hpp"
#include <memory>

namespace rpcxx
{

template<typename...Args>
std::array<std::string, sizeof...(Args)> NamesMap(Args&&...names) {
    return {std::string{std::forward<Args>(names)}...};
}

template<typename T>
struct PackParams {
    static_assert(describe::is_described_v<T> && !std::is_enum_v<T>);
    using type = T;
};

namespace detail {
template<typename T> T get(JsonView json, unsigned idx, TraceFrame& frame);
template<typename T> T get(JsonView json, string_view key, TraceFrame& frame);
template<typename T> struct is_pack : std::false_type {};
template<typename T> struct is_pack<PackParams<T>> : std::true_type {};
template<typename T> struct is_pack<const PackParams<T>> : std::true_type {};
}

struct MiddlewareContext {
    string_view route;
    Request& request;

    MiddlewareContext(Request& req) noexcept : request(req) {}
    MiddlewareContext(MiddlewareContext const&) = delete;
};

struct ExceptionContext {
    string_view route;
    string_view method;
    ContextPtr context;
    std::exception* exception;

    ExceptionContext() noexcept = default;
    ExceptionContext(ExceptionContext const&) = delete;
};

struct Server : IHandler
{
    using RouteMiddleware = MoveFunc<void(string_view route, Request& request)>;
    using Middleware = MoveFunc<void(Request& request)>;
    //! return new <Exception> to override for further handlers and client
    using ExceptionHandler = MoveFunc<std::exception*(ExceptionContext& ctx)>;
    using RouteExceptionHandler = MoveFunc<std::exception*(string_view route, ExceptionContext& ctx)>;

    using Fallback = MoveFunc<JsonView(Request& req, Arena& alloc)>;
    using NoNames = const int*;

    Server();
    Server(const Server&) = delete;
    Server(Server&&) = delete;
    virtual ~Server();

    bool IsMethodRegistered(std::string_view method) const;
    std::vector<std::string> RegisteredMethods() const;
    void Unregister(std::string_view method);

    void AddMiddleware(Middleware mw);
    void AddRouteMiddleware(RouteMiddleware mw);

    ContextPtr CurrentContext();

    template<typename Fn>
    void AddRouteExceptionHandler(Fn f) {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, string_view, ExceptionContext&>>) {
            doAddRouteExceptionHandler([MV(f)](string_view route, ExceptionContext& ectx) mutable {
                f(route, ectx);
                return nullptr;
            });
        } else {
            doAddRouteExceptionHandler(std::move(f));
        }
    }
    template<typename Fn>
    void AddExceptionHandler(Fn f) {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, ExceptionContext&>>) {
            doAddExceptionHandler([MV(f)](ExceptionContext& ectx) mutable {
                f(ectx);
                return nullptr;
            });
        } else {
            doAddExceptionHandler(std::move(f));
        }
    }

    void SetFallback(Fallback handler);

    template<typename Fn, typename Names = NoNames>
    void Method(string_view method, Fn handler, Names names = {}) {
        doRegisterMethod<FuncRet_t<Fn>>(string{method}, std::move(handler), names, FuncArgs_t<Fn>{});
    }
    template<typename Fn, typename Names = NoNames>
    void Notify(string_view method, Fn handler, Names names = {}) {
        doRegisterNotify(string{method}, std::move(handler), names, FuncArgs_t<Fn>{});
    }
protected:
    struct CallCtx {
        Request& req;
        Arena& alloc;
        Promise<JsonView>& cb;

        bool IsMethodCall() const noexcept {return cb.IsValid();}
    };
    template<typename User, typename Ret, typename...Args, typename Names = NoNames>
    void Notify(string_view method, Ret (User::*handler)(Args...), Names names = {}) {
        Notify(method, [this, handler](Args...a) {
            (static_cast<User*>(this)->*handler)(std::forward<Args>(a)...);
        }, names);
    }
    template<auto handler, typename Names = NoNames>
    void Notify(string_view method, Names names = {}) {
        implCall<false, handler>(method, names, ::meta::FuncArgs_t<decltype(handler)>{});
    }
    template<auto handler, typename Names = NoNames>
    void Method(string_view method, Names names = {}) {
        implCall<true, handler>(method, names, ::meta::FuncArgs_t<decltype(handler)>{});
    }
    template<typename User, typename Ret, typename...Args, typename Names = NoNames>
    void Method(string_view method, Ret (User::*handler)(Args...), Names names = {}) {
        Method(method, [this, handler](Args...a) -> Ret {
            return (static_cast<User*>(this)->*handler)(std::forward<Args>(a)...);
        }, names);
    }
    virtual fut::Executor* GetExecutor() const noexcept;
private:
    void doAddRouteExceptionHandler(RouteExceptionHandler h);
    void doAddExceptionHandler(ExceptionHandler h);
    virtual void OnForward(string_view route, Request& req, Promise<JsonView>& cb) noexcept override final;
    virtual void OnForwardNotify(string_view route, Request& req) override final;
    void DoHandleNotify(Request& req) final;
    void DoHandle(Request& req, Promise<JsonView> cb) noexcept override final;

    template<bool ismethod, auto handler, typename Names, typename...Args>
    void implCall(string_view method, Names names, TypeList<Args...>) {
        using cls = typename ::meta::RipFunc<decltype(handler)>::Cls;
        auto impl = [this](Args...a) {
            return (static_cast<cls*>(this)->*handler)(std::forward<Args>(a)...);
        };
        if constexpr (ismethod) {
            Method(method, impl, names);
        } else {
            Notify(method, impl, names);
        }
    }

    template<typename Fn, typename...Args, typename Names>
    void doRegisterNotify(std::string method, Fn handler, Names names, TypeList<Args...>)
    {
        registerCall(method, [=, MV(names), MV(handler)](CallCtx& ctx){
            constexpr auto args = TypeList<Args...>{};
            validateRequest(names, sizeof...(Args), ctx, true);
            doCall(handler, ctx.req.params, names, args, args.idxs());
        });
    }

    template<typename T>
    struct Wrap {
        string method;
        Server* self;
        Promise<JsonView> cb;
        ContextPtr ctx;
        void operator()(Result<T> result) noexcept try {
            if constexpr (std::is_void_v<T>) {
                result.get();
                cb(JsonView(nullptr));
            } else {
                DefaultArena<512> alloc;
                cb(JsonView::From(result.get(), alloc));
            }
        } catch (std::exception& e) {
            auto over = self->excHandlers("", method, std::move(ctx), e);
            cb(over ? std::move(over) : std::current_exception());
        }
    };

    template<typename Ret, typename...Args, typename Fn, typename Names>
    void doRegisterMethod(std::string method, Fn handler, Names names, TypeList<Args...>)
    {
        if constexpr (fut::is_future<Ret>::value) {
            registerCall(method, [=, MV(names), MV(handler)](CallCtx& ctx){
                constexpr auto args = TypeList<Args...>{};
                validateRequest(names, sizeof...(Args), ctx);
                doCall(handler, ctx.req.params, names, args, args.idxs())
                    .AtLast(GetExecutor(), Wrap<typename Ret::value_type>{
                        method, this, std::move(ctx.cb), ctx.req.context
                    });
            });
        } else {
            registerCall(method, [=, MV(names), MV(handler)](CallCtx& ctx){
                constexpr auto args = TypeList<Args...>{};
                validateRequest(names, args.size, ctx);
                if constexpr (std::is_void_v<Ret>) {
                    doCall(handler, ctx.req.params, names, args, args.idxs());
                    ctx.cb(nullptr);
                } else {
                    auto ret = doCall(handler, ctx.req.params, names, args, args.idxs());
                    ctx.cb(JsonView::From(ret, ctx.alloc));
                }
            });
        }
    }
    template<typename Fn, typename Names, typename...Args, size_t...Is>
    static auto doCall(Fn& fn, JsonView params, Names& names, TypeList<Args...>, std::index_sequence<Is...>)
    {
        TraceFrame root;
        TraceFrame frame("<params>", root);
        (void)params;
        if constexpr (std::is_same_v<Names, const NoNames>) {
            return fn(detail::get<std::decay_t<Args>>(params, Is, frame)...);
        } else if constexpr (detail::is_pack<Names>::value) {
            static_assert(sizeof...(Args) == 1);
            static_assert(std::is_same_v<std::decay_t<Args>..., typename Names::type>);
            return fn(params.Get<typename Names::type>(frame));
        } else {
            static constexpr auto namesCount = sizeof(Names) / sizeof(typename Names::value_type);
            static_assert(sizeof...(Args) == namesCount, "Names Map must be the same size as Args Count");
            return fn(detail::get<std::decay_t<Args>>(params, names[Is], frame)...);
        }
    }
    using Call = MoveFunc<void(CallCtx&)>;
    void validateRequest(NoNames, unsigned nargs, CallCtx& ctx, bool notif = false) {
        validateRequest(static_cast<const std::string*>(nullptr), nargs, ctx, notif);
    }
    void validateRequest(const std::string* names, unsigned int nargs, CallCtx& ctx, bool notif = false);
    template<size_t N>
    void validateRequest(const std::array<std::string, N>& n, unsigned, CallCtx& ctx, bool notif = false) {
        validateRequest(n.data(), N, ctx, notif);
    }
    template<typename T>
    void validateRequest(PackParams<T>, unsigned, CallCtx& ctx, bool notif = false) {
        constexpr auto desc = describe::Get<T>();
        constexpr auto rawNames = describe::field_names<T>();
        constexpr auto count = rawNames.size();
        static const std::array<std::string, count> names = prepNames<count>(rawNames);
        validateRequest(names.data(), count, ctx, notif);
    }
    template<size_t N, typename Src>
    static std::array<std::string, N> prepNames(const Src& s) {
        std::array<std::string, N> res;
        for (size_t i{}; i < N; ++i) {res[i] = s[i];}
        return res;
    }
    void runMiddlewares(Request &req);
    void runRouteMiddlewares(string_view route, Request &req);
    std::exception_ptr excHandlers(string_view route, string_view method, ContextPtr ctx, std::exception& exc) noexcept;
    void registerCall(std::string& name, Call call);
    void doHandle(uint32_t internal, JsonView req, uint32_t timeout);
    void handleExtension(CallCtx &ctx);

    struct Impl;
    FastPimpl<Impl, 192> d;
};

namespace detail {
template<typename T> T get(JsonView json, unsigned idx, TraceFrame& frame) {
    TraceFrame next(idx, frame);
    if constexpr (is_optional_v<T>) {
        return json.Value(idx, JsonView{}, frame).Get<T>(next);
    } else {
        return json.At(idx, next).Get<T>(next);
    }
}
template<typename T> T get(JsonView json, string_view key, TraceFrame& frame) {
    TraceFrame next(key, frame);
    if constexpr (is_optional_v<T>) {
        return json.Value(key, JsonView{}, frame).Get<T>(next);
    } else {
        return json.At(key, frame).Get<T>(next);
    }
}
}

} //rpcxx

#endif // RPCXX_SERVER_HPP
