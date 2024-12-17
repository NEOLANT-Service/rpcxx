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

#include <cassert>
#include <fmt/core.h>
#include <fmt/compile.h>
#include <argparse/argparse.hpp>
#include <fstream>
#include <map>
#include "codegen.hpp"
#include "codegen.lua.h"
#include "cppgen.hpp"
#include "gogen.hpp"
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

using namespace rpcxx;
using namespace gen;

template<typename Fn>
static void iterateTable(lua_State* L, Fn&& f) {
    if (lua_type(L, -1) != LUA_TTABLE) {
        throw Err("Table expected");
    }
    lua_pushnil(L);
    while(lua_next(L, -2)) {
        auto was = lua_gettop(L);
        bool contin = true;
        if constexpr (std::is_invocable_r_v<void, Fn>) {
            f();
        } else {
            contin = f();
        }
        auto now = lua_gettop(L);
        if (was != now) {
            fmt::println(stderr, "After calling an iterator function =>"
                      " stack is not of the same size (was: {} != now: {})",
                      was, now);
            std::abort();
        }
        if (!contin) {
            lua_pop(L, 2);
            return;
        }
        lua_pop(L, 1);
    }
}

template<typename Fn>
static void iterateTableConsume(lua_State* L, Fn&& f) {
    iterateTable(L, f);
    lua_pop(L, 1);
}

static string_view getSV(lua_State* L, int idx = -1) {
    size_t sz;
    if (lua_type(L, idx) != LUA_TSTRING) {
        throw Err("Expected string, got: {}", luaL_typename(L, idx));
    }
    auto msg = luaL_checklstring(L, idx, &sz);
    return {msg, sz};
}

static string_view popSV(lua_State* L) {
    auto res = getSV(L, -1);
    lua_pop(L, 1);
    return res;
}

static bool test(lua_State* L, const char* name) {
    lua_getfield(L, -1, name);
    bool result = false;
    if (!lua_isnil(L, -1) && lua_toboolean(L, -1)) {
        result = true;
    }
    lua_pop(L, 1);
    return result;
}

static Type resolveType(lua_State* L, string_view tname, FormatContext& ctx);

static Type resolveNext(lua_State* L, FormatContext& ctx) {
    lua_getfield(L, -1, "__next__");
    lua_getfield(L, -1, "__name__");
    auto name = popSV(L);
    auto found =  resolveType(L, name, ctx);
    lua_pop(L, 1);
    return found;
}

static Namespace extractNs(lua_State* L) {
    lua_getfield(L, -1, "__ns_depth__");
    lua_getfield(L, -2, "__ns__");
    lua_getfield(L, -3, "__source__");
    Namespace ns;
    if (!lua_isnil(L, -1) && !lua_isnil(L, -2) && !lua_isnil(L, -3)) {
        ns = Namespace{string{getSV(L, -1)}, string{getSV(L, -2)}, string{}, int(lua_tointeger(L, -3))};
    } else {
        throw Err("Invalid namespace received");
    }
    lua_pop(L, 3);
    return ns;
}

static Type registerIfNeeded(Type t, FormatContext& ctx) {
    ctx.ast.types.push_back(t);
    auto base = GetBase(t);
    if (!base) {
        throw Err("attempt to register builtin");
    }
    return ctx.ast.savedTypes[base->ns][base->name] = t;
}

static Type tryLookup(string_view name, Namespace& ns, FormatContext& ctx) {
    auto n = ctx.ast.savedTypes.find(ns);
    if (n == ctx.ast.savedTypes.end()) {
        return nullptr;
    }
    auto t = n->second.find(name);
    if (t == n->second.end()) {
        return nullptr;
    }
    return t->second;
}

static def::Value parseDefault(lua_State* L);

static def::Value parseArr(lua_State* L) {
    def::Array res;
    iterateTable(L, [&]{
        auto i = lua_tointeger(L, -2);
        if (i < 1) throw Err("Negative index in default-array");
        auto idx = size_t(i - 1);
        if (res.value.size() <= idx) {
            res.value.resize(idx + 1);
        }
        lua_pushvalue(L, -1);
        res.value[idx] = parseDefault(L);
    });
    size_t idx = 0;
    for (auto& v: res.value) {
        if (!v) {
            throw Err("Index in default-array #{} not populated", idx);
        }
        idx++;
    }
    return new def::Variant{std::move(res)};
}

static def::Value parseTable(lua_State* L) {
    def::Table res;
    iterateTable(L, [&]{
        auto key = getSV(L, -2);
        lua_pushvalue(L, -1);
        res.value[string{key}] = parseDefault(L);
    });
    return new def::Variant{std::move(res)};
}

