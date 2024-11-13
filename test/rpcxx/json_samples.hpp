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

#pragma once

#include <string_view>
#include <string>
#include "rpcxx/rpcxx.hpp"

inline auto RPCSample = R"({
    "method": "methodName",
    "id": "arbitrary-something",
    "params": [3, 2, {"epic": "param"}],
    "jsonrpc": "2.0"
})";

inline auto MinifiedRPCSample =
    R"({"method":"methodName","id":"arbitrary-something","params":[3,2,{"epic":"param"}],"jsonrpc":"2.0"})";

inline auto EarlyFailSample = R"({
    "method"2: "methodName",
    "id": "arbitrary-something",
    "params": [3, 2, {"epic": "param"}],
    "jsonrpc": "2.0"
})";

inline auto BooksSample = R"EOF({
    "glossary": [
    {
        "title": "example glossary",
        "GlossDiv": {
            "title": "S",
            "GlossList": {
                "GlossEntry": {
                    "ID": "SGML",
                    "SortAs": "SGML",
                    "GlossTerm": "Standard Generalized Markup Language",
                    "Acronym": "SGML",
                    "Abbrev": "ISO 8879:1986",
                    "GlossDef": {
                        "para": "A meta-markup language, used to create markup languages such as DocBook.",
                        "GlossSeeAlso": ["GML", "XML"]
                    },
                    "GlossSee": "markup"
                }
            }
        }
    },
    {
        "title": "example glossary",
        "GlossDiv": {
            "title": "S",
            "GlossList": {
                "GlossEntry": {
                    "ID": "SGML",
                    "SortAs": "SGML",
                    "GlossTerm": "Standard Generalized Markup Language",
                    "Acronym": "SGML",
                    "Abbrev": "ISO 8879:1986",
                    "GlossDef": {
                        "para": "A meta-markup language, used to create markup languages such as DocBook.",
                        "GlossSeeAlso": ["GML", "XML"]
                    },
                    "GlossSee": "markup"
                }
            }
        }
    },
    {
        "title": "example glossary",
        "GlossDiv": {
            "title": "S",
            "GlossList": {
                "GlossEntry": {
                    "ID": "SGML",
                    "SortAs": "SGML",
                    "GlossTerm": "Standard Generalized Markup Language",
                    "Acronym": "SGML",
                    "Abbrev": "ISO 8879:1986",
                    "GlossDef": {
                        "para": "A meta-markup language, used to create markup languages such as DocBook.",
                        "GlossSeeAlso": ["GML", "XML"]
                    },
                    "GlossSee": "markup"
                }
            }
        }
    }
]
})EOF";

inline std::string BigSample = []{
    using namespace std::string_literals;
    auto part = std::string(BooksSample);
    auto bigPart = '['+part+','+part+','+part+','+part+','+part+','+part+','+part+','+part+']';
    auto biggerPart = '['+bigPart+','+bigPart+','+bigPart+','+bigPart+','+bigPart+','+bigPart+','+bigPart+','+bigPart+']';
    return R"({"big1":)"+biggerPart+R"(, "big2":)"+biggerPart+'}';
}();

inline auto LateFailSample = R"(
    {
        "title": "example glossary",
        "GlossDiv": {
            "title": "S",
            "GlossList": {
                "GlossEntry": {
                    "ID": "SGML",
                    "SortAs": "SGML",
                    "GlossTerm": "Standard Generalized Markup Language",
                    "Acronym": "SGML",
                    "Abbrev": "ISO 8879:1986",
                    "GlossDef": {
                        "para": "A meta-markup language, used to create markup languages such as DocBook.",
                        "GlossSeeAlso": ["GML", "XML"]
                    },
                    "GlossSee": "markup"
                }
            }
        }
    123}
)";


static constexpr uint8_t MsgPackRPC[] = {
    0x84, 0xA6, 0x6D, 0x65, 0x74, 0x68, 0x6F, 0x64, 0xAA, 0x6D, 0x65, 0x74, 0x68, 0x6F, 0x64, 0x4E, 0x61, 0x6D, 0x65, 0xA2, 0x69, 0x64
   , 0xB3, 0x61, 0x72, 0x62, 0x69, 0x74, 0x72, 0x61, 0x72, 0x79, 0x2D, 0x73, 0x6F, 0x6D, 0x65, 0x74, 0x68, 0x69, 0x6E, 0x67, 0xA6
   , 0x70, 0x61, 0x72, 0x61, 0x6D, 0x73, 0x93, 0x03, 0x02, 0x81, 0xA4, 0x65, 0x70, 0x69, 0x63, 0xA5, 0x70, 0x61, 0x72, 0x61, 0x6D
   , 0xA7, 0x6A, 0x73, 0x6F, 0x6E, 0x72, 0x70, 0x63, 0xA3, 0x32, 0x2E, 0x30
};

