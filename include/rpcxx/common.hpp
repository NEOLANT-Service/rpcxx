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

#ifndef RPCXX_COMMON_HPP
#define RPCXX_COMMON_HPP

#include <string_view>
#include <vector>
#include <forward_list>
#include <cstdint>
#include <map>
#include <optional>
#include <memory>

#include "meta/meta.hpp"
#include "future/future.hpp"
#include "json_view/json_view.hpp"
#include "json_view/json.hpp"

namespace rpcxx
{

using namespace meta;
using namespace fut;
using namespace jv;

using std::optional;
using std::string;
using std::string_view;

enum class ErrorCode : int64_t {
    parse = -32700, // Parse error Invalid JSON was received by the server.
    invalid_request = -32600, // Invalid Request The JSON sent is not a valid Request object.
    method_not_found = -32601, // Method not found The method does not exist / is not available.
    invalid_params = -32602, // Invalid params Invalid method parameter(s).
    internal = -32603, //Internal error Internal JSON-RPC error.
    server = -32099, //-32000 to -32099 Server error Reserved for implementation-defined server-errors.
    server_end = -32001, //sentinel value for server implementation-defined errors
};

DESCRIBE("rpcxx::ErrorCode", ErrorCode, EnumAsInteger) {}

constexpr inline string_view PrintCode(ErrorCode code) {
    using namespace std::string_view_literals;
    switch (code) {
    case ErrorCode::parse: return "Parse Error";
    case ErrorCode::invalid_request: return "Invalid Request";
    case ErrorCode::method_not_found: return "Method not found";
    case ErrorCode::invalid_params: return "Invalid Params";
    case ErrorCode::internal: return "Internal Error";
    default: return "User Defined";
    }
}

using OptJson = std::optional<Json>;
using OptJsonView = std::optional<JsonView>;
template<typename T, typename Compare = std::less<>,
         typename Alloc = std::allocator<std::pair<const string, T>>>
using Map = std::map<string, T, Compare, Alloc>;

enum class Protocol {
    //see JSONRPC 2.0 spec
    json_v2_compliant = 0,
    // same as *_compliant, but without ("jsonrpc": "2.0") and "method" => "m", "params" => "p", etc
    json_v2_minified = 1,
};

constexpr inline string_view PrintProto(Protocol proto) {
    switch (proto) {
    case Protocol::json_v2_compliant: return "json_v2_compliant";
    case Protocol::json_v2_minified: return "json_v2_minified";
    default: return "<invalid>";
    }
}

} //rpcxx

template<>
struct jv::Convert<rpcxx::ErrorCode> {
    using T = rpcxx::ErrorCode;
    using underlying = std::underlying_type_t<T>;
    template<typename Alloc>
    static JsonView DoIntoJson(const T& object, Alloc&) {
        return JsonView(underlying(object));
    }
    static void DoFromJson(T& out, JsonView json, TraceFrame const& frame) {
        out = T(json.Get<underlying>(frame));
    }
};

#endif //RPCXX_COMMON_HPP
