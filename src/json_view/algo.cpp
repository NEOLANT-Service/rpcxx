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
#include "json_view/pointer.hpp"
#include "meta/visit.hpp"
#include <map>

using namespace jv;

JsonView jv::Flatten(JsonView src, Arena& alloc, unsigned int depth)
{
    DepthError::Check(depth);
    src.AssertType(t_object);
    ArenaVector<JsonPair> result(alloc);
    DeepIterate(src, alloc,
        [&](JsonPointer ptr, JsonView item){
            result.push_back(JsonPair{CopyString(ptr.Join(), alloc), Copy(item, alloc)});
        },
        depth);
    return JsonView{result.data(), unsigned(result.size())};
}

namespace {
template<unsigned flags>
JsonView doCopy(JsonView src, Arena& alloc, unsigned int depth) {
    DepthError::Check(depth--);
    switch (src.GetType()) {
    case t_binary: {
        if (flags & NoCopyStrings) {
            return src;
        } else {
            return JsonView::Binary(CopyString(src.GetBinary(), alloc));
        }
    }
    case t_string: {
        if (flags & NoCopyStrings) {
            return src;
        } else {
            return JsonView(CopyString(src.GetString(), alloc));
        }
    }
    case t_array: {
        auto arr = MakeArrayOf(src.GetUnsafe().size, alloc);
        for (auto i = 0u; i < src.GetUnsafe().size; ++i) {
            arr[i] = doCopy<flags>(src.GetUnsafe().d.array[i], alloc, depth);
        }
        return JsonView{arr, src.GetUnsafe().size}.WithFlagsUnsafe(src.GetFlags());
    }
    case t_object: {
        auto obj = MakeObjectOf(src.GetUnsafe().size, alloc);
        for (auto i = 0u; i < src.GetUnsafe().size; ++i) {
            auto& curr = src.GetUnsafe().d.object[i];
            if (flags & NoCopyStrings) {
                obj[i].key = curr.key;
            } else {
                obj[i].key = CopyString(curr.key, alloc);
            }
            obj[i].value = doCopy<flags>(curr.value, alloc, depth);
        }
        return JsonView{obj, src.GetUnsafe().size}.WithFlagsUnsafe(src.GetFlags());
    }
    default: {
        return src;
    }
    }
}
}

JsonView jv::Copy(JsonView src, Arena& alloc, unsigned int depth, unsigned flags)
{
    switch (flags) {
    case NoCopyStrings: {
        return doCopy<NoCopyStrings>(src, alloc, depth);
    }
    case NoCopyBinary: {
        return doCopy<NoCopyBinary>(src, alloc, depth);
    }
    default: {
        return doCopy<0>(src, alloc, depth);
    }
    }
}

bool jv::DeepEqual(JsonView lhs, JsonView rhs, unsigned int depth, double margin)
{
    DepthError::Check(depth--);
    auto& data = lhs.GetUnsafe();
    auto& other = rhs.GetUnsafe();
    constexpr auto numType = t_signed | t_unsigned;
    switch (data.type) {
    case t_signed:
    case t_unsigned: {
        if (!(other.type & numType)) {
            if (other.type == t_number) {
                if (data.type == t_signed) {
                    return std::abs(other.d.number - data.d.integer) < margin;
                } else {
                    return std::abs(other.d.number - data.d.uinteger) < margin;
                }
            }
            return false;
        }
        if (data.d.integer == other.d.integer) {
            if (data.type != other.type) {
                // bits are same, but signess is not. same bitpattern may mean
                // that one is a big unsigned, while other is < 0
                // in such case both values as ints would be minus
                return data.d.integer >= 0 && other.d.integer >= 0;
            } else {
                return true;
            }
        } else {
            return false;
        }
    }
    case t_array: {
        if (other.type != data.type)
            return false;
        if (data.size != other.size)
            return false;
        for (auto i = 0u; i < data.size; ++i) {
            if (!DeepEqual(data.d.array[i], other.d.array[i], depth, margin))
                return false;
        }
        return true;
    }
    case t_number: {
        if (other.type != data.type) {
            if (other.type & numType) {
                if (other.type == t_signed) {
                    return std::abs(data.d.number - other.d.integer) < margin;
                } else {
                    return std::abs(data.d.number - other.d.uinteger) < margin;
                }
            }
            return false;
        }
        if (meta_Unlikely(std::isnan(data.d.number))) {
            return std::isnan(other.d.number);
        }
        if (meta_Unlikely(std::isinf(data.d.number))) {
            return std::isinf(other.d.number);
        }
        auto diff = std::abs(data.d.number - other.d.number);
        auto res = diff < margin;
        return res;
    }
    case t_object: {
        if (other.type != data.type)
            return false;
        if (data.size != other.size)
            return false;
        for (auto i = 0u; i < data.size; ++i) {
            if (data.d.object[i].key != other.d.object[i].key)
                return false;
            if (!DeepEqual(data.d.object[i].value, other.d.object[i].value, depth, margin)) {
                return false;
            }
        }
        return true;
    }
    case t_boolean: {
        if (other.type != data.type)
            return false;
        return data.d.boolean == other.d.boolean;
    }
    case t_binary:
    case t_string: {
        if (other.type != data.type)
            return false;
        if (data.size != other.size)
            return false;
        return lhs.GetStringUnsafe() == rhs.GetStringUnsafe();
    }
    default: {
        return other.type == data.type;
    }
    }
}
