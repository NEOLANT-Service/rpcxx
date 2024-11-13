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

#define NOMINMAX
#include "json_view/dump.hpp"
#include "endian.hpp"

using namespace jv;

namespace {
template<typename T>
static auto toBig(const T& raw) noexcept
{
    union {
        std::array<uint8_t, sizeof(T)> res;
        uint_for_t<T> uint;
    } helper;
    memcpy(helper.res.data(), &raw, sizeof(T));
    if constexpr (endian::host != endian::big) {
        if constexpr (sizeof(T) == 1) {
            //noop
        } else if constexpr (sizeof(T) == 2) {
            helper.uint = htobe16(helper.uint);
        } else if constexpr (sizeof(T) == 4) {
            helper.uint = htobe32(helper.uint);
        } else if constexpr (sizeof(T) == 8) {
            helper.uint = htobe64(helper.uint);
        } else {
            static_assert(always_false<T>, "unsupported");
        }
    }
    return helper.res;
}
}

static void writeType(uint8_t what, membuff::Out &out) {
    out.Write(what);
}

[[maybe_unused]]
static void write(std::string_view what, membuff::Out &out){
    out.Write(what);
}

template<typename T>
static void write(T what, membuff::Out &out){
    auto temp = toBig(what);
    out.Write(temp.data(), temp.size());
};

using std::numeric_limits;

static inline void writeString(string_view sv, membuff::Out &out)
{
    if (sv.size() <= 0b11111) {
        writeType(uint8_t(0b10100000 | sv.size()), out);
    } else if (sv.size() <= numeric_limits<uint8_t>::max()) {
        writeType(0xd9, out);
        write(uint8_t(sv.size()), out);
    }  else if (sv.size() <= numeric_limits<uint16_t>::max()) {
        writeType(0xda, out);
        write(uint16_t(sv.size()), out);
    } else {
        writeType(0xdb, out);
        write(uint32_t(sv.size()), out);
    }
    out.Write(sv);
}

static inline void writeNegInt(int64_t i, membuff::Out& out) {
    if (i >= -32) {
        writeType(int8_t(i), out);
    } else if (i >= numeric_limits<int8_t>::min()) {
        writeType(0xD0, out);
        write(int8_t(i), out);
    } else if (i >= numeric_limits<int16_t>::min()) {
        writeType(0xD1, out);
        write(int16_t(i), out);
    } else if (i >= numeric_limits<int32_t>::min()) {
        writeType(0xD2, out);
        write(int32_t(i), out);
    } else {
        writeType(0xD3, out);
        write(int64_t(i), out);
    }
}

static inline void writePosInt(uint64_t i, membuff::Out& out) {
    if (i < 128) {
        writeType(uint8_t(i), out);
    } else if (i <= numeric_limits<uint8_t>::max()) {
        writeType(0xCC, out);
        write(uint8_t(i), out);
    } else if (i <= numeric_limits<uint16_t>::max()) {
        writeType(0xCD, out);
        write(uint16_t(i), out);
    } else if (i <= numeric_limits<uint32_t>::max()) {
        writeType(0xCE, out);
        write(uint32_t(i), out);
    } else {
        writeType(0xCF, out);
        write(uint64_t(i), out);
    }
}

#if !defined(__clang__) && !defined(_WIN32)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

void jv::DumpMsgPackInto(membuff::Out &out, JsonView json, DumpOptions opts)
{
    DepthError::Check(opts.maxDepth);
    switch (json.GetType()) {
    case t_array: {
        auto sz = json.GetUnsafe().size;
        if (sz <= 0b1111) {
            writeType(uint8_t(0b10010000 | sz), out);
        } else if (sz <= numeric_limits<uint16_t>::max()) {
            writeType(0xdc, out);
            write(uint16_t(sz), out);
        } else {
            writeType(0xdd, out);
            write(uint32_t(sz), out);
        }
        opts.maxDepth--;
        for (auto v: json.Array()) {
            DumpMsgPackInto(out, v, opts);
        }
        break;
    }
    case t_object: {
        auto sz = json.GetUnsafe().size;
        if (sz <= 0b1111)  {
            writeType(uint8_t(0b10000000 | sz), out);
        } else if (sz <= numeric_limits<uint16_t>::max()) {
            writeType(0xde, out);
            write(uint16_t(sz), out);
        } else {
            writeType(0xdf, out);
            write(uint32_t(sz), out);
        }
        opts.maxDepth--;
        for (auto [k, v]: json.Object()) {
            writeString(k, out);
            DumpMsgPackInto(out, v, opts);
        }
        break;
    }
    case t_null: {
        return writeType(uint8_t(0xc0), out);
    }
    case t_boolean: {
        return writeType(json.GetUnsafe().d.boolean ? uint8_t(0xc3) : uint8_t(0xc2), out);
    }
    case t_number: {
        writeType(uint8_t(0xcb), out);
        return write(json.GetUnsafe().d.number, out);
    }
    case t_signed: {
        auto i = json.GetUnsafe().d.integer;
        if (i < 0) {
            writeNegInt(i, out);
        } else {
            writePosInt(uint64_t(i), out);
        }
        break;
    }
    case t_unsigned: {
        writePosInt(json.GetUnsafe().d.uinteger, out);
        break;
    }
    case t_binary: {
        auto bin = json.GetUnsafe().d.binary;
        auto sz = json.GetUnsafe().size;
        if (sz <= numeric_limits<uint8_t>::max()) {
            writeType(0xc4, out);
            write(uint8_t(sz), out);
        } else if (sz <= numeric_limits<uint16_t>::max()) {
            writeType(0xc5, out);
            write(uint16_t(sz), out);
        } else {
            writeType(0xc6, out);
            write(uint32_t(sz), out);
        }
        auto sv = std::string_view{reinterpret_cast<const char*>(bin), sz};
        write(sv, out);
        break;
    }
    case t_string: {
        writeString(json.GetStringUnsafe(), out);
        break;
    }
    default: {
        break;
    }
    }
}

#if !defined(__clang__) && !defined(_WIN32)
#pragma GCC diagnostic pop
#endif