static def::Value parseDefault(lua_State* L) {
    using namespace def;
    defer clean([&]{
        lua_pop(L, 1);
    });
    auto t = lua_type(L, -1);
    switch (t) {
    case LUA_TNIL: return new Variant{Nil{}};
    case LUA_TSTRING: return new Variant{String{getSV(L, -1)}};
    case LUA_TBOOLEAN: return new Variant{Bool{bool(lua_toboolean(L, -1))}};
    case LUA_TNUMBER: {
        if (lua_isinteger(L, -1)) {
            return new Variant{Int{lua_tointeger(L, -1)}};
        } else {
            return new Variant{Num{lua_tonumber(L, -1)}};
        }
    }
    case LUA_TTABLE: {
        auto allInts = true;
        iterateTable(L, [&]{
            if (!lua_isinteger(L, -2)) {
                allInts = false;
            }
            if (!allInts && lua_type(L, -2) != LUA_TSTRING) {
                throw Err("Non-string/int keys are not supported in tables");
            }
        });
        return allInts ? parseArr(L) : parseTable(L);
    }
    default:
        throw Err("Unsupported type for default: {}", lua_typename(L, t));
    }
    return nullptr;
}

static std::set<Attr> parseAttrs(lua_State* L) {
    std::set<Attr> res;
    if (lua_type(L, -1) != LUA_TTABLE) {
        lua_pop(L, 1); // pop table
        throw Err("Table expected as attributes list");
    }
    int idx = 0;
    while (lua_rawgeti(L, -1, ++idx) != LUA_TNIL) {
        if (lua_type(L, -1) != LUA_TSTRING) {
            throw Err("Only strings supported as attribute names");
        }
        res.insert(Attr{string{popSV(L)}});
    }
    lua_pop(L, 2); //pop table + nil
    return res;
}

