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

#include "rpcxx/server.hpp"
#include "rpcxx/exception.hpp"
#include <map>

using namespace rpcxx;

struct rpcxx::Server::Impl {
    Impl() {
        exec = new StoppableExecutor();
        fallbackCtx = new Context;
        current = fallbackCtx;
    }
    ~Impl() {
        exec->Stop();
    }
    rc::Strong<StoppableExecutor> exec;
    std::map<std::string, Call, std::less<>> calls;
    std::vector<Middleware> selfMiddlewares;
    std::vector<ExceptionHandler> EHandlers;
    std::vector<RouteMiddleware> routeMiddlewares;
    std::vector<RouteExceptionHandler> routeEHandlers;
    std::unique_ptr<Fallback> fallback;
    ContextPtr fallbackCtx;
    ContextPtr current;
};

void Server::runMiddlewares(Request& req)
{
    for (auto m = d->selfMiddlewares.rbegin(); m != d->selfMiddlewares.rend(); ++m) {
        (*m)(req);
    }
}

void Server::runRouteMiddlewares(string_view route, Request &req)
{
    for (auto m = d->routeMiddlewares.rbegin(); m != d->routeMiddlewares.rend(); ++m) {
        (*m)(route, req);
    }
}

std::exception_ptr Server::excHandlers(string_view route, string_view method, ContextPtr ctx, std::exception &exc) noexcept
try {
    std::unique_ptr<std::exception> override;
    ExceptionContext ectx = {route, method, std::move(ctx), &exc};
    if (route.empty()) {
        for (auto h = d->EHandlers.rbegin(); h != d->EHandlers.rend(); ++h) {
            if (auto next = (*h)(ectx)) {
                override.reset(next);
                ectx.exception = override.get();
            }
        }
    } else {
        for (auto h = d->routeEHandlers.rbegin(); h != d->routeEHandlers.rend(); ++h) {
            if (auto next = (*h)(route, ectx)) {
                override.reset(next);
                ectx.exception = override.get();
            }
        }
    }
    if (!override) return {};
    if (auto r = dynamic_cast<RpcException*>(ectx.exception)) {
        return std::make_exception_ptr(std::move(*r));
    } else {
        return std::make_exception_ptr(RpcException(ectx.exception->what(), ErrorCode::internal));
    }
} catch (std::exception& unhandled) {
    fprintf(stderr, "RPC: Server => exception during handling of exceptions: %s\n", unhandled.what());
    return std::make_exception_ptr(RpcException("Internal Error", ErrorCode::internal));
}

void Server::AddMiddleware(Middleware mw)
{
    d->selfMiddlewares.push_back(std::move(mw));
}

void Server::AddRouteMiddleware(RouteMiddleware mw)
{
    d->routeMiddlewares.push_back(std::move(mw));
}

ContextPtr Server::CurrentContext()
{
    return d->current;
}

void Server::doAddExceptionHandler(ExceptionHandler h)
{
    d->EHandlers.push_back(std::move(h));
}

void Server::doAddRouteExceptionHandler(RouteExceptionHandler h)
{
    d->routeEHandlers.push_back(std::move(h));
}

void Server::OnForward(string_view route, Request &req, Promise<JsonView> &cb) noexcept
{
    auto orig = std::move(cb);
    cb = Promise<JsonView>{};
    // todo: use timeout somehow
    cb.GetFuture().AtLast(
        GetExecutor(),
        [this, MV(orig), r = string{route}, ctx = req.context, m = string{req.method.name}]
        (Result<JsonView> res) mutable {
            try {
                orig(res.get());
            } catch (std::exception& e) {
                auto over = excHandlers(r, m, ctx, e);
                orig(over ? over : std::current_exception());
            }
        });
    try {
        runRouteMiddlewares(route, req);
    } catch (std::exception& e) {
        auto over = excHandlers(route, req.method.name, std::move(req.context), e);
        cb(over ? std::move(over) : std::current_exception());
    }
}

void Server::OnForwardNotify(string_view route, Request &req)
{
    runRouteMiddlewares(route, req);
}

void Server::SetFallback(Fallback handler)
{
    if (handler) {
        d->fallback.reset(new Fallback{std::move(handler)});
    } else {
        d->fallback.reset();
    }
}

Executor *Server::GetExecutor() const noexcept
{
    return d->exec.get();
}

rpcxx::Server::Server() : d() {

}

rpcxx::Server::~Server()
{}

std::vector<std::string> rpcxx::Server::RegisteredMethods() const
{
    std::vector<std::string> res;
    for (auto& it: d->calls) {
        res.emplace_back(it.first);
    }
    return res;
}

