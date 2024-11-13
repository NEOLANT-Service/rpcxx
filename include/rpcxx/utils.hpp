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

#ifndef RPCXX_UTILS_HPP
#define RPCXX_UTILS_HPP

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace rpcxx
{

template<typename T, size_t size, size_t align = alignof(std::max_align_t)>
struct FastPimpl {
    template<typename...Args>
    FastPimpl(Args&&...a) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        static_assert(alignof(T) <= align);
        static_assert(sizeof(T) <= size);
        new (buff) T(std::forward<Args>(a)...);
    }
    T* data() noexcept {
        return std::launder(reinterpret_cast<T*>(buff));
    }
    const T* data() const noexcept {
        return std::launder(reinterpret_cast<const T*>(buff));
    }
    T* operator->() noexcept {
        return data();
    }
    const T* operator->() const noexcept {
        return data();
    }
    FastPimpl(const FastPimpl& o) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        new (buff) T(*o.data());
    }
    FastPimpl(FastPimpl&& o) noexcept(std::is_nothrow_move_constructible_v<T>) {
        new (buff) T(std::move(*o.data()));
    }
    FastPimpl& operator=(const FastPimpl& o) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        *data() = *o.data();
    }
    FastPimpl& operator=(FastPimpl& o) noexcept(std::is_nothrow_move_assignable_v<T>) {
        *data() = std::move(*o.data());
    }
    ~FastPimpl() {
        data()->~T();
    }
protected:
    alignas(align) char buff[size];
};

}

#endif //RPCXX_UTILS_HPP