static Type resolveType(lua_State* L, string_view tname, FormatContext& ctx) try {
    if (!test(L, "__is_type__")) {
        throw Err("Type expected: {}", tname);
    }
    if (auto it = ctx.ast.builtins.find(tname); it != ctx.ast.builtins.end()) {
        return it->second;
    }
    lua_getfield(L, -1, "__subtype__");
    auto sub = popSV(L);
    auto ns = extractNs(L);
    if (auto found = tryLookup(tname, ns, ctx)){
        return found;
    }
    if (sub == "builtin") {
        throw Err("Unhandled builtin type: {}", tname);
    } else if (sub == "alias") {
        Alias result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "arr") {
        Array result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "opt") {
        Optional result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "map") {
        Map result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "enum") {
        Enum result;
        result.ns = ns;
        result.name = tname;
        lua_getfield(L, -1, "__attrs__");
        result.attributes = parseAttrs(L);
        lua_getfield(L, -1, "__fields__");
        iterateTableConsume(L, [&]{
            Enum::Value curr;
            if (lua_type(L, -2) == LUA_TSTRING) {
                curr.name = string{getSV(L, -2)};
                if (!lua_isinteger(L, -1)) {
                    throw Err("Expected integer in enum {}: value: {}", tname, curr.name);
                }
                curr.number = lua_tointeger(L, -1);
            } else {
                curr.name = string{getSV(L, -1)};
            }
            result.values.push_back(std::move(curr));
        });
        std::sort(result.values.begin(), result.values.end(), [](auto& lhs, auto& rhs){
            return lhs.name < rhs.name;
        });
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "default") {
        WithDefault result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        lua_getfield(L, -1, "__value__");
        result.value = parseDefault(L);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "attrs") {
        WithAttrs result;
        result.ns = ns;
        result.name = tname;
        result.item = resolveNext(L, ctx);
        lua_getfield(L, -1, "__attrs__");
        result.attributes = parseAttrs(L);
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else if (sub == "struct") {
        Struct result;
        result.ns = ns;
        result.name = tname;
        lua_getfield(L, -1, "__attrs__");
        result.attributes = parseAttrs(L);
        lua_getfield(L, -1, "__fields__");
        iterateTableConsume(L, [&]{
            auto subname = getSV(L, -2);
            lua_getfield(L, -1, "__name__");
            auto subtname = popSV(L);
            Type found;
            try {
                found = resolveType(L, subtname, ctx);
            } catch (std::exception& e) {
                throw Err("{}\n =>\tWhile resolving for struct field: '{}'", e.what(), subname);
            }
            if (!found) {
                throw Err("Error resolving: {}.{}", tname, subname);
            }
            result.fields.push_back({string{subname}, found});
        });
        return registerIfNeeded(new TypeVariant{result}, ctx);
    } else {
        throw Err("{}: unknown subtype: {}", tname, sub);
    }
} catch (std::exception& e) {
    throw Err("{}\n =>\tWhile resolving type: '{}'", e.what(), tname);
}

static void doResolveNotify(Notify& result, lua_State* L, FormatContext& ctx) {
    auto ispack = test(L, "pack");
    auto isnamed = test(L, "named");
    lua_getfield(L, -1, "service");
    result.service = string{popSV(L)};
    lua_getfield(L, -1, "name");
    result.name = string{popSV(L)};
    auto* pack = ispack ? &result.params.emplace<ParamsPack>() : nullptr;
    auto* arr = !ispack && !isnamed ? &result.params.emplace<ParamsArray>() : nullptr;
    auto* named = !ispack && isnamed ? &result.params.emplace<ParamsNamed>() : nullptr;
    lua_getfield(L, -1, "params");
    iterateTableConsume(L, [&]{
        if (lua_type(L, -2) == LUA_TSTRING) {
            auto key = getSV(L, -2);
            if (key.substr(0, 2) == "__") {
                return true;
            }
        }
        if (lua_type(L, -1) != LUA_TTABLE) {
            return true;
        }
        lua_getfield(L, -1, "__name__");
        auto pname = popSV(L);
        auto par = resolveType(L, pname, ctx);
        if (pack) {
            pack->item = par;
        } else if (arr) {
            auto idx = unsigned(lua_tointeger(L, -2) - 1);
            if (arr->size() <= idx) {
                arr->resize(idx + 1);
            }
            (*arr)[idx] = par;
        } else {
            assert(named);
            auto key = getSV(L, -2);
            (*named)[string{key}] = par;
        }
        return true;
    });
}

static void resolveMethod(lua_State* L, FormatContext& ctx) {
    Method& result = ctx.ast.methods.emplace_back();
    doResolveNotify(result, L, ctx);
    result.async = test(L, "async");
    lua_getfield(L, -1, "returns");
    lua_getfield(L, -1, "__name__");
    auto retname = popSV(L);
    result.returns = resolveType(L, retname, ctx);
    lua_pop(L, 1);
}

static void resolveNotify(lua_State* L, FormatContext& ctx) {
    Notify& result = ctx.ast.notify.emplace_back();
    doResolveNotify(result, L, ctx);
}

static int msghandler(lua_State* L) {
    luaL_Buffer b;
    lua_Debug ar;
    luaL_buffinit(L, &b);
    auto msg = getSV(L, 1);
    msg = msg.substr(msg.find(": ") + 2);
    luaL_addlstring(&b, msg.data(), msg.size());
    int level = 1;
    while (lua_getstack(L, level++, &ar)) {
        lua_getinfo(L, "Sln", &ar);
        // if (strcmp(ar.source, "<frontend>") == 0) {
        //     continue;
        // }
        if (ar.currentline <= 0) {
            continue;
        } else {
            lua_pushfstring(L, "\n =>\t%s:%d", ar.short_src, ar.currentline);
            luaL_addvalue(&b);
        }
    }
    luaL_pushresult(&b);
    return 1;
}

static void parseOneNamespace(lua_State* L, FormatContext& ctx) {
    lua_getfield(L, -1, "types");
    iterateTableConsume(L, [&]{
        resolveType(L, getSV(L, -2), ctx);
    });
    lua_getfield(L, -1, "methods");
    iterateTableConsume(L, [&]{
        resolveMethod(L, ctx);
    });
    lua_getfield(L, -1, "notify");
    iterateTableConsume(L, [&]{
        resolveNotify(L, ctx);
    });
}

template<auto f>
int Protected(lua_State* L) noexcept {
    try {
        return f(L);
    } catch (std::exception& e) {
        lua_pushstring(L, e.what());
    }
    lua_error(L);
    std::abort();
}

static int resolve_inc(lua_State* L) {
    auto was = fs::path{getSV(L, 1)};
    auto wanted = fs::path{getSV(L, 2)};
    if (wanted.is_absolute()) {
        lua_pushvalue(L, 2);
        return 1;
    }
    auto rel = was.parent_path()/wanted;
    if (fs::exists(rel)) {
        lua_pushstring(L, rel.string().c_str());
        return 1;
    }
    throw Err("Could not include: {} => {} does not exist",
              wanted.string(), rel.string());
}

static void initEnv(lua_State* L, FormatContext& ctx) {
    lua_pushlstring(L, ctx.params.main.sourceFile.data(), ctx.params.main.sourceFile.size());
    lua_setglobal(L, "__current_file__");
    lua_register(L, "__resolve_inc__", Protected<resolve_inc>);
    if (!lua_checkstack(L, 300)) {
        throw Err("Could not reserve lua stack");
    }
}

void gen::PopulateFromFrontend(lua_State* L, FormatContext& ctx) {
    lua_pushcfunction(L, msghandler);
    auto msgh = lua_gettop(L);
    luaL_openlibs(L);
    initEnv(L, ctx);
    if (luaL_loadbufferx(L, codegen_script, strlen(codegen_script), "<frontend>", "t") != LUA_OK) {
        throw Err("Could not load init-script: {}", getSV(L));
    }
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        throw Err("Error running init-script: {}", getSV(L));
    }
    int all_ns = luaL_ref(L, LUA_REGISTRYINDEX);
    auto spec = ctx.prog.get("spec");
    if (luaL_loadfile(L, spec.c_str()) != LUA_OK) {
        throw Err("Could not load spec file: {} => {}", spec, getSV(L));
    }
    if (lua_pcall(L, 0, 0, msgh) != LUA_OK) {
        throw Err("{}", getSV(L));
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, all_ns);
    lua_getfield(L, -1, "__root__");
    parseOneNamespace(L, ctx);
    lua_getfield(L, -1, "ns");
    auto res = string{getSV(L)};
    lua_pop(L, 2);
    ctx.params.main.name = res;
}
