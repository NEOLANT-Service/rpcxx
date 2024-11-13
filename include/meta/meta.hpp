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

#ifndef META_META_HPP
#define META_META_HPP

#include "compiler_macros.hpp"
#include <type_traits>
#include <stdint.h>
#include <utility>
#include <stddef.h>
#include <optional>

namespace meta {

struct never{};

template<typename...Args> struct TypeList {
    static constexpr auto size = sizeof...(Args);
    static constexpr auto idxs() noexcept {
        return std::make_index_sequence<sizeof...(Args)>();
    }
    template<typename T>
    constexpr bool operator==(T) const {
        return std::is_same_v<TypeList, T>;
    }
    template<typename T> constexpr bool operator!=(T) const {
        return !std::is_same_v<TypeList, T>;
    }
    template<typename T> static constexpr bool Contains() {
        return (std::is_same_v<T, Args> || ...);
    }
};
template<typename Head, typename...Rest>
constexpr TypeList<Rest...> PopFrontType(TypeList<Head, Rest...>) {return {};}
template<typename Pack> struct HeadTypeOf {
    using type = never;
};
template<typename Head, typename...Rest> struct HeadTypeOf<TypeList<Head, Rest...>> {
    using type = Head;
};
template<typename Pack> using HeadTypeOf_t = typename HeadTypeOf<Pack>::type;

template<typename T> constexpr bool always_false = false;

template<typename T, typename=void> struct is_assoc_container : std::false_type{};
template<typename T, typename=void> struct is_index_container : std::false_type{};

namespace det {
template<typename T>
using check_range_t =
    std::void_t<decltype(std::declval<T>().begin()), decltype(std::declval<T>().end())>;
}

//! @warning NOT a fully-compliant container detector
template<typename T> struct is_index_container<T, std::void_t<
    typename T::value_type,
    decltype(std::declval<T>().clear()),
    decltype(std::declval<T>().push_back(std::declval<typename T::value_type>())),
    decltype(std::declval<T>()[std::declval<size_t>()]),
    det::check_range_t<T>
>> : std::true_type{};

//! @warning NOT a fully-compliant container detector
template<typename T> struct is_assoc_container<T, std::void_t<
    typename T::value_type,
    typename T::key_type,
    typename T::mapped_type,
    decltype(std::declval<T>().clear()),
    decltype(std::declval<T>()[std::declval<const typename T::key_type&>()]),
    det::check_range_t<T>
>> : std::true_type{};

//! @warning NOT a fully-compliant container detector
template<typename T> constexpr bool is_assoc_container_v = is_assoc_container<T>::value;
//! @warning NOT a fully-compliant container detector
template<typename T> constexpr bool is_index_container_v = is_index_container<T>::value;

template<typename T> struct is_optional : std::false_type {};
template<typename T> struct is_optional<std::optional<T>> : std::true_type {};
template<typename T> constexpr bool is_optional_v = is_optional<T>::value;

template<typename...Ts> struct overloaded : Ts... {using Ts::operator()...;};
template<typename...Ts> overloaded(Ts...) -> overloaded<Ts...>;

template<typename Fn>
struct [[nodiscard]] defer {
    defer(Fn f) noexcept(std::is_nothrow_move_constructible_v<Fn>) : fn(std::move(f)) {}
    ~defer() noexcept(std::is_nothrow_invocable_v<Fn>) {fn();}
    Fn fn;
};
template<typename Fn> defer(Fn) -> defer<Fn>;

struct empty {};
template<typename T> struct non_void {
    using type = T;
};
template<> struct non_void<void> {
    using type = empty;
};
template<typename T> using non_void_t = typename non_void<T>::type;

template<typename Func, typename=void> struct RipFunc;

template<typename R, typename...A> struct RipFunc<R(A...)> {
    using Ret = R;
    using Args = TypeList<A...>;
    static constexpr auto ArgCount = sizeof...(A);
    static constexpr bool IsMethod = false;
};
template<typename R, typename... A>
struct RipFunc<R (*)(A...)> : RipFunc<R(A...)>
{};
template<typename R, typename... A>
struct RipFunc<R (&)(A...)> : RipFunc<R(A...)>
{};
template<typename C, typename R, typename...A> 
struct RipFunc<R(C::*)(A...)> : RipFunc<R(A...)> {
    using Cls = C;
    static constexpr bool IsMethod = true;
};
template<typename C, typename R, typename...A> 
struct RipFunc<R(C::*)(A...) const> : RipFunc<R(C::*)(A...)> {};
template<typename Fn> struct RipFunc<Fn, std::enable_if_t<std::is_class_v<Fn>>> :
    RipFunc<decltype(&Fn::operator())>
{};
template<typename Fn> using FuncRet_t = typename RipFunc<Fn>::Ret;
template<typename Fn> using FuncArgs_t = typename RipFunc<Fn>::Args;
template<typename Fn> constexpr auto FuncArgCount_v = RipFunc<Fn>::ArgCount;

}

#endif //META_META_HPP
