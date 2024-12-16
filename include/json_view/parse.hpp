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

#ifndef JV_JSON_PARSER_HPP
#define JV_JSON_PARSER_HPP
#pragma once

#include "json_view.hpp"
#include <filesystem>
#include "membuff/membuff.hpp"
#include "alloc.hpp"

namespace jv
{

struct ParsingError : public std::runtime_error {
    using std::runtime_error::runtime_error;
    size_t position = 0;
};

struct ParseSettings {
    ParseSettings() = default;
    unsigned maxDepth = JV_DEFAULT_DEPTH;
};

struct [[nodiscard]] ParseResult {
    JsonView result;
    size_t consumed;
    operator JsonView() const noexcept {
        return result;
    }
};


JsonView ParseJson(std::istream& data, Arena& alloc, ParseSettings params = {});
JsonView ParseJson(membuff::In& data, Arena& alloc, ParseSettings params = {});
JsonView ParseJsonInPlace(char* buff, size_t len, Arena& alloc, ParseSettings params = {});
JsonView ParseJsonFile(std::filesystem::path const& file, Arena& alloc, ParseSettings params = {});
JsonView ParseJson(string_view json, Arena& alloc, ParseSettings params = {});

ParseResult ParseMsgPack(string_view data, Arena& alloc, ParseSettings params = {});
ParseResult ParseMsgPackInPlace(string_view data, Arena& alloc, ParseSettings params = {});
ParseResult ParseMsgPackInPlace(const void* data, size_t size, Arena& alloc, ParseSettings params = {});
ParseResult ParseMsgPack(membuff::In& reader, Arena& alloc, ParseSettings params = {});

}

#endif //JV_JSON_PARSER_HPP
