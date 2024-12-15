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

#ifndef GEN_COMMON_HPP
#define GEN_COMMON_HPP

#include <argparse/argparse.hpp>
#include <fmt/core.h>
#include <fmt/args.h>
#include <fmt/compile.h>
#include <string>
#include <string_view>
#include <filesystem>
#include <variant>

struct lua_State;

namespace rpcxx::gen {

using std::vector;
using std::string_view;
using std::string;

template<typename...Fs> struct overloaded : Fs... {using Fs::operator()...;};
template<typename...Fs> overloaded(Fs...) -> overloaded<Fs...>;

template<typename Var, typename...Fs>
decltype(auto) Visit(Var&& v, Fs&&...fs) {
    return std::visit(overloaded<Fs...>{std::forward<Fs>(fs)...}, std::forward<Var>(v));
}

template<typename Fn> struct [[nodiscard]] defer {
    defer(Fn&& f) : f(std::forward<Fn>(f)) {}
    ~defer() {f();}
    Fn f;
};

template<typename Fmt, typename...Args>
std::runtime_error Err(Fmt&& fmt, Args&&...args) {
    return std::runtime_error(fmt::format(std::forward<Fmt>(fmt), std::forward<Args>(args)...));
}

constexpr auto OneTab = "    ";
namespace fs = std::filesystem;

enum Targets {
    TargetNone      = 0,
    TargetTypes     = 1 << 0,
    TargetClient    = 1 << 1,
    TargetServer    = 1 << 2,
    TargetAll     = TargetTypes | TargetClient | TargetServer,
};

enum class Lang {
    cpp, go
};

struct Namespace {
    string sourceFile;
    string name; //"." as separators
    string part = {};
    int depth = 0;

    constexpr auto rank() const noexcept {
        return std::tie(sourceFile, name, part, depth);
    }
    constexpr bool operator<(const Namespace& o) const noexcept {
        return rank() < o.rank();
    }
    constexpr bool operator==(const Namespace& o) const noexcept {
        return rank() == o.rank();
    }
    constexpr bool operator!=(const Namespace& o) const noexcept {
        return rank() != o.rank();
    }
};

struct TypeBase
{
    Namespace ns = {};
    string name;
};

struct TypeVariant;

using Type = TypeVariant*;

using TypeMap = std::map<string, Type, std::less<>>;

struct Builtin {
    enum Kind : short {
        Invalid = 0, Bool, Int8, Uint8, Int16, Uint16,
        Int32, Uint32, Int64, Uint64, Float, Double, String,
        String_View, Binary, Binary_View, Json, Json_View, Void,
    };
    Kind kind;
};

struct Enum : TypeBase {
    struct Value {
        string name;
        std::optional<int64_t> number;
    };
    vector<Value> values;
};

struct Struct : TypeBase {
    struct Field {
        string name;
        Type type = nullptr;
        size_t sz = 0;
    };
    vector<Field> fields;
};

struct Array : TypeBase {
    Type item;
};

struct Alias : TypeBase {
    Type item;
};

struct Map : TypeBase {
    Type item;
};

struct Optional : TypeBase {
    Type item;
};

namespace def {

struct Variant;
using Value = Variant*;

struct Nil {};

struct Int {
    long long value;
};

struct Num {
    double value;
};

struct String {
    string_view value;
};

struct Bool {
    bool value;
};

struct Table {
    std::map<string, Value> value;
};

struct Array {
    std::vector<Value> value;
};

struct Variant : public std::variant<Nil, Int, Num, String, Bool, Table, Array> {
    using variant::variant;
    variant& AsVariant() noexcept {
        return *this;
    }
    const variant& AsVariant() const noexcept {
        return *this;
    }
};

}

struct WithDefault : TypeBase {
    Type item;
    def::Value value;
};

struct TypeVariant : public std::variant<Builtin, Enum, Struct, Array, Map, Optional, Alias, WithDefault>
{
    using variant::variant;
    variant& AsVariant() noexcept {
        return *this;
    }
    const variant& AsVariant() const noexcept {
        return *this;
    }
    TypeBase* Base() noexcept {
        return Visit(
            AsVariant(),
            [](Builtin) -> TypeBase* {
                return nullptr;
            }, [](TypeBase& b){
                return &b;
            });
    }
};

template<typename T, typename U>
bool is(U* t) {
    return std::holds_alternative<T>(*t);
}

template<typename T, typename U>
T* as(U* t) {
    return std::get_if<T>(t);
}

struct ParamsArray : vector<Type> {
    using vector::vector;
};

struct ParamsNamed : TypeMap {
    using TypeMap::TypeMap;
};

struct ParamsPack {
    Type item;
    size_t size() const {return 1;}
};

using Params = std::variant<ParamsArray, ParamsNamed, ParamsPack>;

struct Notify
{
    string service;
    string name;
    Params params;
};

struct Method : Notify
{
    Type returns;
    uint32_t timeout = 10000;
    bool async = false;
};

struct AST
{
    std::map<string_view, Type, std::less<>> builtins;
    std::map<Namespace, std::map<string, Type, std::less<>>> savedTypes;
    vector<Type> types;
    vector<Notify> notify;
    vector<Method> methods;
};

struct GenParams
{
    Lang lang = Lang::cpp;
    Targets targets = TargetAll;
    vector<string> extraIncludes;
    Namespace main;
    bool describeServer = false;
};

struct GoOpts {
    string pkgPrefix;
};

struct CppOpts {
    bool sourceFile = false;
};

struct FormatContext {
    GenParams& params;
    AST& ast;
    argparse::ArgumentParser& prog;
    void* opts = nullptr;
};

inline TypeBase* GetBase(Type t) {
    return Visit(t->AsVariant(), [](Builtin) -> TypeBase* {
            return nullptr;
        }, [](auto& other) -> TypeBase* {
            return &other;
        });
}

inline Namespace* GetNs(Type t) {
    return Visit(t->AsVariant(), [](Builtin) -> Namespace* {
            return nullptr;
        }, [](auto& other)-> Namespace* {
            return &other.ns;
        });
}

void PopulateFromFrontend(lua_State* L, FormatContext& ctx);

} //gen

#endif //GEN_COMMON_HPP
