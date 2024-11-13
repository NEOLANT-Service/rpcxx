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

#include "cpp/clientgen.hpp"
#include "cpp/servergen.hpp"
#include "cpp/typegen.hpp"
#include "fmt/compile.h"
#include <cctype>

namespace rpcxx::gen::cpp
{

static string builtin(Builtin builtin)
{
    using namespace std::string_literals;
    switch (builtin.kind) {
    case Builtin::Json: return "rpcxx::Json"s;
    case Builtin::Json_View: return "rpcxx::JsonView"s;
    case Builtin::Bool: return "bool"s;
    case Builtin::Int8: return "int8_t"s;
    case Builtin::Uint8: return "uint8_t"s;
    case Builtin::Int16: return "int16_t"s;
    case Builtin::Uint16: return "uint16_t"s;
    case Builtin::Int32: return "int32_t"s;
    case Builtin::Uint32: return "uint32_t"s;
    case Builtin::Int64: return "int64_t"s;
    case Builtin::Uint64: return "uint64_t"s;
    case Builtin::Float: return "float"s;
    case Builtin::Double: return "double"s;
    case Builtin::String: return "std::string"s;
    case Builtin::String_View: return "std::string_view"s;
    case Builtin::Binary: return "rpcxx::Binary"s;
    case Builtin::Binary_View: return "rpcxx::BinaryView"s;
    case Builtin::Void: return "void"s;
    default: throw Err("NonBuiltin passed into Builtin Handler");
    }
}

string PrintType(Type t) {
    return Visit(t->AsVariant(),
        [](Builtin b) -> string {
            return builtin(b);
        },
        [](Optional& o) -> string {
            return fmt::format(FMT_COMPILE("std::optional<{}>"), PrintType(o.item));
        },
        [](Map& m) -> string {
            return fmt::format(FMT_COMPILE("rpcxx::Map<{}>"), PrintType(m.item));
        },
        [](Array& a) -> string {
            return fmt::format(FMT_COMPILE("std::vector<{}>"), PrintType(a.item));
        },
        [](WithDefault& a) -> string {
            return PrintType(a.item);
        },
        [](auto& v) -> string {
            return fmt::format(FMT_COMPILE("{}::{}"), ToNamespace(v.ns.name), v.name);
        }
        );
}

constexpr auto common_template = FMT_COMPILE(R"EOF(// THIS FILE IS GENERATED. Do not modify. Generated from: {source_file}
{guard_start}
{extra_includes}
#include <rpcxx/rpcxx.hpp>
{type_gen}
{namespace_start}
{server_gen}
{client_gen}

{server_gen_source}
{client_gen_source}
{namespace_end}
{guard_end}
)EOF");

Guard MakeGuard(string_view file, string_view ns, string_view part) {
    auto fns = ToNamespace(file, "_");
    auto nsns = ToNamespace(ns, "_");
    return {
        fmt::format(FMT_COMPILE("#ifndef _{0}_G_{1}_P_{2}\n#define _{0}_G_{1}_P_{2}"), nsns, fns, part),
        fmt::format(FMT_COMPILE("#endif //_{}_G_{}_P_{}"), nsns, fns, part)
    };
}

Guard MakeGuard(Namespace const& ns) {
    return MakeGuard(ns.sourceFile, ns.name, ns.part);
}

string ToNamespace(string_view file, string sep) {
    string result;
    for (auto ch: file) {
        if (!std::isalnum(ch) && ch != '_') {
            result += sep;
        } else {
            result += ch;
        }
    }
    return result;
}

static bool isTrivial(Type t) {
    return Visit(
        t->AsVariant(),
        [](Builtin){
            return true;
        },
        [](Enum&){
            return true;
        },
        [](Alias& a) {
            return isTrivial(a.item);
        },
        [](Optional& o) {
            return isTrivial(o.item);
        },
        [](auto&){
            return false;
        }
        );
}

static string doFormat(Params const& params, bool needCref) {
    auto ifCref = [&](Type t) {
        return needCref && !isTrivial(t) ? " const&" : "";
    };
    return Visit(params,
        [&](ParamsNamed const& named) -> string {
            string result;
            int idx = 0;
            for (auto& [k, v]: named) {
                result += fmt::format(FMT_COMPILE("{}{}{} {}"), idx ? ", " : "", PrintType(v), ifCref(v), k);
                idx++;
            }
            return result;
        },
        [&](ParamsArray const& arr) -> string  {
            string result;
            int idx = 0;
            for (auto& v: arr) {
                result += fmt::format(FMT_COMPILE("{}{}{} arg{}"), idx ? ", " : "", PrintType(v), ifCref(v), idx);
                idx++;
            }
            return result;
        },
        [&](ParamsPack const& pack) -> string  {
            return fmt::format(FMT_COMPILE("{}{} args"),
                               PrintType(pack.item),
                               ifCref(pack.item));
        }
        );
}

string server::FormatSignature(Params const& params) {
    return doFormat(params, false);
}

string client::FormatSignature(Params const& params) {
    return doFormat(params, true);
}

string Format(FormatContext& ctx)
{
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    auto mainns = ToNamespace(ctx.params.main.name);
    string extraIncludes = "\n";
    for (auto& ns: ctx.params.extraIncludes) {
        extraIncludes += fmt::format(FMT_COMPILE("#include \"{}\"\n"), ns);
    }
    bool genNamespace = !mainns.empty();
    string gstart, gend;
    if (ctx.params.targets != Targets::TargetTypes) {
        auto gs = MakeGuard(ctx.params.main);
        gstart = std::move(gs.begin);
        gend = std::move(gs.end);
    } else {
        genNamespace = false;
    }
    auto types = types::Format(ctx);
    opts.sourceFile = false;
    auto clientHeader = client::Format(ctx);
    auto serverHeader = server::Format(ctx);
    opts.sourceFile = true;
    auto clientSrc = client::Format(ctx);
    auto serverSrc = server::Format(ctx);
    return fmt::format(
        common_template,
        fmt::arg("extra_includes", extraIncludes),
        fmt::arg("type_gen", types),
        fmt::arg("client_gen", clientHeader),
        fmt::arg("server_gen", serverHeader),
        fmt::arg("guard_start", gstart),
        fmt::arg("namespace_start", genNamespace ? fmt::format("namespace {} \n{{", mainns) : ""),
        fmt::arg("namespace_end", genNamespace ? fmt::format("}} //namespace {}", mainns) : ""),
        fmt::arg("client_gen_source", clientSrc),
        fmt::arg("server_gen_source", serverSrc),
        fmt::arg("guard_end", "\n"+gend),
        fmt::arg("source_file", ctx.prog.get("spec"))
        );
}

}
