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
#include <string_view>
#ifndef JV_KEY_HPP
#define JV_KEY_HPP

namespace jv
{

struct Key
{
    constexpr Key() noexcept = default;
    constexpr Key(unsigned idx) noexcept : size(idx) {}
    constexpr Key(std::string_view key) noexcept :
        size(unsigned(key.size())), str(key.data() ? key.data() : "")
    {}
    template<typename Fn>
    constexpr void Visit(Fn&& visitor) const {
        if (str) visitor(std::string_view{str, size});
        else visitor(size);
    }
    bool IsString() const noexcept {
        return bool(str);
    }
    unsigned Index() const noexcept {
        return size;
    }
    std::string_view String() const noexcept {
        return std::string_view{str, size};
    }
protected:
    unsigned size{};
    const char* str{};
};


}

#endif //JV_KEY_HPP
