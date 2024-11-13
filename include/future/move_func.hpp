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

#ifndef FUT_CALL_ONCE_HPP
#define FUT_CALL_ONCE_HPP

#include <cassert>
#include <stdexcept>
#include <utility>
#include <cstddef>
#include <type_traits>
#include "meta/meta.hpp"

namespace fut {

using namespace meta;

struct InvalidMoveFuncCall : public std::exception {
    const char* what() const noexcept {
        return "Invalid MoveFunc Call";
    }
};

template<typename Ret, typename...Args>
struct FuncSig {
    using R = Ret;
    using A = meta::TypeList<Args...>;
};

namespace detail {

constexpr size_t SOO = sizeof(void*) * 3;
enum Op { destroy, move,};

union Storage {
    alignas(std::max_align_t) char _small[SOO];
    void* _big;
};

template<typename Fn>
inline Fn* cast(detail::Storage* s) noexcept {
    return sizeof(Fn) > detail::SOO ? static_cast<Fn*>(s->_big) : std::launder(reinterpret_cast<Fn*>(s->_small));
}

template<typename Ret, typename Fn, typename...Args>
Ret _invoke(Storage* s, Args...args) {
    auto& fn = *cast<Fn>(s);
    return fn(std::forward<Args>(args)...);
}

template<typename Fn>
void _manager(Op op, detail::Storage* s, void* o) noexcept {
    constexpr auto isBig = sizeof(Fn) > detail::SOO;
    switch (op) {
    case destroy: {
        if constexpr (isBig) delete cast<Fn>(s);
        else cast<Fn>(s)->~Fn();
        break;
    }
    case move: {
        auto other = static_cast<detail::Storage*>(o);
        if constexpr (isBig) {
            s->_big = std::exchange(other->_big, nullptr);
        } else {
            new (s->_small) Fn(std::move(*cast<Fn>(other)));
            cast<Fn>(other)->~Fn();
        }
        break;
    }
    }
}
}

template<typename Sig> class MoveFunc;

template<typename Ret, typename...Args>
class MoveFunc<Ret(Args...)>
{
    mutable detail::Storage stor;
    void(*manager)(detail::Op, detail::Storage*, void*) noexcept = nullptr;
    Ret(*call)(detail::Storage*, Args...) = nullptr;
public:
    using Sig = FuncSig<Ret, Args...>;
    MoveFunc() noexcept = default;

    template<typename Fn>
    static constexpr bool _valid = std::is_move_constructible_v<Fn> && std::is_invocable_v<Fn, Args&&...>;

    template<typename Fn, typename = std::enable_if_t<_valid<Fn>>>
    MoveFunc(Fn f) :
        manager(detail::_manager<Fn>),
        call(detail::_invoke<Ret, Fn, Args...>)
    {
        if constexpr (!std::is_void_v<Ret>) {
            static_assert(std::is_convertible_v<std::invoke_result_t<Fn, Args&&...>, Ret>);
        }
        if constexpr (sizeof(Fn) > detail::SOO) {
            stor._big = new Fn(std::move(f));
        } else {
            new (stor._small) Fn(std::move(f));
        }
    }
    explicit operator bool() const noexcept {
        return manager;
    }
    meta_alwaysInline Ret operator()(Args...a) const {
        if (!manager) {
            throw InvalidMoveFuncCall();
        }
        return call(const_cast<detail::Storage*>(&stor), std::forward<Args>(a)...);
    }
    MoveFunc(MoveFunc&& oth) noexcept {
        moveIn(oth);
    }
    MoveFunc& operator=(MoveFunc&& oth) noexcept {
        // cannot just swap => cannot swap small storage for
        // non-trivially movable types (pinned-like)
        if (this != &oth) {
            deref();
            moveIn(oth);
        }
        return *this;
    }
    ~MoveFunc() {
        deref();
    }
private:
    void deref() noexcept {
        if (manager) {
            manager(detail::destroy, &stor, nullptr);
        }
    }
    void moveIn(MoveFunc& oth) noexcept {
        call = std::exchange(oth.call, nullptr);
        manager = std::exchange(oth.manager, nullptr);
        if (manager) {
            manager(detail::move, &stor, &oth.stor);
        }
    }
};

template<typename Ret, typename...Args>
MoveFunc(Ret(*)(Args...)) -> MoveFunc<Ret(Args...)>;

} //fut

#endif // FUT_CALL_ONCE_HPP
