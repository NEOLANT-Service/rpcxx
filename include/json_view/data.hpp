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

#ifndef JV_DATA_HPP
#define JV_DATA_HPP

#include <cstdint>

namespace jv
{

struct JsonView;
struct JsonPair;

enum Type : int16_t {
    t_null      = 0,
    t_binary    = 1 << 0,
    t_boolean   = 1 << 1,
    t_number    = 1 << 2,
    t_string    = 1 << 3,
    t_signed    = 1 << 4,
    t_unsigned  = 1 << 5,
    t_array     = 1 << 6,
    t_object    = 1 << 7,
    t_discarded = 1 << 8,
    t_custom    = 1 << 9,

    t_float     = t_number,
    t_any_integer = t_signed | t_unsigned,
    t_any_number = t_number | t_signed | t_unsigned,
};

// can be used to indicate some invariant about JsonView layout
// unused for now
enum Flags : short {
    f_none      = 0,
    //flags below will not be used by library
    f_user      = 1 << sizeof(short) * 4,
};

constexpr Type operator|(Type l, Type r) noexcept {
    return Type(int(l) | int(r));
}
constexpr Type& operator|=(Type& l, Type r) noexcept {
    return l = Type(int(l) | int(r));
}
constexpr Flags operator|(Flags l, Flags r) noexcept {
    return Flags(int(l) | int(r));
}
constexpr Flags& operator|=(Flags& l, Flags r) noexcept {
    return l = Flags(int(l) | int(r));
}

struct Data {
    Type type = {};
    Flags flags = {};
    unsigned size;
    union {
        const char* string;
        const void* binary;
        void* custom;
        const JsonView* array;
        const JsonPair* object;
        double number;
        uint64_t uinteger;
        int64_t integer;
        bool boolean;
    } d;
};

}
#endif // JV_DATA_HPP
