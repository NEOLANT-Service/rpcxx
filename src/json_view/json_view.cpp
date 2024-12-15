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

#include "json_view/json_view.hpp"
#include "json_view/dump.hpp"
#include "json_view/json.hpp"
#include "json_view/pointer.hpp"
#include "json_view/algo.hpp"

using namespace jv;
using namespace std::string_view_literals;

const JsonView *JsonView::Find(const JsonPointer &ptr, const TraceFrame &frame) const {
    const JsonView* current = this;
    for (auto& part: ptr) {
        current = part.Visit([&](string_view key){
            return current->FindVal(key, TraceFrame(key, frame));
        }, [&](size_t idx){
            return current->Find(idx, TraceFrame(idx, frame));
        });
        if (!current)
            return nullptr;
    }
    return current;
}

std::string JsonView::Dump(bool pretty) const
{
    return jv::DumpJson(*this, {pretty});
}

std::string JsonView::DumpMsgPack() const
{
    return jv::DumpMsgPack(*this);
}

void JsonView::throwMissmatch(Type wanted, TraceFrame const& frame) const {
    TypeMissmatch exc(frame);
    exc.wanted = wanted;
    exc.was = data.type;
    throw exc;
}

void JsonView::throwKeyError(std::string_view key, TraceFrame const& frame) const
{
    KeyError err(frame);
    err.missing = std::string{key};
    throw err;
}

void JsonView::throwIndexError(unsigned int key, TraceFrame const& frame) const
{
    IndexError err(frame);
    err.actualSize = data.size;
    err.wanted = key;
    throw err;
}

void JsonView::throwIntRangeError(int64_t min, uint64_t max, TraceFrame const& frame) const
{
    IntRangeError err(frame);
    if (data.type == t_signed){
        err.isUnsigned = false;
        err.was.i = data.d.integer;
    } else if (data.type == t_unsigned) {
        err.isUnsigned = true;
        err.was.u = data.d.uinteger;
    }
    err.min = min;
    err.max = max;
    throw err;
}

JsonException::JsonException(const TraceFrame &frame) :
    trace{frame.PrintTrace()}
{
    if (!trace.empty())
        trace += ": ";
}

JsonException::JsonException(const JsonPointer &ptr) :
    trace{ptr.Join('.')}
{
    if (!trace.empty())
        trace += ": ";
}

ForeignError::ForeignError(std::string msg, TraceFrame const& frame) :
    JsonException(frame)
{
    nested = std::make_exception_ptr(std::runtime_error(std::move(msg)));
}

ForeignError::ForeignError(std::string msg, const JsonPointer &ptr) :
    JsonException(ptr)
{
    nested = std::make_exception_ptr(std::runtime_error(std::move(msg)));
}

const char *ForeignError::what() const noexcept try
{
    if (!msg.empty()) {
        return msg.c_str();
    }
    try {
        if (nested) {
            std::rethrow_exception(nested);
        } else {
            throw std::runtime_error("<exception not set>");
        }
    } catch (std::exception& e) {
        msg += trace + e.what();
    }
    return msg.c_str();
} catch (...) {
    return "Foreign Json Error (+ OOM)";
}


const char *KeyError::what() const noexcept try
{
    if (!msg.empty()) {
        return msg.c_str();
    }
    msg += trace;
    msg += "key not found: "sv;
    if (msg.empty()) {
        msg += std::to_string(idx);
    } else {
        msg += missing;
    }
    return msg.c_str();
} catch (...) {
    return "Json Key Error (+ OOM)";
}

const char *IndexError::what() const noexcept try
{
    if (!msg.empty()) {
        return msg.c_str();
    }
    msg += trace;
    msg += "index not found: "sv;
    msg += std::to_string(wanted);
    msg += " (was size: "sv;
    msg += std::to_string(actualSize) + ')';
    return msg.c_str();
} catch (...) {
    return "Json Index Error (+ OOM)";
}

static std::string printMask(jv::Type mask) {
    std::string msg;
    if (!mask)
        return "null";
    for (auto i = 1u; i <= t_discarded; i <<= 1) {
        if (auto single = mask & i) {
            if (!msg.empty()) {
                msg += '|';
            }
            msg += JsonView::PrintType(jv::Type(single));
        }
    }
    return msg;
}

const char *TypeMissmatch::what() const noexcept try
{
    if (!msg.empty()) {
        return msg.c_str();
    }
    msg += trace;
    msg += "type missmatch: (was: "sv;
    msg += std::string(JsonView::PrintType(was));
    msg += " => wanted: "sv;
    msg += printMask(wanted) + ')';
    return msg.c_str();
} catch (...) {
    return "Json Type Missmatch (+ OOM)";
}

const char *IntRangeError::what() const noexcept try
{
    if (!msg.empty()) {
        return msg.c_str();
    }
    msg += trace + "integer ";
    if (isUnsigned) {
        msg += std::to_string(was.u);
    } else {
        msg += std::to_string(was.i);
    }
    msg += " could not fit in range: ["sv;
    msg += std::to_string(min);
    msg += " - "sv;
    msg += std::to_string(max);
    msg += ']';
    return msg.c_str();
} catch (...) {
    return "Json Int Range Error (+ OOM)";
}
