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
#include <set>

namespace rpcxx::gen::cpp::server
{

constexpr auto format_inline = R"EOF(
struct {server_name} : public rpcxx::Server
{{
    template<int=0>
    {server_name}();
    {server_name}* as_{server_name}() noexcept {{return this;}}
{methods}
}};{describe}
)EOF";

constexpr auto format_source = R"EOF(
template<int>
inline {server_name}::{server_name}() : Server() {{{register_methods}
}}
)EOF";

constexpr auto method_fmt = FMT_COMPILE(R"EOF(
    virtual {return_type} {method_name}({args_with_types}) = 0;)EOF");

constexpr auto register_method = FMT_COMPILE(R"EOF(
    {method_or_notify}<&{server_name}::{method_name}>("{method_name}"{names_mapping});)EOF");

static string generateNamesMap(const ParamsNamed& params)
{
    string map;
    bool first = true;
    for (auto& [name, _]: params) {
        if (first) {
            first = false;
        } else {
            map += ", ";
        }
        map += '"'+string{name}+'"';
    }
    return ", rpcxx::NamesMap("+map+')';
}

template<bool isMethod>
static string generateSingleRegister(const Notify& method, string_view server)
{
    auto do_format = [&](const string& names) {
        return fmt::format(register_method,
                           fmt::arg("method_name", method.name),
                           fmt::arg("server_name", server),
                           fmt::arg("names_mapping", names),
                           fmt::arg("method_or_notify", isMethod ? "Method" : "Notify")
                           );
    };
    if (auto named = std::get_if<ParamsNamed>(&method.params)) {
        return do_format(generateNamesMap(*named));
    } else if (auto pack = std::get_if<ParamsPack>(&method.params)) {
        return do_format(fmt::format(", rpcxx::PackParams<{}>()", PrintType(pack->item)));
    } else {
        return do_format("");
    }
}

static bool sameService(string_view name, string_view service) {
    return name == service.substr(0, service.size() - strlen("_Server"));
}

static string formatMethodRegister(AST& ast, string_view server)
{
    string registerAll;
    for (auto& m: ast.methods) {
        if (!sameService(m.service, server)) continue;
        registerAll += generateSingleRegister<true>(m, server);
    }
    for (auto& n: ast.notify) {
        if (!sameService(n.service, server)) continue;
        registerAll += generateSingleRegister<false>(n, server);
    }
    return registerAll;
}

static string formatMethods(AST& ast, string_view server)
{
    string methods;
    for (auto& m: ast.methods) {
        if (!sameService(m.service, server)) continue;
        auto ret = PrintType(m.returns);
        methods += fmt::format(
            method_fmt,
            fmt::arg("return_type", m.async ? fmt::format("rpcxx::Future<{}>", ret) : ret),
            fmt::arg("method_name", m.name),
            fmt::arg("args_with_types", server::FormatSignature(m.params))
            );
    }
    for (auto& n: ast.notify) {
        if (!sameService(n.service, server)) continue;
        methods += fmt::format(
            method_fmt,
            fmt::arg("return_type", "void"),
            fmt::arg("method_name", n.name),
            fmt::arg("args_with_types", server::FormatSignature(n.params))
            );
    }
    return methods;
}

static string printMethods(AST& ast, string_view server) {
    string res;
    unsigned dontSplit = 0;
    auto add = [&](auto& name) {
        res += ",&_::"+string{name};
        if (++dontSplit == 5) {
            dontSplit = 0;
            res+='\n';
        }
    };
    for (auto& n: ast.notify) {
        if (!sameService(n.service, server)) continue;
        add(n.name);
    }
    for (auto& m: ast.methods) {
        if (!sameService(m.service, server)) continue;
        add(m.name);
    }
    return res;
}

static string formatOne(string_view name, FormatContext& ctx) {
    auto gen_desc = [&]{
        return fmt::format(
            FMT_COMPILE("\nDESCRIBE({}::{}{})"),
            ToNamespace(ctx.params.main.name), name, printMethods(ctx.ast, name));
    };
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    return fmt::format(
        opts.sourceFile ? format_source : format_inline,
        fmt::arg("server_name", name),
        fmt::arg("register_methods", opts.sourceFile ? formatMethodRegister(ctx.ast, name) : ""),
        fmt::arg("methods", opts.sourceFile ? "" : formatMethods(ctx.ast, name)),
        fmt::arg("describe", ctx.params.describeServer ? gen_desc() : "")
        );
}

string Format(FormatContext& ctx)
{
    if (!(ctx.params.targets & TargetServer)) return "";
    if (ctx.ast.methods.empty() && ctx.ast.notify.empty()) {
        return "";
    }
    string result;
    std::set<string_view> names;
    for (auto& m : ctx.ast.methods) {
        names.insert(m.service);
    }
    for (auto& n : ctx.ast.notify) {
        names.insert(n.service);
    }
    for (auto name: names) {
        result += formatOne(string(name) + "_Server", ctx);
    }
    return result;
}

}

