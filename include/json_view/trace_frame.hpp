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

#ifndef JV_TRACE_FRAME_HPP
#define JV_TRACE_FRAME_HPP

#include <string_view>
#include <string>
#include "meta/meta.hpp"

namespace jv
{

struct TraceFrame {
    constexpr TraceFrame() noexcept {}
    TraceFrame(TraceFrame&&) = delete;
    constexpr TraceFrame(unsigned idx, TraceFrame const& prev) noexcept :
        prev(&prev), size(idx), str(nullptr)
    {}
    constexpr TraceFrame(std::string_view key, TraceFrame const& prev) noexcept :
        prev(&prev), size(unsigned(key.size())), str(key.data())
    {
        str = str ? str : "";
    }
    template<typename F> void Walk(F&& f) const {
        auto node = prepareWalk();
        while(node) {
            if(node->str) f(std::string_view{node->str, node->size});
            else f(node->size);
            node = node->next;
        }
    }
    constexpr void SetIndex(unsigned _idx) noexcept {
        size = _idx;
        str = nullptr;
    }
    constexpr void SetKey(std::string_view key) noexcept {
        size = unsigned(key.size());
        str = key.data();
    }
    std::string PrintTrace() const
    {
        std::string result;
        Walk(meta::overloaded{
            [&](std::string_view key){
                result += '.';
                result += key;
            },
            [&](unsigned idx){
                result += std::string_view(".[");
                result += std::to_string(idx);
                result += ']';
            }
        });
        return result;
    }
private:
    const TraceFrame* prepareWalk() const {
        if (!prev)
            return nullptr;
        const TraceFrame* parent = prev;
        const TraceFrame* current = this;
        while(parent) {
            parent->next = current;
            current = parent;
            parent = parent->prev;
        }
        return current->next;
    }

    const TraceFrame* prev{};
    mutable const TraceFrame* next{};
    unsigned size{};
    const char* str{};
};

}

#endif // JV_TRACE_FRAME_HPP
