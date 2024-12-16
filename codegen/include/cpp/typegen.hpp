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

#include "cppgen.hpp"
#include <cassert>
#include <fmt/ranges.h>

namespace rpcxx::gen::cpp::types {

constexpr auto struct_fmt = FMT_COMPILE(R"EOF(
struct {type_name} {{{fields}
}};
DESCRIBE("{ns}::{type_name}", {type_name}{cls_attrs}) {{{field_names}
}}
)EOF");

constexpr auto enum_fmt = FMT_COMPILE(R"EOF(
enum class {type_name} {{{fields}
}};
DESCRIBE("{ns}::{type_name}", {type_name}{cls_attrs}) {{{field_names}
}}
)EOF");

constexpr auto field = FMT_COMPILE(R"EOF(
    {type} {name} = {{{default}}};)EOF");

constexpr auto alias = FMT_COMPILE(R"EOF(
using {type_name} = {aliased_type};
)EOF");

static string_view rawName(Type t) {
    return Visit(t->AsVariant(), [](Builtin) -> string_view{
            return {};
        }, [](auto const& other)-> string_view {
            return other.name;
        });
}

static string defaultFromTrivial(def::Value v) {
    return Visit(
        v->AsVariant(),
        [](def::String& s) -> string {
            return '"' + string{s.value} + '"';
        },
        [](def::Int& s) -> string {
            return std::to_string(s.value);
        },
        [](def::Num& s) -> string {
            return std::to_string(s.value);
        },
        [](def::Bool& s) -> string {
            return s.value ? "true" : "false";
        },
        [](auto&) -> string {
            return {};
        }
        );
}

static string defaultFromRaw(Type t, def::Value v);

static string defaultFromArray(Array& t, def::Value adef) {
    vector<string> items;
    auto src = as<def::Array>(adef);
    if (!src) throw Err("Table expected for default value");
    for (auto& v: src->value) {
        items.push_back(defaultFromRaw(t.item, v));
    }
    return fmt::format("{}", fmt::join(items, ", "));
}

static string defaultFromTable(Map& t, def::Value adef) {
    string result;
    auto src = as<def::Table>(adef);
    if (!src) throw Err("Table expected for default value");
    for (auto& [k, v]: src->value) {
        result += "{";
        result += k;
        result += ", ";
        result += defaultFromRaw(t.item, v);
        result += "}";
        result += ", ";
    }
    return result;
}

static string defaultFromEnum(Enum& e, def::Value adef) {
    auto src = as<def::String>(adef);
    if (!src) throw Err("String expected for default value");
    auto type = TypeVariant{e}; //micro - kostyl
    return PrintType(&type) + "::" + string{src->value};
}

static string defaultFromStruct(Struct& t, def::Value adef) {
    string result;
    auto src = as<def::Table>(adef);
    if (!src) throw Err("Table expected for default value");
    vector<std::pair<string, string>> unordered;
    for (auto& pair: src->value) {
        auto nested = std::find_if(t.fields.begin(), t.fields.end(), [&](Struct::Field& f){
            return f.name == pair.first;
        });
        if (nested == t.fields.end()) {
            throw Err("Could not find field ({}) for default value in struct", pair.first);
        }
        string nestedDef = defaultFromRaw(nested->type, pair.second);
        unordered.push_back({string{pair.first}, std::move(nestedDef)});
    }
    for (auto& f: t.fields) {
        auto toUse = std::find_if(unordered.begin(), unordered.end(), [&](auto& pair){
            return f.name == pair.first;
        });
        if (toUse != unordered.end()) {
            result += PrintType(f.type) + "{" + toUse->second + "},";
        } else {
            result += "{},";
        }
    }
    return result;
}

static string defaultFromRaw(Type t, def::Value adef) try {
    return Visit(
        t->AsVariant(),
        [&](Map& v) -> string {
            return defaultFromTable(v, adef);
        },
        [&](Array& v) -> string {
            return defaultFromArray(v, adef);
        },
        [&](Struct& v) -> string {
            return defaultFromStruct(v, adef);
        },
        [&](Enum& v) -> string {
            return defaultFromEnum(v, adef);
        },
        [&](auto&) -> string {
            return defaultFromTrivial(adef);
        }
        );
} catch (std::exception& e) {
    throw Err("{}\n =>\tGetting default for '{}'", e.what(), PrintType(t));
}

static string getDefault(Type t) {
    return Visit(t->AsVariant(),
        [&](WithDefault& d) -> string {
            return defaultFromRaw(d.item, d.value);
        },
        [&](Optional& o) -> string {
            return getDefault(o.item);
        },
        [&](Alias& a) -> string {
            return getDefault(a.item);
        },
        [](auto&) -> string {
            return {};
        });
}

static string formatSingleType(FormatContext& ctx, Type t)
{
    return Visit(t->AsVariant(),
        [&](const Alias& a) {
            return fmt::format(
                alias,
                fmt::arg("type_name", a.name),
                fmt::arg("aliased_type", PrintType(a.item))
                );
        },
        [&](const Enum& a) {
            string fields;
            string field_names;
            unsigned count = 0;
            for (auto& v: a.values) {
                if (auto& n = v.number) {
                    fields += fmt::format(FMT_COMPILE("\n    {} = {},"), v.name, *n);
                } else {
                    fields += fmt::format(FMT_COMPILE("\n    {},"), v.name);
                }
                field_names += fmt::format(
                    FMT_COMPILE("\n    MEMBER(\"{0}\", _::{0}{1});"),
                    v.name, "");
            }
            return fmt::format(
                enum_fmt,
                fmt::arg("ns", ToNamespace(a.ns.name)),
                fmt::arg("type_name", rawName(t)),
                fmt::arg("fields", fields),
                fmt::arg("cls_attrs", ""),
                fmt::arg("field_names", field_names)
                );
        },
        [&](Struct const& s) {
            string fields;
            string field_names;
            unsigned count = 0;
            for (auto& it: s.fields) {
                auto& subName = it.name;
                auto& subType = it.type;
                fields += fmt::format(
                    field,
                    fmt::arg("type", PrintType(subType)),
                    fmt::arg("name", subName),
                    fmt::arg("default", getDefault(subType))
                    );
                field_names += fmt::format(
                    FMT_COMPILE("\n    MEMBER(\"{0}\", &_::{0}{1});"),
                    subName, "");
            }
            return fmt::format(
                struct_fmt,
                fmt::arg("ns", ToNamespace(s.ns.name)),
                fmt::arg("type_name", rawName(t)),
                fmt::arg("fields", fields),
                fmt::arg("cls_attrs", ""),
                fmt::arg("field_names", field_names)
                );
        },
        [](const auto&) -> string {
            throw Err("Unformattable type passed to cpp formatting");
        });
}

using DependecyDepth = uint32_t;

static DependecyDepth CalcDepth(Type t) {
    return Visit(t->AsVariant(),
        [](Struct s){
            uint32_t max = 1;
            for (auto& f: s.fields) {
                if (auto curr = CalcDepth(f.type) + 1; curr > max) {
                    max = curr;
                }
            }
            return max;
        },
        [](Builtin) -> uint32_t{
            return 0;
        },
        [](Enum) -> uint32_t{
            return 0;
        },
        [](auto&& other) {
            return CalcDepth(other.item) + 1;
        });
}

using DepPair = std::pair<DependecyDepth, Type>;

static bool CompareDeps(const DepPair& lhs, const DepPair& rhs) {
    auto lns = GetNs(lhs.second);
    auto rns = GetNs(rhs.second);
    return std::tie(lhs.first, *lns) < std::tie(rhs.first, *rns);
}

static size_t getSizeof(Builtin b) {
    switch (b.kind) {
    case Builtin::Bool: return sizeof(bool);
    case Builtin::Int8:
    case Builtin::Uint8: return sizeof(int8_t);
    case Builtin::Int16:
    case Builtin::Uint16: return sizeof(int16_t);
    case Builtin::Int32:
    case Builtin::Uint32: return sizeof(int32_t);
    case Builtin::Int64:
    case Builtin::Uint64: return sizeof(int64_t);
    case Builtin::Float: return sizeof(float);
    case Builtin::Double: return sizeof(double);
    case Builtin::Binary: return sizeof(float);
    case Builtin::String: return sizeof(std::string);
    case Builtin::String_View: return sizeof(std::string_view);
    case Builtin::Json: return sizeof(std::string_view) * 2;
    case Builtin::Json_View: return sizeof(std::string_view);
    default: return sizeof(void*);
    }
}

static size_t getSizeof(Type t) {
    return Visit(t->AsVariant(),
        [](Builtin b){
            return getSizeof(b);
        },
        [](Enum&){
            return sizeof(unsigned);
        },
        [](Struct& s){
            size_t sz = 0;
            for (auto& f: s.fields) {
                if (!f.sz) {
                    f.sz = getSizeof(f.type);
                }
                sz += f.sz;
            }
            return sz;
        },
        [](Alias const& a){
            return getSizeof(a.item);
        },
        [](WithDefault const& d){
            return getSizeof(d.item);
        },
        [](WithAttrs const& d){
            return getSizeof(d.item);
        },
        [](Array const&){
            return sizeof(vector<int>);
        },
        [](Map const&){
            return sizeof(std::map<string, int>);
        },
        [](Optional const& o){
            return getSizeof(o.item);
        });
}

static void reorderMembers(Struct& t) {
    for (auto& f: t.fields) {
        f.sz = getSizeof(f.type);
        if (auto s = std::get_if<Struct>(f.type)) {
            reorderMembers(*s);
        }
    }
    std::sort(t.fields.begin(), t.fields.end(), [](Struct::Field& lhs, Struct::Field& rhs){
        return std::make_tuple(-lhs.sz, std::cref(lhs.name)) < std::make_tuple(-rhs.sz, std::cref(rhs.name));
    });
}

std::string Format(FormatContext& ctx)
{
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    if (opts.sourceFile) return "";
    if (!(ctx.params.targets & TargetTypes))
        return "";
    std::vector<DepPair> byDepth;
    for (auto& t: ctx.ast.types) {
        auto* asStruct = std::get_if<Struct>(&t->AsVariant());
        if (asStruct) {
            reorderMembers(*asStruct);
        }
        if (asStruct || is<Alias>(t) || is<Enum>(t)) {
            byDepth.push_back({CalcDepth(t), t});
        }
    }
    std::sort(byDepth.begin(), byDepth.end(), CompareDeps);
    string result;
    Namespace lastns;
    string guardEnd;
    auto end_ns = [&]{
        if (lastns.name.empty()) {
            return;
        }
        result += "} //namespace " + ToNamespace(lastns.name) + "\n";
        result += guardEnd;
        result += "\n";
    };
    auto start_ns = [&](Namespace const& ns) {
        if (lastns != ns) {
            end_ns();
            auto [gstart, gend] = cpp::MakeGuard(ns);
            result += gstart;
            result += fmt::format("\nnamespace {} {{", ToNamespace(ns.name));
            lastns = ns;
            guardEnd = std::move(gend);
        }
    };
    for (auto& [_, type]: byDepth) {
        try {
            auto base = GetBase(type);
            if (!base) {
                throw Err("Invalid type passed");
            }
            auto curr = base->ns;
            curr.part = base->name;
            start_ns(curr);
            result += formatSingleType(ctx, type);
        } catch (std::exception& exc) {
            throw Err("{}\n =>\tGenerating code for type '{}'", exc.what(), PrintType(type));
        }
    }
    end_ns();
    return result;
}

}
