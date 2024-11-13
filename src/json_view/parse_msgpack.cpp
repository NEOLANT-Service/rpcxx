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

#include "json_view/algo.hpp"
#include "json_view/parse.hpp"
#include <algorithm>
#include <string.h>
#include "endian.hpp"

using namespace jv;

#define _CHECK(jv) if (meta_Unlikely(jv.Is(t_discarded))) return jv
#define _LEN(state, l) if (meta_Unlikely(state.Left() < l)) return ErrEOF

namespace {

static auto ErrEOF = JsonView::Discarded("unexpected eof");
static auto ErrOOM = JsonView::Discarded("unexpected oom");
static auto ErrTooDeep = JsonView::Discarded("recursion is too deep");

struct State {
    const char* ptr;
    const char* end;
    ParseSettings& opts;
    meta_alwaysInline
    constexpr size_t Left() const noexcept {
        return end - ptr;
    }
    meta_alwaysInline
    const char* Consume(size_t amount) noexcept {
        return std::exchange(ptr, ptr + amount);
    }
};

JsonView parseObject(unsigned count, State& state, Arena& alloc, unsigned depth) noexcept;
JsonView parseArray(unsigned count, State& state, Arena& alloc, unsigned depth) noexcept;

template<typename T>
static T fromBig(const char* raw) noexcept
{
    static_assert(std::is_trivial_v<T>);
    union {
        T res;
        uint_for_t<T> uint;
    } helper;
    memcpy(&helper.res, raw, sizeof(T));
    if constexpr (endian::host != endian::big) {
        if constexpr (sizeof(T) == 1) {
            //noop
        } else if constexpr (sizeof(T) == 2) {
            helper.uint = be16toh(helper.uint);
        } else if constexpr (sizeof(T) == 4) {
            helper.uint = be32toh(helper.uint);
        } else if constexpr (sizeof(T) == 8) {
            helper.uint = be64toh(helper.uint);
        } else {
            static_assert(always_false<T>, "unsupported");
        }
    }
    return helper.res;
}

template<typename T>
inline JsonView unpackTrivial(State& state) noexcept
{
    _LEN(state, sizeof(T));
    return fromBig<T>(state.Consume(sizeof(T)));
}

template<typename SzT>
JsonView unpackStr(State& state) noexcept
{
    auto len = unpackTrivial<SzT>(state);
    _CHECK(len);
    auto act = len.GetUnsafe().d.uinteger;
    _LEN(state, act);
    return string_view(state.Consume(act), act);
}

template<typename SzT, SzT add = 0>
JsonView unpackBin(State& state) noexcept
{
    auto len = unpackTrivial<SzT>(state);
    _CHECK(len);
    auto act = len.GetUnsafe().d.uinteger + add;
    _LEN(state, act);
    return JsonView::Binary({state.Consume(act), size_t(act)});
}

template<typename SzT>
JsonView unpackArr(State& state, Arena& alloc, unsigned depth) noexcept
{
    auto len = unpackTrivial<SzT>(state);
    _CHECK(len);
    return parseArray(unsigned(len.GetUnsafe().d.uinteger), state, alloc, depth);
}

template<typename SzT>
JsonView unpackObj(State& state, Arena& alloc, unsigned depth) noexcept
{
    auto len = unpackTrivial<SzT>(state);
    _CHECK(len);
    return parseObject(unsigned(len.GetUnsafe().d.uinteger), state, alloc, depth);
}

template<size_t size>
static JsonView unpackExt(State& state) noexcept {
    constexpr auto typeTag = 1;
    constexpr auto total = size + typeTag;
    if (meta_Unlikely(state.Left() < total))
        return ErrEOF;
    return JsonView::Binary({state.Consume(total), total});
}

JsonView parseOne(State& state, Arena& alloc, unsigned depth) noexcept
{
    if (meta_Unlikely(!depth)) {
        return ErrTooDeep;
    }
    if (meta_Unlikely(state.ptr == state.end)) {
        return ErrEOF;
    }
    static_assert(std::numeric_limits<float>::is_iec559, "non IEEE 754 float");
    static_assert(std::numeric_limits<double>::is_iec559, "non IEEE 754 double");
    auto head = uint8_t(*state.ptr++);
    switch (head) {
    case 0xc0: return JsonView{nullptr};
    case 0xc1: return JsonView::Discarded("0xC1 is not allowed in MsgPack");
    case 0xc2: return JsonView{false};
    case 0xc3: return JsonView{true};
    case 0xcc: return unpackTrivial<uint8_t>(state);
    case 0xcd: return unpackTrivial<uint16_t>(state);
    case 0xce: return unpackTrivial<uint32_t>(state);
    case 0xcf: return unpackTrivial<uint64_t>(state);
    case 0xd0: return unpackTrivial<int8_t>(state);
    case 0xd1: return unpackTrivial<int16_t>(state);
    case 0xd2: return unpackTrivial<int32_t>(state);
    case 0xd3: return unpackTrivial<int64_t>(state);
    case 0xca: return unpackTrivial<float>(state);
    case 0xcb: return unpackTrivial<double>(state);
    case 0xd9: return unpackStr<uint8_t>(state);
    case 0xda: return unpackStr<uint16_t>(state);
    case 0xdb: return unpackStr<uint32_t>(state);
    case 0xc4: return unpackBin<uint8_t>(state);
    case 0xc5: return unpackBin<uint16_t>(state);
    case 0xc6: return unpackBin<uint32_t>(state);
    case 0xdc: return unpackArr<uint16_t>(state, alloc, depth - 1);
    case 0xdd: return unpackArr<uint32_t>(state, alloc, depth - 1);
    case 0xde: return unpackObj<uint16_t>(state, alloc, depth - 1);
    case 0xdf: return unpackObj<uint32_t>(state, alloc, depth - 1);
    case 0xd4: return unpackExt<1>(state);
    case 0xd5: return unpackExt<2>(state);
    case 0xd6: return unpackExt<4>(state);
    case 0xd7: return unpackExt<8>(state);
    case 0xd8: return unpackExt<16>(state);
    case 0xc7: return unpackBin<uint8_t, 1>(state);
    case 0xc8: return unpackBin<uint16_t, 1>(state);
    case 0xc9: return unpackBin<uint32_t, 1>(state);
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12:
    case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
    case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45:
    case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
    case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67:
    case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 78:
    case 79: case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88: case 89:
    case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97: case 98: case 99: case 100:
    case 101: case 102:  case 103: case 104: case 105: case 106: case 107: case 108: case 109: case 110:
    case 111: case 112: case 113: case 114: case 115: case 116: case 117: case 118: case 119: case 120:
    case 121: case 122: case 123: case 124: case 125: case 126:
    case 127: { //pos fixint
        return uint8_t(head);
    }
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88:
    case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8e:
    case 0x8f: { //fixmap
        return parseObject(head & 0b1111, state, alloc, depth - 1);
    }
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e:
    case 0x9f: { //fixarr
        return parseArray(head & 0b1111, state, alloc, depth - 1);
    }
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7:
    case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
    case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
    case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe:
    case 0xbf: { //fixstr
        size_t len = head & 0b11111;
        if (meta_Unlikely(state.Left() < len)) {
            return ErrEOF;
        }
        return string_view(state.Consume(len), len);
    }
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: case 0xe6: case 0xe7:
    case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
    case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf7: case 0xf8:
    case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe:
    case 0xff: { //neg fixint
        return int8_t(head);
    }
    default: {
        return {JsonView::Discarded("unknown type")};
    }
    }
}

JsonView parseObject(unsigned count, State& state, Arena& alloc, unsigned int depth) noexcept try
{
    auto obj = MakeObjectOf(count, alloc);
    if (state.opts.sorted) {
        unsigned size = 0;
        for (size_t i = 0u; i < count; ++i) {
            auto key = parseOne(state, alloc, depth);
            _CHECK(key);
            if (meta_Unlikely(!key.Is(t_string)))
                return JsonView::Discarded("keys must be string");
            auto value = parseOne(state, alloc, depth);
            _CHECK(value);
            size = SortedInsertJson(obj, size, {key.GetStringUnsafe(), value}, count);
        }
        Data result{t_object, f_sorted, size, {}};
        result.d.object = obj;
        return JsonView(result);
    } else {
        for (size_t i = 0u; i < count; ++i) {
            auto key = parseOne(state, alloc, depth);
            _CHECK(key);
            if (meta_Unlikely(!key.Is(t_string)))
                return JsonView::Discarded("keys must be string");
            obj[i].key = key.GetStringUnsafe();
            obj[i].value = parseOne(state, alloc, depth);
            _CHECK(obj[i].value);
        }
        return JsonView(obj, count);
    }
} catch(...) {
    return ErrOOM;
}

JsonView parseArray(unsigned int count, State& state, Arena& alloc, unsigned int depth) noexcept try
{
    auto arr = MakeArrayOf(count, alloc);
    for (size_t i = 0u; i < count; ++i) {
        arr[i] = parseOne(state, alloc, depth);
        _CHECK(arr[i]);
    }
    return JsonView{arr, count};
} catch (...) {
    return ErrOOM;
}

} // anon

