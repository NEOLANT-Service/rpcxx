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

template<Builtin::Kind val>
Type makeBuiltin(AST& ast, string_view name) {
    static TypeVariant v{Builtin{val}};
    return ast.builtins[name] = &v;
};

static void populateBuiltins(AST& ast) {
    ast.builtins["nothing"] = ast.builtins["noreturn"] = makeBuiltin<Builtin::Void>(ast, "void");
    makeBuiltin<Builtin::Binary>(ast, "binary");
    makeBuiltin<Builtin::Binary_View>(ast, "binary_view");
    makeBuiltin<Builtin::Json>(ast, "json");
    makeBuiltin<Builtin::Json_View>(ast, "json_view");
    ast.builtins["bool"] = makeBuiltin<Builtin::Bool>(ast, "boolean");
    makeBuiltin<Builtin::String_View>(ast, "string_view");
    ast.builtins["str"] = makeBuiltin<Builtin::String>(ast, "string");
    ast.builtins["u64"] = makeBuiltin<Builtin::Uint64>(ast, "uint64");
    ast.builtins["uint"] = ast.builtins["u32"] = makeBuiltin<Builtin::Uint32>(ast, "uint32");
    ast.builtins["u16"] = makeBuiltin<Builtin::Uint16>(ast, "uint16");
    ast.builtins["u8"] = makeBuiltin<Builtin::Uint8>(ast, "uint8");
    ast.builtins["i64"] = makeBuiltin<Builtin::Int64>(ast, "int64");
    ast.builtins["int"] = ast.builtins["i32"] = makeBuiltin<Builtin::Int32>(ast, "int32");
    ast.builtins["i16"] = makeBuiltin<Builtin::Int16>(ast, "int16");
    ast.builtins["i8"] = makeBuiltin<Builtin::Int8>(ast, "int8");
    ast.builtins["f64"] = ast.builtins["number"] = makeBuiltin<Builtin::Double>(ast, "double");
    ast.builtins["f32"] = makeBuiltin<Builtin::Float>(ast, "float");
}


static void parseCli(argparse::ArgumentParser& cli, GenParams& params, int argc, char* argv[]) {
    cli.add_argument("spec")
    .required()
        .help("target <spec>.json file");
    cli.add_argument("--name", "-n")
        .default_value("")
        .help("name of the main file. default: [stem of spec file]");
    cli.add_argument("--output-dir", "-d")
        .default_value(".")
        .help("output generated files to");
    cli.add_argument("--describe-server")
        .implicit_value(true)
        .default_value(false)
        .help("generate DESCRIBE() for server methods");
    cli.add_argument("--marker", "-m")
        .help("marker file, that gets touched on generation (to be used in build systems)");
    cli.add_argument("--no-client")
        .implicit_value(true)
        .default_value(false)
        .help("omit client-related codegen");
    cli.add_argument("--no-server")
        .implicit_value(true)
        .default_value(false)
        .help("omit server-related codegen");
    cli.add_argument("--opt", "-o")
        .append()
        .default_value<std::vector<std::string>>({})
        .help("lang-specific options");
    cli.add_argument("--lang")
        .default_value("cpp")
        .action([&](auto lang){
            if (lang == "cpp") {
                params.lang = Lang::cpp;
            } else if(lang == "go") {
                params.lang = Lang::go;
            } else {
                throw Err("Invalid lang param: {}", lang);
            }
        })
        .help("target language");
    cli.add_argument("--stdout")
        .help("print result to stdout instead of a file")
        .default_value(false)
        .implicit_value(true);
    cli.add_description(
        "Generates a client and server stub headers based on lua based DSL spec.\n"
        "Currently supported languages: [cpp]"
        );
    try {
        cli.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        fmt::println(stderr, "Error: {}", err.what());
        std::cerr << cli;
        std::exit(1);
    }
}

