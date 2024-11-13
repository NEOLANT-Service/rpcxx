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

#include "json_view/pointer.hpp"
#include <algorithm>

using namespace jv;

namespace {

static JsonKey parseOne(string_view raw, bool onlyNumbers) {
    if (raw.empty()) {
        return JsonKey{""};
    } else if (onlyNumbers) {
        unsigned idx = 0;
        auto end = raw.data() + raw.size();
        auto [ptr, ec] = std::from_chars(raw.data(), end, idx, 10);
        if (ec != std::errc{}) {
            throw std::runtime_error("Invalid number on JsonPointer");
        }
        if (ptr != end) {
            throw std::runtime_error("Invalid number on JsonPointer");
        }
        return JsonKey{idx};
    } else {
        return JsonKey{raw};
    }
}

static constexpr bool needPercentEncode(char c) {
    return !(
        (c >= '0' && c <= '9')
        || (c >= 'A' && c <='Z')
        || (c >= 'a' && c <= 'z')
        || c == '-'
        || c == '.'
        || c == '_'
        || c == '~');
}

static char percentDecode(size_t& idx, string_view src) {
    char c = 0;
    for (int j = 0; j < 2; j++) {
        c = c << 4;
        auto h = src[idx++];
        if      (h >= '0' && h <= '9') c = static_cast<char>(c + h - '0');
        else if (h >= 'A' && h <= 'F') c = static_cast<char>(c + h - 'A' + 10);
        else if (h >= 'a' && h <= 'f') c = static_cast<char>(c + h - 'a' + 10);
        else {
            throw std::runtime_error("Invalid percent encoding in Json Pointer");
        }
    }
    return c;
}

static void parseTokens(
    char sep,
    char* meta_Restrict storage,
    size_t cap,
    size_t len,
    const char* meta_Restrict src,
    JsonKey* out)
{
    size_t toks = 0;
    bool escape = false;
    bool onlyNumbers = true;
    bool isUri = src[0] == '#';
    size_t idx = 0;
    size_t ptr = 0;
    size_t last = 0;
    if (isUri) {
        idx++;
    }
    if (*src == sep) {
        idx++;
    }
    auto append = [&](char ch){
        (void)cap; assert(ptr < cap);
        storage[ptr++] = ch;
    };
    auto add_token = [&]{
        append('\0');
        out[toks++] = parseOne({storage + last, ptr - last - 1}, onlyNumbers);
        last = ptr + 1;
        onlyNumbers = true;
    };
    while (idx < len) {
        char ch = src[idx++];
        if (ch > '9' || ch < '0') {
            onlyNumbers = false;
        }
        if (ch == sep) {
            add_token();
        }
        if (escape) {
            if (ch == '0') {
                append('~');
            } else if (ch == '1') {
                append(sep);
            } else {
                throw std::runtime_error("Invalid escape in Json Pointer");
            }
        } else if (ch == '~') {
            escape = true;
        } else {
            if (isUri) {
                if (ch == '%') {
                    if (meta_Unlikely(len - idx < 2)) {
                        throw std::runtime_error("Percent encoding missing tail in Json Pointer");
                    }
                    append(percentDecode(idx, src));
                    continue;
                } else if (needPercentEncode(ch)) {
                    throw std::runtime_error("Percent encode missing in Json Pointer");
                }
            }
            append(ch);
        }
        if (idx == len) {
            add_token();
        }
    }
}

}

JsonPointer JsonPointer::FromString(string_view ptr, Arena &alloc, char sep)
{
    if (ptr.empty()) {
        return {};
    }
    if (ptr.size() >= (std::numeric_limits<unsigned>::max)()) {
        throw std::runtime_error("json pointer is too big");
    }
    auto count = unsigned(std::count(ptr.begin(), ptr.end(), sep));
    if (ptr[0] != sep) count++;
    ArenaString storage(alloc);
    storage.reserve(ptr.size() + 1 + count);
    auto tokens = static_cast<JsonKey*>(alloc(sizeof(JsonKey) * count, alignof(JsonKey)));
    parseTokens(sep, storage.data(), storage.capacity(), ptr.size(), ptr.data(), tokens);
    return {tokens, count};
}

JsonPointer JsonPointer::FromString(string_view ptr, Arena& alloc)
{
    return FromString(ptr, alloc, '/');
}

std::string JsonPointer::Join(char sep, bool uri) const
{
    membuff::StringOut buff;
    JoinInto(buff, sep, uri);
    return buff.Consume();
}

namespace {

static void writeNumber(membuff::Out &buff, unsigned num) {
    char temp[std::numeric_limits<unsigned>::digits10 + 1];
    auto [ptr, ec] = std::to_chars(std::begin(temp), std::end(temp), num);
    buff.Write(temp, ptr - temp);
}

struct PercHelper {
    char data[3];
    constexpr operator string_view() const {
        return {data, 3};
    }
};

static constexpr const char hexDigits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F' };

constexpr PercHelper PercentEncode(char c) {
    auto u = static_cast<unsigned char>(c);
    return {{'%', hexDigits[u >> 4], hexDigits[u & 15]}};
}

template<bool uri>
static void writeString(membuff::Out &buff, string_view str) {
    for (auto ch: str) {
        if (ch == '~') {
            buff.Write("~0");
        } else if (ch == '/') {
            buff.Write("~1");
        } else if (uri && needPercentEncode(ch)) {
            buff.Write(PercentEncode(ch));
        } else {
            buff.Write(ch);
        }
    }
}

template<bool uri>
void write(membuff::Out& buff, char sep, JsonPointer ptr) {
    for (auto& part: ptr) {
        part.Visit([&](string_view key){
            buff.Write(sep);
            writeString<uri>(buff, key);
        }, [&](unsigned idx){
            buff.Write(sep);
            writeNumber(buff, idx);
        });
    }
}

}


void JsonPointer::JoinInto(membuff::Out &out, char sep, bool uri) const
{
    if (uri) {
        write<true>(out, sep, *this);
    } else {
        write<false>(out, sep, *this);
    }
}
