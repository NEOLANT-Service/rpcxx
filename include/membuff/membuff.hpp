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

#ifndef MEMBUFF_HPP
#define MEMBUFF_HPP

#include <cstring>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include "meta/compiler_macros.hpp"
#include <istream>

// These classes are used for half-virtual buffer implementations
// Api is absolutely minimal - just one virt method to be implemented
// membuff::Out::Grow(amount) -> try to allocate more space (or flush previous)
// membuff::In::Refill(amount) -> try to get more data to read from buffer

namespace membuff
{

inline constexpr size_t NoHint = 0;

struct In
{
    const char* buffer = {};
    size_t ptr = {};
    size_t capacity = {};
    long LastError = {};

    size_t Available() const noexcept;
    [[nodiscard]]
    char ReadByte(size_t growAmount = NoHint);
    size_t Read(char* buff, size_t size, size_t growAmount = NoHint);
    size_t Read(void* buff, size_t size, size_t growAmount = NoHint);
    virtual size_t TryTotalLeft() {return 0;}
    virtual void Refill(size_t amountHint = NoHint) = 0;
    virtual ~In() = default;
};

struct Out
{
    char* buffer = {};
    size_t ptr = {};
    size_t capacity = {};
    long LastError = {};

    size_t SpaceLeft() const noexcept;
    char* Current() noexcept;
    void Write(const char* data, size_t size, size_t growAmount = NoHint);
    void Write(const void* data, size_t size, size_t growAmount = NoHint);
    void Write(std::string_view data, size_t growAmount = NoHint);
    void Write(char byte, size_t growAmount = NoHint);
    void Write(uint8_t byte, size_t growAmount = NoHint) {
        return Write(char(byte), growAmount);
    }
    virtual void Grow(size_t amountHint) = 0;
    virtual ~Out() = default;
};

template<typename String = std::string>
struct StringOut final: Out
{
    using size_type = typename String::size_type;
    [[nodiscard]] String Consume() noexcept {
        out.resize(size_type(ptr));
        capacity = ptr = 0;
        return std::move(out);
    }
    void Grow(size_t amount) override {
        out.resize(out.size() + size_type(amount));
        buffer = reinterpret_cast<char*>(out.data());
        capacity = size_t(out.size());
    }
    template<typename...Args>
    StringOut(size_t startSize = 512, Args&&...a) : out(std::forward<Args>(a)...) {
        init(startSize);
    }
protected:
    void init(size_t startSize) {
        out.resize(size_type(startSize));
        buffer = reinterpret_cast<char*>(out.data());
        capacity = size_t(out.size());
    }
    String out;
};

template<size_t buff = 1024, typename Fn = void>
struct FuncOut final : Out {
    FuncOut(Fn f) : f(std::move(f)) {
        capacity = buff;
        ptr = 0;
        buffer = stor;
    }
    void Flush() {
      f(static_cast<char*>(stor), ptr);
      ptr = 0;
    }
protected:
    void Grow(size_t) override {
        Flush();
    }
    char stor[buff];
    Fn f;
};

template<typename Fn> FuncOut(Fn) -> FuncOut<1024, Fn>;

struct IStreamIn final : In {
    char buff[2048];
    std::istream& stream;
    IStreamIn(std::istream& stream) : stream(stream) {
        ptr = 0;
        buffer = buff;
        capacity = 0;
        Refill(NoHint);
    }
    void Refill(size_t) override {
        stream.read(buff, sizeof(buff));
        auto read = stream.gcount();
        if (read > 0) {
            capacity = size_t(read);
        } else {
            LastError = long(read);
            capacity = 0;
        }
    }
};

inline size_t Out::SpaceLeft() const noexcept {
    return capacity - ptr;
}

inline char *Out::Current() noexcept {
    return buffer + ptr;
}

inline void Out::Write(const char *data, size_t size, size_t growAmount)
{
    if (ptr + size > capacity) {
        do {
            Grow(growAmount ? growAmount : capacity);
            if (meta_Unlikely(LastError)) {
                return;
            }
            auto left = capacity - ptr;
            auto min = std::min meta_NO_MACRO (size, left);
            if (meta_Likely(min)) {
                ::memcpy(buffer + ptr, data, min);
            }
            size -= min;
            ptr += min;
            data += min;
        } while (size);
    } else {
        if (meta_Likely(size)) {
            ::memcpy(buffer + ptr, data, size);
        }
        ptr += size;
    }
}

inline void Out::Write(const void *data, size_t size, size_t growAmount)
{
    Write(static_cast<const char*>(data), size, growAmount);
}

inline void Out::Write(std::string_view data, size_t growAmount)
{
    return Write(data.data(), data.size(), growAmount);
}

inline void Out::Write(char byte, size_t growAmount) {
    LastError = 0;
    if (meta_Unlikely(ptr >= capacity)) {
        Grow(growAmount ? growAmount : capacity);
        if (meta_Unlikely(LastError)) {
            return;
        }
    }
    buffer[ptr++] = byte;
}

inline size_t In::Available() const noexcept
{
    return capacity - ptr;
}

inline char In::ReadByte(size_t growAmount)
{
    LastError = 0;
    if (meta_Unlikely(ptr >= capacity)) {
        Refill(growAmount ? growAmount : capacity);
        if (!capacity) {
            return {};
        }
    }
    char res = buffer[ptr++];
    return res;
}

inline size_t In::Read(char *buff, size_t size, size_t growAmount)
{
    auto start = buff;
    if (ptr + size > capacity) {
        do {
            if (auto av = Available()) {
                ::memcpy(buff, buffer + ptr, av);
                buff += av;
            }
            ptr = 0;
            Refill(growAmount ? growAmount : capacity);
            if (!capacity) {
                return size_t(buff - start);
            }
            auto left = capacity - ptr;
            auto min = (std::min)(size, left);
            ::memcpy(buff, buffer + ptr, min);
            size -= min;
            ptr += min;
            buff += min;
        } while (size);
        return size_t(buff - start);
    } else {
        ::memcpy(buff, buffer + ptr, size);
        ptr += size;
        return size;
    }
}

inline size_t In::Read(void *buff, size_t size, size_t growAmount)
{
    return Read(static_cast<char*>(buff), size, growAmount);
}

} //jv

#endif //MEMBUFF_HPP
