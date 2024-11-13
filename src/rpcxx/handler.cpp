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

#include "rpcxx/handler.hpp"

namespace rpcxx {

using AllRoutes = std::map<string, rc::Weak<IHandler>, std::less<>>;

struct IHandler::Impl {
    AllRoutes routes;
};

IHandler::IHandler()
{

}

void IHandler::OnForward(string_view, Request &, Promise<JsonView> &) noexcept
{

}

void IHandler::OnForwardNotify(string_view, Request &)
{

}

rc::Weak<IHandler> IHandler::GetRoute(string_view route)
{
    auto it = d->routes.find(route);
    if (it == d->routes.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

void IHandler::SetRoute(string_view route, rc::Weak<IHandler> handler)
{
    if (route.empty()) {
        throw std::runtime_error("SetRoute(): cannot be empty");
    }
    if (route.find_first_of('/') != string_view::npos) {
        throw std::runtime_error("Route name must not contain any '/'");
    }
    if (auto h = handler.peek()) {
        d->routes[string{route}] = h;
    } else {
        auto it = d->routes.find(route);
        if (it != d->routes.end()) {
            d->routes.erase(it);
        }
    }
}


//! Important, that this a stable route sanitizers
//! @param method: /a/b////c//d/
//! @returns a/b/c/d (no prefix and trailing '/', no duplicates)
static string_view sanitizeSlashes(string& storage, string_view method) {
    storage.resize(method.size());
    size_t i = 0;
    size_t o = 0;
    bool lastSlash = false;
    for (; i < method.size(); ++i) {
        auto ch = method[i];
        if (ch == '/') {
            lastSlash = true;
        } else {
            if (lastSlash) {
                storage[o++] = '/';
            }
            storage[o++] = ch;
            lastSlash = false;
        }
    }
    storage.resize(o);
    auto res = string_view{storage};
    return res.size() && res.front() == '/' ? res.substr(1) : res;
}

static IHandler* tryRoute(string& storage, AllRoutes& rs, string_view& method, string_view& outRoute) {
    auto raw = sanitizeSlashes(storage, method);
    method = raw;
    auto pos = raw.find_first_of('/');
    if (pos == string_view::npos) return nullptr;
    std::string_view maybeRoute = raw.substr(0, pos);
    auto it = rs.find(maybeRoute);
    if (it == rs.end()) return nullptr;
    auto h = it->second.peek();
    if (!h) return nullptr;
    outRoute = maybeRoute;
    method = raw.substr(pos + 1);
    return h;
}

void IHandler::Handle(Request &request, Promise<JsonView> cb) noexcept
{
    string storage;
    string_view route;
    if (auto h = tryRoute(storage, d->routes, request.method.name, route)) {
        OnForward(route, request, cb);
        h->Handle(request, std::move(cb));
    } else {
        DoHandle(request, std::move(cb));
    }
}

void IHandler::HandleNotify(Request &request)
{
    string storage;
    string_view route;
    if (auto h = tryRoute(storage, d->routes, request.method.name, route)) {
        OnForwardNotify(route, request);
        h->HandleNotify(request);
    } else {
        DoHandleNotify(request);
    }
}

IHandler::~IHandler()
{

}

}