//! Most of memory in this program intentionally leaks
/// We just alloc on heap
/// + disable Lua GC to allow direct refs into VM`s string_views
int main(int argc, char *argv[]) try
{
    lua_State* L = luaL_newstate();
    lua_gc(L, LUA_GCSTOP); //we do not use gc
    defer close{[L]{
        lua_close(L);
    }};
    argparse::ArgumentParser cli("rpccxx-codegen");
    GenParams params;
    parseCli(cli, params, argc, argv);
    fs::path specfile = cli.get("spec");
    std::filesystem::current_path(specfile.parent_path());
    params.main.sourceFile = string{specfile.string()};
    AST ast;
    populateBuiltins(ast);
    FormatContext ctx{params, ast, cli};
    PopulateFromFrontend(L, ctx);
    params.describeServer = cli["describe-server"] == true;
    auto mainOut = cli.get("name");
    if (mainOut.empty()) {
        mainOut = specfile.stem().string();
    }
    if (cli["no-client"] == true) {
        params.targets = Targets(params.targets & ~TargetClient);
    }
    if (cli["no-server"] == true) {
        params.targets = Targets(params.targets & ~TargetServer);
    }
    fs::path dir = cli.get("d");
    auto comment = [&]{
        switch (params.lang) {
        case Lang::go:
        case Lang::cpp: return "//";
        }
        return "";
    }();
    auto isStdOut = cli["--stdout"] == true;
    auto writeOutput = [&](fs::path file, string_view res){
        file = dir/std::move(file);
        if (isStdOut) {
            fmt::print(stdout, "{} ===> {}\n{}\n\n", comment, file.string(), res);
        } else {
            if (std::filesystem::is_directory(file)) {
                throw Err("Output file '{}' is a directory", file.string());
            }
            auto fileDir = file.parent_path();
            if (!fs::exists(fileDir)){
                if (!fs::create_directories(fileDir)) {
                    throw Err("Could not create directories for: {}", fileDir.string());
                }
            }
            std::ofstream out(file, std::ofstream::trunc);
            if (!out.is_open()) {
                throw Err("Could not open: {}", file.string());
            }
            out << res;
        }
    };
    auto langOpts = cli.get<std::vector<std::string>>("opt");
    auto iterOpts = [&](auto fn){
        for (string_view o: langOpts) {
            auto pos = o.find_first_of('=');
            if (pos == string_view::npos || pos == o.size()) {
                throw Err("incorrect 'opt' format: expected key=val");
            }
            fn(o.substr(0, pos), o.substr(pos + 1));
        }
    };
    if (params.lang == Lang::cpp) {
        CppOpts cppOpts;
        auto was = ctx.params.targets;
        ctx.opts = &cppOpts;
        ctx.params.targets = Targets(was & TargetTypes);
        writeOutput(mainOut+".private.hpp", cpp::Format(ctx));
        ctx.params.targets = Targets(was & ~TargetTypes);
        ctx.params.extraIncludes = {mainOut+".private.hpp"};
        for (auto& f: ctx.ast.attrs) {
            ctx.params.extraIncludes.push_back(f+".hpp");
        }
        writeOutput(mainOut+".hpp", cpp::Format(ctx));
    } else if (params.lang == Lang::go) {
        GoOpts opts;
        iterOpts([&](string_view key, string_view val){
            if (key == "pkg_prefix") {
                opts.pkgPrefix = string{val};
            }
        });
        ctx.opts = &opts;
        go::Format(ctx, writeOutput);
    } else {
        throw Err("unsupported lang");
    }
    if (auto marker = cli.present("marker")) {
        auto m = std::filesystem::path(*marker);
        std::filesystem::create_directories(m.parent_path());
        std::ofstream(m, std::ios_base::trunc | std::ios_base::out).write("1", 1);
    }
} catch (std::exception& exc) {
    fmt::println(stderr, "Exception: Traceback: \n =>\t{}", exc.what());
    return -1;
}