static constexpr uint8_t MsgPackBooks[] = {
    129, 168, 103, 108, 111, 115, 115, 97, 114, 121, 147, 130, 165, 116, 105, 116, 108, 101, 176, 101, 120, 97, 109, 112, 108, 101, 32, 103, 108, 111, 115, 115, 97, 114, 121, 168, 71, 108, 111, 115, 115, 68, 105, 118, 130, 165, 116, 105, 116, 108, 101, 161, 83, 169, 71, 108, 111, 115, 115, 76, 105, 115, 116, 129, 170, 71, 108, 111, 115, 115, 69, 110, 116, 114, 121, 135, 162, 73, 68, 164, 83, 71, 77, 76, 166, 83, 111, 114, 116, 65, 115, 164, 83, 71, 77, 76, 169, 71, 108, 111, 115, 115, 84, 101, 114, 109, 217, 36, 83, 116, 97, 110, 100, 97, 114, 100, 32, 71, 101, 110, 101, 114, 97, 108, 105, 122, 101, 100, 32, 77, 97, 114, 107, 117, 112, 32, 76, 97, 110, 103, 117, 97, 103, 101, 167, 65, 99, 114, 111, 110, 121, 109, 164, 83, 71, 77, 76, 166, 65, 98, 98, 114, 101, 118, 173, 73, 83, 79, 32, 56, 56, 55, 57, 58, 49, 57, 56, 54, 168, 71, 108, 111, 115, 115, 68, 101, 102, 130, 164, 112, 97, 114, 97, 217, 72, 65, 32, 109, 101, 116, 97, 45, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 44, 32, 117, 115, 101, 100, 32, 116, 111, 32, 99, 114, 101, 97, 116, 101, 32, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 115, 32, 115, 117, 99, 104, 32, 97, 115, 32, 68, 111, 99, 66, 111, 111, 107, 46, 172, 71, 108, 111, 115, 115, 83, 101, 101, 65, 108, 115, 111, 146, 163, 71, 77, 76, 163, 88, 77, 76, 168, 71, 108, 111, 115, 115, 83, 101, 101, 166, 109, 97, 114, 107, 117, 112, 130, 165, 116, 105, 116, 108, 101, 176, 101, 120, 97, 109, 112, 108, 101, 32, 103, 108, 111, 115, 115, 97, 114, 121, 168, 71, 108, 111, 115, 115, 68, 105, 118, 130, 165, 116, 105, 116, 108, 101, 161, 83, 169, 71, 108, 111, 115, 115, 76, 105, 115, 116, 129, 170, 71, 108, 111, 115, 115, 69, 110, 116, 114, 121, 135, 162, 73, 68, 164, 83, 71, 77, 76, 166, 83, 111, 114, 116, 65, 115, 164, 83, 71, 77, 76, 169, 71, 108, 111, 115, 115, 84, 101, 114, 109, 217, 36, 83, 116, 97, 110, 100, 97, 114, 100, 32, 71, 101, 110, 101, 114, 97, 108, 105, 122, 101, 100, 32, 77, 97, 114, 107, 117, 112, 32, 76, 97, 110, 103, 117, 97, 103, 101, 167, 65, 99, 114, 111, 110, 121, 109, 164, 83, 71, 77, 76, 166, 65, 98, 98, 114, 101, 118, 173, 73, 83, 79, 32, 56, 56, 55, 57, 58, 49, 57, 56, 54, 168, 71, 108, 111, 115, 115, 68, 101, 102, 130, 164, 112, 97, 114, 97, 217, 72, 65, 32, 109, 101, 116, 97, 45, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 44, 32, 117, 115, 101, 100, 32, 116, 111, 32, 99, 114, 101, 97, 116, 101, 32, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 115, 32, 115, 117, 99, 104, 32, 97, 115, 32, 68, 111, 99, 66, 111, 111, 107, 46, 172, 71, 108, 111, 115, 115, 83, 101, 101, 65, 108, 115, 111, 146, 163, 71, 77, 76, 163, 88, 77, 76, 168, 71, 108, 111, 115, 115, 83, 101, 101, 166, 109, 97, 114, 107, 117, 112, 130, 165, 116, 105, 116, 108, 101, 176, 101, 120, 97, 109, 112, 108, 101, 32, 103, 108, 111, 115, 115, 97, 114, 121, 168, 71, 108, 111, 115, 115, 68, 105, 118, 130, 165, 116, 105, 116, 108, 101, 161, 83, 169, 71, 108, 111, 115, 115, 76, 105, 115, 116, 129, 170, 71, 108, 111, 115, 115, 69, 110, 116, 114, 121, 135, 162, 73, 68, 164, 83, 71, 77, 76, 166, 83, 111, 114, 116, 65, 115, 164, 83, 71, 77, 76, 169, 71, 108, 111, 115, 115, 84, 101, 114, 109, 217, 36, 83, 116, 97, 110, 100, 97, 114, 100, 32, 71, 101, 110, 101, 114, 97, 108, 105, 122, 101, 100, 32, 77, 97, 114, 107, 117, 112, 32, 76, 97, 110, 103, 117, 97, 103, 101, 167, 65, 99, 114, 111, 110, 121, 109, 164, 83, 71, 77, 76, 166, 65, 98, 98, 114, 101, 118, 173, 73, 83, 79, 32, 56, 56, 55, 57, 58, 49, 57, 56, 54, 168, 71, 108, 111, 115, 115, 68, 101, 102, 130, 164, 112, 97, 114, 97, 217, 72, 65, 32, 109, 101, 116, 97, 45, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 44, 32, 117, 115, 101, 100, 32, 116, 111, 32, 99, 114, 101, 97, 116, 101, 32, 109, 97, 114, 107, 117, 112, 32, 108, 97, 110, 103, 117, 97, 103, 101, 115, 32, 115, 117, 99, 104, 32, 97, 115, 32, 68, 111, 99, 66, 111, 111, 107, 46, 172, 71, 108, 111, 115, 115, 83, 101, 101, 65, 108, 115, 111, 146, 163, 71, 77, 76, 163, 88, 77, 76, 168, 71, 108, 111, 115, 115, 83, 101, 101, 166, 109, 97, 114, 107, 117, 112
};
