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

#ifndef FUT_GATHER_HPP
#define FUT_GATHER_HPP
#pragma once

#include "future.hpp"
#include <vector>
#include <mutex>
#include <memory>

namespace fut
{

namespace detail
{

template<typename...Args>
using return_t = std::conditional_t<(std::is_void_v<Args> && ...), void, std::tuple<non_void_t<Args>...>>;

template<typename...Args>
struct GatherCtx : rc::DefaultBase {
    std::recursive_mutex mut;
    non_void_t<return_t<Args...>> results {};
    size_t doneCount = {};
    Promise<return_t<Args...>> setter {};
};

template<typename...Args>
using SharedGatherCtx = rc::Strong<GatherCtx<Args...>>;

template<size_t idx, typename T, typename...Args>
void handleSingleFut(SharedGatherCtx<Args...> ctx, Future<T> fut)
{
    fut.AtLastSync([ctx](auto res){
        std::lock_guard lock(ctx->mut);
        if(!ctx->setter.IsValid()) {
            return;
        }
        if (auto&& err = res.get_exception()) {
            ctx->setter(std::move(err));
        } else {
            if constexpr (!std::is_void_v<T>) {
                std::get<idx>(ctx->results) = res.get();
            }
            if (++ctx->doneCount == sizeof...(Args)) {
                if constexpr (std::is_same_v<decltype(ctx->results), meta::empty>) {
                    ctx->setter();
                } else {
                    ctx->setter(std::move(ctx->results));
                }
            }
        }
    });
}

template<size_t...idx, typename...Args>
void callGatherHandlers(SharedGatherCtx<Args...> ctx,
                        std::index_sequence<idx...>,
                        Future<Args>...futs)
{
    (handleSingleFut<idx>(ctx, std::move(futs)), ...);
}

}

template<typename...Args>
Future<detail::return_t<Args...>> GatherTuple(Future<Args>...futs)
{
    static_assert(sizeof...(Args), "Empty Promise List");
    using Ctx = detail::GatherCtx<Args...>;
    auto ctx = rc::Strong(new Ctx);
    auto gathered = ctx->setter.GetFuture();
    callGatherHandlers(std::move(ctx), std::index_sequence_for<Args...>{}, std::move(futs)...);
    return gathered;
}

template<typename Iter, typename Sent>
auto Gather(Iter iter, Sent end)
{
    using futT = typename Iter::value_type;
    using T = typename futT::value_type;
    if constexpr (!std::is_void_v<T>) {
        static_assert(std::is_default_constructible_v<T>);
    }
    using promT = std::conditional_t<std::is_void_v<T>, void, std::vector<T>>;
    using resultsT = std::conditional_t<std::is_void_v<T>, empty, std::vector<T>>;
    if (iter == end) {
        if constexpr (!std::is_void_v<T>)
            return fut::Resolved<promT>(promT{});
        else
            return fut::Resolved();
    }
    struct Ctx {
        std::recursive_mutex mut;
        resultsT results;
        Promise<promT> prom;
        size_t left;
    };
    auto ctx = std::make_shared<Ctx>();
    ctx->left = size_t(std::distance(iter, end));
    if constexpr (!std::is_void_v<T>) {
        ctx->results.resize(ctx->left);
    }
    auto final = ctx->prom.GetFuture();
    size_t idx = 0;
    for (;iter != end; ++iter) {
        auto curr = idx++;
        (*iter).AtLastSync([=](auto res){
            std::lock_guard lock(ctx->mut);
            if (!ctx->prom.IsValid())
                return;
            if (res) {
                if constexpr (!std::is_void_v<T>)
                    ctx->results[curr] = res.get();
                if (!--ctx->left) {                    
                    if constexpr (!std::is_void_v<T>)
                        ctx->prom(std::move(ctx->results));
                    else
                        ctx->prom();
                }
            } else {
                ctx->prom(std::move(res).get_exception());
            }
        });
    }
    return final;
}

template<typename Range>
auto Gather(Range range) {
    return Gather(std::begin(range), std::end(range));
}

} //fut

#endif //FUT_GATHER_HPP
