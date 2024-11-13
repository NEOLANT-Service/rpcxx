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

#ifndef JV_DUMP_JSON_HPP
#define JV_DUMP_JSON_HPP
#include "json_view/json.hpp"
#pragma once

#include "membuff/membuff.hpp"
#include "json_view.hpp"

namespace jv
{

struct DumpOptions {
    bool pretty = false;
    unsigned maxDepth = JV_DEFAULT_DEPTH;
    char indentChar = ' ';
    unsigned indent = 4;
};

void DumpJsonInto(membuff::Out& out, JsonView json, DumpOptions opts = {});

inline std::string DumpJson(JsonView j, DumpOptions opts = {}) {
    membuff::StringOut buff;
    DumpJsonInto(buff, j, opts);
    return buff.Consume();
}

void DumpMsgPackInto(membuff::Out& out, JsonView json, DumpOptions opts = {});

inline std::string DumpMsgPack(JsonView j, DumpOptions opts = {}) {
    membuff::StringOut buff;
    DumpMsgPackInto(buff, j, opts);
    return buff.Consume();
}

template<typename T>
JsonView DumpStruct(Arena& alloc) {
    if constexpr (describe::is_described_enum_v<T>) {
        return describe::Get<T>().name;
    } else if constexpr (describe::is_described_struct_v<T>) {
        constexpr auto desc = describe::Get<T>();
        constexpr size_t fields = desc.fields_count;
        auto result = MakeObjectOf(fields, alloc);
        desc.for_each_field([&](auto f){
            using f_t = typename decltype(f)::type;
            auto& curr = result[desc.index_of(f)];
            curr.key = f.name;
            curr.value = DumpStruct<f_t>(alloc);
        });
        return JsonView(result, fields);
    } else if constexpr (is_optional<T>::value) {
        return DumpStruct<typename T::value_type>(alloc);
    } else if constexpr (std::is_convertible_v<T, string_view>) {
        return "string";
    } else if constexpr (is_assoc_container_v<T>) {
        auto result = MakeObjectOf(1, alloc);
        *result = {"<key>", DumpStruct<typename T::mapped_type>(alloc)};
        return JsonView(result, 1);
    } else if constexpr (is_index_container_v<T>) {
        auto result = MakeObjectOf(1, alloc);
        *result = {"[index]", DumpStruct<typename T::value_type>(alloc)};
        return JsonView(result, 1);
    }
    else if constexpr (std::is_same_v<uint8_t, T>) return "uint8";
    else if constexpr (std::is_same_v<T, int8_t>) return "int8";
    else if constexpr (std::is_same_v<T, uint16_t>) return "uint16";
    else if constexpr (std::is_same_v<T, int16_t>) return "int16";
    else if constexpr (std::is_same_v<T, uint32_t>) return "uint32";
    else if constexpr (std::is_same_v<T, int32_t>) return "int32";
    else if constexpr (std::is_same_v<T, uint64_t>) return "uint64";
    else if constexpr (std::is_same_v<T, int64_t>) return "int64";
    else if constexpr (std::is_same_v<T, float>) return "float";
    else if constexpr (std::is_same_v<T, double>) return "double";
    else if constexpr (std::is_same_v<T, bool>) return "bool";
    else if constexpr (std::is_same_v<T, JsonView>) return "json";
    else if constexpr (std::is_same_v<T, Json>) return "json";
    else if constexpr (std::is_same_v<T, MutableJson>) return "json";
    else {
        static_assert(always_false<T>, "Unsupported type in DumpStruct()");
    }
}
} //jv

#endif //JV_DUMP_JSON_HPP