bool rpcxx::Server::IsMethodRegistered(std::string_view method) const
{
    return d->calls.find(method) != d->calls.end();
}

void rpcxx::Server::Unregister(std::string_view method) {
    if (auto it = d->calls.find(method); it != d->calls.end()) {
        d->calls.erase(it);
    } else {
        throw std::runtime_error("Cannot unregister method, not found: " + std::string(method));
    }
}

void Server::DoHandleNotify(Request& req) try
{
    auto& alloc = req.alloc;
    Promise<JsonView> cb{nullptr};
    CallCtx ctx{req, alloc, cb};
    d->current = req.context;
    defer revert([&]{
        d->current = d->fallbackCtx;
    });
    runMiddlewares(req);
    auto found = d->calls.find(req.method.name);
    if (found != d->calls.end()) {
        found->second(ctx);
    }
} catch (std::exception& e) {
    auto over = excHandlers("", req.method.name, req.context, e);
    std::rethrow_exception(over ? std::move(over) : std::current_exception());
}

void Server::DoHandle(Request& req, Promise<JsonView> cb) noexcept try
{
    auto& alloc = req.alloc;
    CallCtx ctx{req, alloc, cb};
    d->current = req.context;
    defer revert([&]{
        d->current = d->fallbackCtx;
    });
    runMiddlewares(req);
    if (req.method.name.substr(0, 4) == "rpc.") {
        handleExtension(ctx);
        return;
    }
    auto found = d->calls.find(req.method.name);
    if (found != d->calls.end()) {
        found->second(ctx);
    } else if (d->fallback) {
        ctx.cb((*d->fallback)(req, alloc));
    } else {
        JsonPair data[] = {
            {"was_method", req.method.name}
        };
        auto exc = RpcException("Method not found", ErrorCode::method_not_found, Json(data));
        auto over = excHandlers("", req.method.name, req.context, exc);
        ctx.cb(over ? over : std::make_exception_ptr(std::move(exc)));
    }
} catch (std::exception& e) {
    auto over = excHandlers("", req.method.name, req.context, e);
    cb(over ? std::move(over) : std::current_exception());
}

void rpcxx::Server::registerCall(std::string& name, Call call) {
    if (name.find("rpc.") == 0) {
        throw std::runtime_error("methods cannot start with 'rpc.' - reserved for extensions");
    }
    auto ok = d->calls.try_emplace(std::move(name), std::move(call)).second;
    if (!ok) {
        throw std::runtime_error("Method already registered: " + std::string(name));
    }
}

void rpcxx::Server::validateRequest(const string *names, unsigned nargs, CallCtx& ctx, bool notif)
{
    if (notif) {
        if (meta_Unlikely(ctx.IsMethodCall()))
            throw RpcException("Expected a notification call, called as method", ErrorCode::invalid_request);
    } else if (meta_Unlikely(!ctx.IsMethodCall())) {
        throw RpcException("Expected a method call, called as notify", ErrorCode::invalid_request);
    }
    if (names) {
        if (!ctx.req.params.Is(jv::t_object)) {
            auto toPrint = MakeArrayOf(nargs, ctx.alloc);
            for (auto i = 0u; i < nargs; ++i) {
                toPrint[i] = JsonView{names[i]};
            }
            JsonPair data[] = {
                {"params_count", nargs},
                {"was_type", ctx.req.params.GetTypeName()},
                {"params_names", JsonView{toPrint, nargs}}
            };
            throw RpcException("Method expected named params", ErrorCode::invalid_params, Json(data));
        }
    } else if (nargs) {
        if (!ctx.req.params.Is(jv::t_array)) {
            JsonPair data[] = {
                {"params_count", nargs},
                {"was_type", ctx.req.params.GetTypeName()}
            };
            throw RpcException("Method expected positional params", ErrorCode::invalid_params, Json(data));
        }
    }
}

void rpcxx::Server::handleExtension(CallCtx& ctx)
{
    if (ctx.req.method.name == "rpc.list") {
        auto arr = MakeArrayOf(unsigned(d->calls.size()), ctx.alloc);
        unsigned count = 0;
        for (auto& it: d->calls) {
            arr[count++] = JsonView(it.first);
        }
        ctx.cb(JsonView(arr, count));
    } else {
        JsonPair data[] = {
            {"was_ext", ctx.req.method.name}
        };
        throw RpcException{"Could not find extension", ErrorCode::method_not_found, Json(data)};
    }
}
