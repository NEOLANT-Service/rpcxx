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

#ifndef JV_JSON_ALGORITHM_HPP
#define JV_JSON_ALGORITHM_HPP

#include "json_view.hpp"
#include "membuff/membuff.hpp"
#include <charconv>

namespace jv {

struct JsonKey {
    constexpr JsonKey() noexcept = default;
    constexpr JsonKey(JsonKey const&) noexcept = default;
    constexpr JsonKey(JsonKey &&) noexcept = default;
    constexpr JsonKey& operator=(JsonKey const&) noexcept = default;
    constexpr JsonKey& operator=(JsonKey &&) noexcept = default;
    constexpr JsonKey(string_view key) noexcept :
        size(unsigned(key.size())),
        string(key.data())
    {
        string = string ? string : "";
    }
    constexpr JsonKey(unsigned idx) noexcept :
        size(idx)
    {}
    constexpr JsonKey(const char* key) noexcept :
        JsonKey(string_view{key})
    {
    }
    template<typename Fn>
    constexpr decltype(auto) Visit(Fn&& f) const {
        if (string) {
            return f(string_view{string, size});
        } else {
            return f(size);
        }
    }
    template<typename KFn, typename IFn>
    constexpr decltype(auto) Visit(KFn&& key, IFn&& id) const {
        if (string) {
            return key(string_view{string, size});
        } else {
            return id(size);
        }
    }
protected:
    //use padding here?
    unsigned size{};
    const char* string{};
};

struct JsonPointer {
    const JsonKey* keys = {};
    unsigned size = {};
    constexpr JsonPointer() noexcept = default;
    constexpr JsonPointer(const JsonKey* keys, unsigned size) noexcept :
        keys(keys), size(size)
    {}
    template<size_t N>
    constexpr JsonPointer(const JsonKey (&keys)[N]) noexcept :
        keys(keys), size(N)
    {}
    static constexpr unsigned npos = (std::numeric_limits<unsigned>::max)();
    static JsonPointer FromString(std::string_view ptr, Arena& alloc, char sep);
    static JsonPointer FromString(std::string_view ptr, Arena& alloc);
    void JoinInto(membuff::Out& out, char sep = '/', bool asUri = false) const;
    std::string Join(char sep = '/', bool asUri = false) const;
    constexpr JsonPointer SubPtr(unsigned begin, unsigned len = npos) const {
        if (size <= begin) {
            throw std::out_of_range{"JsonPointer SubPtr(): "
                                    + std::to_string(begin)
                                    + " >= " + std::to_string(size)};
        }
        auto left = size - begin;
        return {keys + begin, len > left ? left : len};
    }
    constexpr const JsonKey* begin() const noexcept {return keys;}
    constexpr const JsonKey* end() const noexcept {return keys + size;}
    constexpr const JsonKey* cbegin() const noexcept {return keys;}
    constexpr const JsonKey* cend() const noexcept {return keys + size;}
};

namespace detail {
template<typename F, typename K>
void doDeepIterate(JsonView view, K& keys, F& cb, unsigned depth)
{
    DepthError::Check(depth--);
    if (view.Is(t_object)) {
        for (auto [k, v]: view.Object()) {
            keys.emplace_back(k);
            doDeepIterate(v, keys, cb, depth);
            keys.pop_back();
        }
    } else if (view.Is(t_array)) {
        unsigned idx = 0;
        for (auto v: view.Array()) {
            keys.emplace_back(idx++);
            doDeepIterate(v, keys, cb, depth);
            keys.pop_back();
        }
    } else {
        cb(JsonPointer{keys.data(), unsigned(keys.size())}, view);
    }
}
}

template<typename F, typename = std::enable_if_t<std::is_invocable_v<F, JsonPointer, JsonView>>>
void DeepIterate(JsonView view, Arena& alloc, F&&f, unsigned depth = JV_DEFAULT_DEPTH) {
    ArenaVector<JsonKey> keys(alloc);
    detail::doDeepIterate(view, keys, f, depth);
}

} //jv

#endif // JV_JSON_ALGORITHM_HPP
