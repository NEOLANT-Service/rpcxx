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

#ifndef RPCXX_EXCEPTION_HPP
#define RPCXX_EXCEPTION_HPP

#include <optional>
#include <string>
#include "common.hpp"
#include "describe/describe.hpp"

namespace rpcxx
{

struct RpcException : public std::exception
{
    RpcException() noexcept = default;
    RpcException(std::string_view msg,
                 ErrorCode code = ErrorCode::internal,
                 std::optional<Json> data = {}) :
        code(code),
        message(msg),
        data(std::move(data))
    {}
    ErrorCode code;
    std::string message;
    std::optional<Json> data;
    const char* what() const noexcept override {
        return message.c_str();
    }
};

DESCRIBE(RpcException, &_::code, &_::message, &_::data)

}

#endif //RPCXX_EXCEPTION_HPP
