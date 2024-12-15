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

#ifndef RPCXX_PROTO_HPP
#define RPCXX_PROTO_HPP

#include "common.hpp"
#include "rpcxx/exception.hpp"

namespace rpcxx {

template<Protocol proto>
struct Fields {
    static constexpr string_view Id = "id";
    static constexpr string_view Method = proto == Protocol::json_v2_compliant ? "method" : "m";
    static constexpr string_view Params = proto == Protocol::json_v2_compliant ? "params" : "p";
    static constexpr string_view Result = proto == Protocol::json_v2_compliant ? "result" : "r";
    static constexpr string_view Error = proto == Protocol::json_v2_compliant ? "error" : "e";
};

using millis = uint32_t;
constexpr auto NoTimeout = (std::numeric_limits<millis>::max)();
constexpr auto Compliant = std::integral_constant<Protocol, Protocol::json_v2_compliant>{};
constexpr auto Minified = std::integral_constant<Protocol, Protocol::json_v2_minified>{};

template<typename T>
struct NamedArg {
    string_view name;
    const T& value;
};

template<typename T> struct is_named_arg : std::false_type {};
template<typename T> struct is_named_arg<NamedArg<T>> : std::true_type {};

template<typename T>
NamedArg<T> Arg(string_view name, const T& ref) noexcept {return {name, ref};}

struct Method {
    Method() = default;

    explicit Method(string_view str, millis timeout) :
        name(str), timeout(timeout)
    {}

    string_view name;
    millis timeout = NoTimeout;
};

template<Protocol proto>
struct Formatter {
    using Fs = Fields<proto>;
    Formatter() = default;
    JsonView MakeRequest(JsonView id, string_view method, JsonView params) noexcept {
        body = {{
            {Fs::Id, id},
            {Fs::Method, method},
            {Fs::Params, params},
        }};
        if constexpr (proto == Compliant) {
            body[3] = {"jsonrpc", "2.0"};
        }
        return JsonView(body.data(), proto == Compliant ? 4 : 3);
    }
    JsonView MakeNotify(string_view method, JsonView params) noexcept {
        body = {{
            {Fs::Method, method},
            {Fs::Params, params},
        }};
        if constexpr (proto == Compliant) {
            body[2] = {"jsonrpc", "2.0"};
        }
        return JsonView(body.data(), proto == Compliant ? 3 : 2);
    }
    JsonView MakeResponce(JsonView id, JsonView resp) noexcept {
        body = {{
            {Fs::Id, id},
            {Fs::Result, resp},
        }};
        if constexpr (proto == Compliant) {
            body[2] = {"jsonrpc", "2.0"};
        }
        return JsonView(body.data(), proto == Compliant ? 3 : 2);
    }
    JsonView MakeError(JsonView id, RpcException const& exception) noexcept {
        errWrapper.Setup(exception);
        body = {{
            {Fs::Id, id},
            {Fs::Error, errWrapper.View()},
        }};
        if constexpr (proto == Compliant) {
            body[2] = {"jsonrpc", "2.0"};
        }
        return JsonView(body.data(), proto == Compliant ? 3 : 2);
    }
private:
    StaticJsonView<RpcException> errWrapper;
    std::array<JsonPair, 4> body;
};

struct UnpackedRequest {
    string_view method;
    JsonView params;
};

template<Protocol proto>
UnpackedRequest UnpackSingleRequest(JsonView req) {
    JsonView* method = req.FindVal(Fields<proto>::Method);
    if (meta_Unlikely(!method)) {
        throw RpcException{"Missing 'method' field", ErrorCode::invalid_request};
    }
    if (meta_Unlikely(!method->Is(jv::t_string))) {
        JsonPair errobj[] = {{"was_type", method->GetTypeName()}};
        throw RpcException{"'method' field is not a string", ErrorCode::invalid_request, Json(errobj)};
    }
    if (proto == Protocol::json_v2_compliant) {
        if (auto tag = req.FindVal("jsonrpc"); !tag || !DeepEqual(*tag, "2.0")){
            throw RpcException{"'jsonrpc' field missing or != '2.0'", ErrorCode::invalid_request};
        }
    }
    auto foundParams = req.FindVal(Fields<proto>::Params);
    return {method->GetStringUnsafe(), foundParams ? *foundParams : EmptyArray()};
}

template<Protocol proto>
JsonView UnpackSingleResponce(JsonView resp) {
    if (meta_Unlikely(!resp.Is(jv::t_object))) {
        jv::JsonPair data[]{{"was_type", resp.GetTypeName()}};
        throw RpcException("non-object responce", ErrorCode::parse, Json(data));
    } else if (JsonView* res = resp.FindVal(Fields<proto>::Result)) {
        return *res;
    } else if (JsonView* err = resp.FindVal(Fields<proto>::Error)) {
        TraceFrame root;
        throw err->Get<RpcException>({"(rpc.error)", root});
    } else {
        throw RpcException("Missing 'result' or 'error' field", ErrorCode::parse);
    }
}

namespace det {

template<typename...Args, size_t...Is>
static void populateArr(std::index_sequence<Is...>, JsonView* arr, Arena& ctx, const Args&...args) {
    ((void)(arr[Is] = JsonView::From(args, ctx)), ...);
}
template<typename...Args, size_t...Is>
static void populateObj(std::index_sequence<Is...>, JsonPair* obj, Arena& ctx, const Args&...args) {
    ((void)(obj[Is].key = args.name), ...);
    ((void)(obj[Is].value = JsonView::From(args.value, ctx)), ...);
}

}

template<typename...Args>
struct IntoParams  {
    static constexpr auto count = sizeof...(Args);
    static constexpr auto namedCount = ((is_named_arg<Args>::value * 1) + ... + 0);
    static_assert(!namedCount || namedCount == count,
                  "all arguments must be named OR all unnamed");
    JsonView Result;
    IntoParams(const Args&...args) {
        if constexpr (!count) {
            Result = EmptyArray();
        } else if constexpr (namedCount) {
            det::populateObj(std::make_index_sequence<count>(), obj, alloc, args...);
            Result = JsonView(obj, count);
        } else {
            det::populateArr(std::make_index_sequence<count>(), arr, alloc, args...);
            Result = JsonView(arr, count);
        }
    }
private:
    union {
        JsonView arr[count + 1];
        JsonPair obj[count + 1];
    };
    DefaultArena<512> alloc;
};

}

#endif //RPCXX_PROTO_HPP
