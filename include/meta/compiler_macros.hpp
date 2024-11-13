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

#ifndef META_COMP_MACROS_H
#define META_COMP_MACROS_H

#include <assert.h>
#ifdef __GNUC__
#define meta_Likely(x)       __builtin_expect(!!(x), 1)
#define meta_Unlikely(x)     __builtin_expect(!!(x), 0)
#define meta_attr_PURE       __attribute__((pure))
#else
#define meta_Likely(x)       (x)
#define meta_Unlikely(x)     (x)
#define meta_attr_PURE
#endif

#ifdef __GNUC__ // GCC, Clang, ICC
#define meta_alwaysInline __attribute__((always_inline))
#define meta_Unreachable() do{assert(false && "unreachable"); __builtin_unreachable();}while(0)
#define meta_Restrict __restrict__
#elif defined(_MSC_VER) // MSVC
#define meta_Restrict
#define meta_alwaysInline __forceinline
#define meta_Unreachable() do{assert(false && "unreachable"); __assume(false);}while(0)
#else
#define meta_Restrict
#define meta_alwaysInline
#define meta_Unreachable() do{assert(false && "unreachable");}while(0)
#endif

// disable min() max() on windows
#define meta_NO_MACRO


#endif //META_COMP_MACROS_H
