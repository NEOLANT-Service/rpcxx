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

namespace rpcxx::gen::cpp::client
{

constexpr auto format_inline = R"EOF(
struct {client_name} : public rpcxx::Client
{{
    using rpcxx::Client::Client;
    {client_name}* as_{client_name}() noexcept {{return this;}}
{methods}{notifications}
}};
)EOF";

constexpr auto format_source = R"EOF(
{methods}{notifications})EOF";

constexpr auto method_inline = FMT_COMPILE(R"EOF(
    template<int=0>
    rpcxx::Future<{return_type}> {method_name}({args_with_types}millis __timeout = {timeout});)EOF");

constexpr auto method_source = FMT_COMPILE(R"EOF(
template<int>
inline rpcxx::Future<{return_type}> {client_name}::{method_name}({args_with_types}millis __timeout) {{
    return Request{if_pack}<{return_type}>(rpcxx::Method{{"{method_name}", __timeout}}{args});
}})EOF");

constexpr auto notify_inline = FMT_COMPILE(R"EOF(
    template<int=0>
    void {method_name}({args_with_types});)EOF");

constexpr auto notify_source = FMT_COMPILE(R"EOF(
template<int>
inline void {client_name}::{method_name}({args_with_types}) {{
    return Notify{if_pack}("{method_name}"{args});
}})EOF");

static bool sameService(string_view name, string_view service) {
    return name == service.substr(0, service.size() - strlen("_Client"));
}

static string formatArgs(const Notify& n)
{
    return Visit(n.params,
        [&](const ParamsPack&) {
            return string{",args"};
        },
        [&](const ParamsArray& positinal) {
            string result;
            for (auto count = 0u; count < positinal.size(); ++count) {
                result += fmt::format(",{}{}", "arg", count);
            }
            return result;
        },
        [&](const ParamsNamed& named) {
            string result;
            for (auto& [name, _]: named) {
                result += fmt::format(",rpcxx::Arg(\"{}\", {})", name, name);
            }
            return result;
        });
}

static string sig(Params const& params, bool method) {
    auto sz = Visit(params, [](auto& p){return p.size();});
    return client::FormatSignature(params) + (method && sz ? ", " : "");
}

static string formatMethods(FormatContext& ctx, string_view name)
{
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    string result;
    for (auto& m: ctx.ast.methods) {
        if (!sameService(m.service, name)) continue;
        auto formatter = [&](auto fmt){
            return fmt::format(
                fmt,
                fmt::arg("if_pack", std::holds_alternative<ParamsPack>(m.params) ? "Pack" : ""),
                fmt::arg("client_name", name),
                fmt::arg("return_type", PrintType(m.returns)),
                fmt::arg("method_name", m.name),
                fmt::arg("args", formatArgs(m)),
                fmt::arg("timeout", m.timeout),
                fmt::arg("args_with_types", sig(m.params, true))
                );
        };
        if (opts.sourceFile) {
            result += formatter(method_source);
        } else {
            result += formatter(method_inline);
        }
    }
    return result;
}

static string formatNotifications(FormatContext& ctx, string_view name)
{
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    string result;
    for (auto& n: ctx.ast.notify) {
        if (!sameService(n.service, name)) continue;
        auto formatter = [&](auto fmt){
            return fmt::format(
                fmt,
                fmt::arg("if_pack", std::holds_alternative<ParamsPack>(n.params) ? "Pack" : ""),
                fmt::arg("client_name", name),
                fmt::arg("method_name", n.name),
                fmt::arg("args", formatArgs(n)),
                fmt::arg("args_with_types", sig(n.params, false))
                );
        };
        if (opts.sourceFile) {
            result += formatter(notify_source);
        } else {
            result += formatter(notify_inline);
        }
    }
    return result;
}

static string formatOne(string_view name, FormatContext& ctx) {
    auto& opts = *static_cast<CppOpts*>(ctx.opts);
    return fmt::format(
        opts.sourceFile ? format_source : format_inline,
        fmt::arg("client_name", name),
        fmt::arg("methods", formatMethods(ctx, name)),
        fmt::arg("notifications", formatNotifications(ctx, name))
        );
}

string Format(FormatContext& ctx)
{
    if (!(ctx.params.targets & TargetClient)) return "";
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
        result += formatOne(string{name} + "_Client", ctx);
    }
    return result;
}

}

