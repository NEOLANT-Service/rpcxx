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

#include "json_view/algo.hpp"
#include "json_view/parse.hpp"
#include <algorithm>
#include <charconv>
#include <deque>
#include <string>
#include <fstream>
#include "rapidjson/reader.h"
#include "rapidjson/error/en.h"
#include "alloc_adapter.hpp"

using namespace jv;
using namespace rapidjson;

namespace {


struct LimitedStream : InsituStringStream {
    char* end;
    LimitedStream(char* beg, size_t len) :
        InsituStringStream(beg),
        end(beg + len)
    {}
    Ch Peek() { return meta_Unlikely(src_ == end) ? 0 : *src_; }
    Ch Take() { return meta_Unlikely(src_ == end) ? 0 : *src_++; }
};

}

template<>
struct rapidjson::StreamTraits<LimitedStream> {
    enum { copyOptimization = 1 };
};

namespace {

struct SaxHandler
{
    typedef typename UTF8<>::Ch Ch;
    enum Tag {
        val,
        arr,
        obj,
    };
    Arena& alloc;
    ParseSettings opts;
    struct State {
        union {
            JsonView* array;
            JsonPair* object;
        };
        unsigned capacity;
        unsigned size;
        string_view key;
        Tag tag = val;
    };
    ArenaVector<State> stack = ArenaVector<State>(alloc);
    JsonView result = {};
    State current = {};

    void appendToArray(JsonView view) {
        if (meta_Unlikely(current.size == current.capacity)) {
            auto newCap = current.capacity ? current.capacity * 2 : 2;
            auto newArr = MakeArrayOf(newCap, alloc);
            if (current.array) {
                memcpy(newArr, current.array, sizeof(JsonView) * current.size);
            }
            current.array = newArr;
            current.capacity = newCap;
        }
        current.array[current.size++] = view;
    }


    void appendToObject(JsonView view) {
        if (meta_Unlikely(current.size == current.capacity)) {
            auto newCap = current.capacity ? current.capacity * 2 : 2;
            auto newObj = MakeObjectOf(newCap, alloc);
            if (current.object) {
                ::memcpy(newObj, current.object, sizeof(JsonPair) * current.size);
            }
            current.object = newObj;
            current.capacity = newCap;
        }
        current.size = SortedInsertJson(current.object, current.size, {current.key, view}, current.capacity);
    }

    std::true_type doAdd(JsonView view) {
        if (meta_Unlikely(current.tag == val)) {
            result = view;
        } else if (current.tag == obj) {
            appendToObject(view);
        } else { //arr
            assert(current.tag == arr);
            appendToArray(view);
        }
        return {};
    }

    std::false_type RawNumber(const char*, size_t, bool) {
        return {};
    }
    void Push() {
        if (meta_Unlikely(stack.size() == opts.maxDepth)) {
            throw DepthError{};
        }
        stack.push_back(current);
        current = {};
    }
    State Pop() {
        auto was = current;
        current = stack.back();
        stack.pop_back();
        return was;
    }
    std::true_type StartArray() {
        Push();
        current.tag = arr;
        return {};
    }
    std::true_type StartObject() {
        Push();
        current.tag = obj;
        return {};
    }
    std::true_type EndArray(SizeType) {
        auto was = Pop();
        assert(was.tag == arr);
        doAdd(JsonView(was.array, was.size));
        return {};
    }
    std::true_type EndObject(SizeType) {
        auto was = Pop();
        assert(was.tag == obj);
        Data curr;
        curr.size = was.size;
        curr.d.object = was.object;
        curr.type = t_object;
        doAdd(JsonView(curr));
        return {};
    }
    std::true_type Null() {
        return doAdd(nullptr);
    }
    std::true_type Bool(bool v) {
        return doAdd(JsonView(v));
    }
    std::true_type String(const Ch* str, rapidjson::SizeType len, bool) {
        doAdd(string_view{str, len});
        return {};
    }
    std::true_type Key(const Ch* str, rapidjson::SizeType len, bool) {
        current.key = {str, len};
        return {};
    }
    JsonView Result() const noexcept {
        return result;
    }

    std::true_type Int(int v) {
        doAdd(JsonView(v));
        return {};
    }
    std::true_type Uint(unsigned v) {
        doAdd(JsonView(v));
        return {};
    }
    std::true_type Int64(int64_t v) {
        doAdd(JsonView(v));
        return {};
    }
    std::true_type Uint64(uint64_t v) {
        doAdd(JsonView(v));
        return {};
    }
    std::true_type Double(double v) {
        doAdd(JsonView(v));
        return {};
    }
};

static std::string atOffset(string_view src, size_t offs) {
    size_t line = 0;
    size_t col = 0;
    for (auto ch: src.substr(0, offs)) {
        if (ch == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    return " @ line(" + std::to_string(line) + ") col(" + std::to_string(col) + ")";
}

static jv::JsonView parseOwnedBuff(char* buff, size_t len, Arena& alloc, ParseSettings params) {
    RapidArenaAllocator rapidAlloc{&alloc};
    GenericReader<UTF8<>, UTF8<>, RapidArenaAllocator> reader(&rapidAlloc);
    LimitedStream stream(buff, len);
    constexpr auto flags = kParseCommentsFlag | kParseTrailingCommasFlag
                           | kParseInsituFlag | kParseIterativeFlag
                           | kParseNanAndInfFlag;
    SaxHandler handler{alloc, params};
    try {
        reader.Parse<flags>(stream, handler);
    } catch (ParsingError& e) {
        e.position = reader.GetErrorOffset();
    }
    if (meta_Unlikely(reader.HasParseError())) {
        auto offs = reader.GetErrorOffset();
        auto msg = GetParseError_En(reader.GetParseErrorCode()) + atOffset({buff, len}, offs);
        ParsingError err(std::move(msg));
        err.position = offs;
        throw std::move(err);
    }
    return handler.Result();
}

}

jv::JsonView jv::ParseJson(membuff::In& data, Arena& alloc, ParseSettings params) {
    ArenaString buff(alloc);
    if (auto hint = data.TryTotalLeft()) {
        buff.reserve(hint);
    }
    char temp[2048];
    while (auto read = data.Read(temp, sizeof(temp))) {
        buff.Append({temp, read});
    }
    return parseOwnedBuff(buff.data(), buff.size(), alloc, params);
}

jv::JsonView jv::ParseJsonFile(std::filesystem::path const& file, Arena& alloc, ParseSettings params) {
    std::ifstream source(file);
    if (!source.is_open()) {
        throw ParsingError("Could not open: " + file.string());
    }
    try {
        return ParseJson(source, alloc, params);
    } catch (ParsingError& e) {
        ParsingError wrap(file.string() + ": " + e.what());
        wrap.position = e.position;
        throw std::move(wrap);
    }
}

jv::JsonView jv::ParseJson(string_view json, Arena& alloc, ParseSettings params) {
    ArenaString buff(json, alloc);
    return parseOwnedBuff(buff.data(), buff.size(), alloc, params);
}

jv::JsonView jv::ParseJsonInPlace(char* buff, size_t len, Arena& alloc, ParseSettings params) {
    return parseOwnedBuff(buff, len, alloc, params);
}

jv::JsonView jv::ParseJson(std::istream& data, Arena& alloc, ParseSettings params) {
    membuff::IStreamIn in(data);
    return ParseJson(in, alloc, params);
}