jv::ParseResult jv::ParseMsgPackInPlace(string_view data, Arena& alloc, ParseSettings opts)
{
    State state{data.data(), data.data() + data.size(), opts};
    auto result = parseOne(state, alloc, opts.maxDepth);
    auto consumed = size_t(state.ptr - data.data());
    if (result.Is(t_discarded)) {
        if (result.GetDiscardReason() == ErrOOM.GetDiscardReason()) {
            throw std::bad_alloc();
        }
        auto err = ParsingError(
            "msgpack parse error: " +
            std::string(result.GetDiscardReason()) +
            " @" + std::to_string(state.ptr - data.data()));
        err.position = state.ptr - data.data();
        throw err;
    }
    return {result, consumed};
}

jv::ParseResult jv::ParseMsgPack(string_view data, Arena& alloc, ParseSettings params)
{
    ArenaString buff(data, alloc);
    return ParseMsgPackInPlace(buff, alloc, params);
}

jv::ParseResult jv::ParseMsgPackInPlace(const void* data, size_t size, Arena& alloc, ParseSettings params)
{
    return ParseMsgPackInPlace(string_view{static_cast<const char*>(data), size}, alloc, params);
}

jv::ParseResult jv::ParseMsgPack(membuff::In& reader, Arena& alloc, ParseSettings params)
{
    ArenaString buff(alloc);
    if (auto hint = reader.TryTotalLeft()) {
        buff.resize(hint);
    }
    char temp[2048];
    while (auto read = reader.Read(temp, sizeof(temp))) {
        buff.Append({temp, read});
    }
    return ParseMsgPackInPlace(buff, alloc, params);
}
