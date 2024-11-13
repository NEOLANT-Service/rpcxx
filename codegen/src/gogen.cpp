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

#include "gogen.hpp"
#include <cassert>

using namespace rpcxx::gen;

static string builtin(Builtin builtin)
{
    using namespace std::string_literals;
    switch (builtin.kind) {
    case Builtin::Json: return "any"s;
    case Builtin::Json_View: return "any"s;
    case Builtin::Bool: return "bool"s;
    case Builtin::Int8: return "int8"s;
    case Builtin::Uint8: return "uint8"s;
    case Builtin::Int16: return "int16"s;
    case Builtin::Uint16: return "uint16"s;
    case Builtin::Int32: return "int32"s;
    case Builtin::Uint32: return "uint32"s;
    case Builtin::Int64: return "int64"s;
    case Builtin::Uint64: return "uint64"s;
    case Builtin::Float: return "float32"s;
    case Builtin::Double: return "float64"s;
    case Builtin::String: return "string"s;
    case Builtin::String_View: return "string"s;
    case Builtin::Binary: return "any"s;
    case Builtin::Binary_View: return "any"s;
    case Builtin::Void: return ""s;
    default: throw Err("NonBuiltin passed into Builtin Handler");
    }
}

static string PrintType(Type t) {
    return Visit(
        t->AsVariant(),
        [](Builtin b) -> string {
            return builtin(b);
        },
        [](Optional& o) -> string {
            return fmt::format(FMT_COMPILE("*{}"), PrintType(o.item));
        },
        [](Map& m) -> string {
            return fmt::format(FMT_COMPILE("map[string]{}"), PrintType(m.item));
        },
        [](Array& a) -> string {
            return fmt::format(FMT_COMPILE("[]{}"), PrintType(a.item));
        },
        [](WithDefault& a) -> string {
            return PrintType(a.item);
        },
        [](TypeBase& v) -> string {
            return fmt::format("{}{}", v.ns.depth ? v.ns.name + "." : "", v.name);
        }
        );
}

constexpr auto Tab = "    ";

static string pascalCase(string_view src) {
    string res(src);
    if (res.size()) {
        res[0] = std::toupper(res[0]);
    }
    size_t in = 0;
    size_t out = 0;
    for(;in < res.size(); ++in) {
        auto ch = res[in];
        if (ch == '_' && in != res.size() - 1) {
            res[out++] = std::toupper(res[++in]);
        } else {
            res[out++] = ch;
        }
    }
    res.resize(out);
    return res;
}

static string formatFields(Struct& s) {
    string res;
    size_t maxNameLen = 0;
    size_t maxTypeLen = 0;
    vector<string> pascalNames;
    vector<string> types;
    std::sort(s.fields.begin(), s.fields.end(), [](Struct::Field& l, Struct::Field& r){
        return l.name < r.name;
    });
    for (auto& f: s.fields) {
        auto& n = pascalNames.emplace_back(pascalCase(f.name));
        if (n.size() > maxNameLen) {
            maxNameLen = n.size();
        }
        auto& t = types.emplace_back(PrintType(f.type));
        if (t.size() > maxTypeLen) {
            maxTypeLen = t.size();
        }
    }
    size_t idx = 0;
    for (auto& f: s.fields) {
        auto i = idx++;
        string attrs;
        // maybe used
        res += fmt::format(
            FMT_COMPILE("\n{}{:<{}} {:<{}} `json:\"{}{}\"`"),
            Tab,
            pascalNames[i], maxNameLen,
            types[i], maxTypeLen,
            f.name, attrs);
    }
    return res;
}

void go::Format(FormatContext &ctx, const Writer &writer)
{
    assert(ctx.opts);
    GoOpts& opts = *static_cast<GoOpts*>(ctx.opts); //not used yet, will be for imports
    std::map<Namespace, vector<Type>> byNs;
    for (auto& t: ctx.ast.types) {
        if (is<Alias>(t) || is<Enum>(t)) {
            throw Err("Enums or aliases not supported yet");
        }
        if (auto* str = std::get_if<Struct>(&t->AsVariant())) {
            byNs[str->ns].push_back(t);
        }
    }
    for (auto& [ns, types]: byNs) {
        std::sort(types.begin(), types.end(), [](Type l, Type r){
            return l->Base()->name < r->Base()->name;
        });
        auto dotPos = ns.name.find_last_of('.');
        auto pkg = ns.name.substr(dotPos + 1);
        string file;
        file += fmt::format("package {}\n\n", pkg);
        for (auto t: types) {
            if (auto* str = std::get_if<Struct>(&t->AsVariant())) {
                file += fmt::format(
                    FMT_COMPILE("type {} struct {{{}\n}}\n\n"),
                    str->name, formatFields(*str));
            }
        }
        auto pref = ns.name;
        std::replace(pref.begin(), pref.end(), '.', '/');
        writer(fs::path(pref) / (pkg+".gen.go"), file);
    }
}
