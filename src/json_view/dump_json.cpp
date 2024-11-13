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

#include "json_view/dump.hpp"
#include <charconv>
#include "rapidjson/internal/strtod.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "alloc_adapter.hpp"

using namespace jv;
using namespace std::string_view_literals;
using namespace rapidjson;

namespace {

struct Stream {
    using Ch = char;
    membuff::Out &out;
    void PutUnsafe(char ch) {
        out.Write(ch);
    }
    void Put(char ch) {
        out.Write(ch);
    }
    void Flush() {}
};

constexpr auto wrFlags = kWriteNanAndInfNullFlag;

using Pretty = PrettyWriter<Stream, UTF8<>, UTF8<>, RapidArenaAllocator, wrFlags>;
using Basic = Writer<Stream, UTF8<>, UTF8<>, RapidArenaAllocator, wrFlags>;

template<typename Target>
struct Override : public Target {
    using Target::Target;
    void DoRawNumber(const char* beg, const char* end) {
        if constexpr (std::is_same_v<Target, Pretty>) {
            this->PrettyPrefix(kNumberType);
        } else {
            this->Prefix(kNumberType);
        }
        this->os_->out.Write(beg, end - beg);
    }
};

template<typename Writer>
static void visit(JsonView json, unsigned depth, Writer& wr) {
    DepthError::Check(depth--);
    switch (json.GetType()) {
    case t_array: {
        wr.StartArray();
        for (auto i: json.Array(false)) {
            visit(i, depth, wr);
        }
        wr.EndArray(json.GetUnsafe().size);
        break;
    }
    case t_object:{
        wr.StartObject();
        for (auto [key, v]: json.Object(false)) {
            assert(!key.empty());
            wr.Key(key.data(), key.size(), false);
            visit(v, depth, wr);
        }
        wr.EndObject(json.GetUnsafe().size);
        break;
    }
    case t_number: {
        wr.Double(json.GetUnsafe().d.number);
        break;
    }
    case t_signed: {
        char buff[50];
        auto [ptr, ec] = std::to_chars(std::begin(buff), std::end(buff), json.GetUnsafe().d.integer);
        wr.DoRawNumber(buff, ptr);
        break;
    }
    case t_unsigned: {
        char buff[50];
        auto [ptr, ec] = std::to_chars(std::begin(buff), std::end(buff), json.GetUnsafe().d.uinteger);
        wr.DoRawNumber(buff, ptr);
        break;
    }
    case t_null: {
        wr.Null();
        break;
    }
    case t_string: {
        auto str = json.GetStringUnsafe();
        if (auto s = str.data()) {
            wr.String(s, str.size());
        } else {
            wr.String("", 0);
        }
        break;
    }
    case t_boolean: {
        wr.Bool(json.GetUnsafe().d.boolean);
        break;
    }
    default: {
        break;
    }
    }
}

}

void jv::DumpJsonInto(membuff::Out &out, JsonView json, DumpOptions opts)
{
    Stream stream{out};
    DefaultArena arena;
    RapidArenaAllocator alloc{&arena};
    if (opts.pretty) {
        Override<Pretty> writer(stream, &alloc, 32);
        writer.SetIndent(opts.indentChar, opts.indent);
        visit(json, opts.maxDepth, writer);
    } else {
        Override<Basic> writer(stream, &alloc);
        visit(json, opts.maxDepth, writer);
    }
}
