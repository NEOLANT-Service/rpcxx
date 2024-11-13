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
#include <climits>
#ifndef JV_ALGOS_HPP
#define JV_ALGOS_HPP

#include "json_view.hpp"
#include "pointer.hpp"
#include <algorithm>

namespace jv {

template<typename Range, typename T, typename Comp, typename Eq>
auto SortedInsert(Range& rng, T&& v, Comp cmp, Eq eq)
{
    auto end = rng.end();
    auto pos = std::lower_bound(rng.begin(), end, v, cmp);
    if (pos == end) {
        rng.push_back(std::forward<T>(v));
        return rng.end() - 1;
    } else if (eq(*pos, v)) {
        *pos = std::forward<T>(v);
        return pos;
    } else {
        return rng.insert(pos, std::forward<T>(v));
    }
}

template<typename Range>
auto SortedInsertJson(Range& rng, JsonPair pair) {
    return SortedInsert(rng, pair, KeyLess{}, KeyEq{});
}

template<typename Iter>
auto LowerBoundJson(Iter beg, Iter end, JsonPair const& pair) {
    return std::lower_bound(beg, end, pair, KeyLess{});
}

template<typename Range>
auto LowerBoundJson(Range const& range, JsonPair const& pair) {
    return std::lower_bound(std::begin(range), std::end(range), pair, KeyLess{});
}

// returns new size
inline unsigned SortedInsertJson(JsonPair* storage, unsigned size, JsonPair const& entry, size_t cap = SIZE_MAX) {
    (void)cap;
    auto end = storage + size;
    auto pos = LowerBoundJson(storage, end, entry);
    if (pos == end) {
        assert(size < cap);
        storage[size++] = entry;
    } else if (meta_Unlikely(pos->key == entry.key)) {
        pos->value = entry.value;
    } else {
        assert(size < cap);
        size++;
        auto diff = size_t(end - pos);
        memmove(pos + 1, pos, sizeof(*pos) * diff);
        *pos = entry;
    }
    return size;
}

JsonView Flatten(JsonView src, Arena& alloc, unsigned depth = JV_DEFAULT_DEPTH);

enum CopyFlags {
    NoCopyStrings = 1,
    NoCopyBinary  = 2,
};

JsonView Copy(JsonView src, Arena& alloc, unsigned depth = JV_DEFAULT_DEPTH, unsigned flags = 0);

constexpr auto DEFAULT_MARGIN = std::numeric_limits<double>::epsilon() * 10;
bool DeepEqual(JsonView lhs, JsonView rhs, unsigned depth = JV_DEFAULT_DEPTH, double margin = DEFAULT_MARGIN);

inline bool operator==(JsonView lhs, JsonView rhs) {
    return DeepEqual(lhs, rhs);
}

inline bool operator!=(JsonView lhs, JsonView rhs) {
    return !DeepEqual(lhs, rhs);
}

}

#endif //JV_ALGOS_HPP
