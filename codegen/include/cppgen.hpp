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

#ifndef GEN_CPPGEN_HPP
#define GEN_CPPGEN_HPP

#include "codegen.hpp"

namespace rpcxx::gen::cpp {

struct Guard {
    string begin;
    string end;
};

Guard MakeGuard(string_view file, string_view ns, string_view part = {});
Guard MakeGuard(Namespace const& ns);
string ToNamespace(string_view raw, string sep = "::");
string PrintType(Type t);

namespace server {
string FormatSignature(Params const& params);
string Format(FormatContext& ctx);
}

namespace client {
string FormatSignature(Params const& params);
string Format(FormatContext& ctx);
}

namespace types {
string Format(FormatContext& ctx, vector<string>& attrs);
}

string Format(FormatContext& ctx);

}

#endif //GEN_CPPGEN_HPP
