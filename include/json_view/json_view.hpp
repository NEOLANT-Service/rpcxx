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

#ifndef JV_JSON_VIEW_HPP
#define JV_JSON_VIEW_HPP

#include "data.hpp"
#include "describe/describe.hpp"
#include "json_view/alloc.hpp"
#include "trace_frame.hpp"
#include "meta/meta.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <string.h>
#include <stdexcept>
#include <optional>

namespace jv
{

#ifndef JV_DEFAULT_DEPTH
#define JV_DEFAULT_DEPTH 300
#endif

using namespace meta;
using std::string_view;

//! Attributes

//! Treat all fields as optional
struct SkipMissing {};

//! Convert structs as tuples (arrays) of fields
struct StructAsTuple {};

struct FieldIndexBase {};
//! explicit field index (by default it is DESCRIBE() order)
template<unsigned idx>
struct FieldIndex : FieldIndexBase {
    static constexpr unsigned value = idx;
};
//! Convert enums not into strings, but into integers
struct EnumAsInteger {
    static constexpr bool validate = false;
};
//! Convert enums not into strings, but into integers (validate fits)
struct EnumAsIntegerValidate : EnumAsInteger {
    static constexpr bool validate = true;
};

struct Validator {static void validate(...) = delete;};

template<typename T>
struct ValidatedWith : Validator {
    template<typename U> static void validate(U& val) { T::validate(val); }
};
struct EnumFallbackBase {};
//! Do not throw exceptions on enum conversion errors, rather return fallback
template<auto val>
struct EnumFallback : EnumFallbackBase {
    static constexpr auto value = val;
};

using std::string_view;

struct JsonView;
struct JsonPointer;

template<typename T> using if_integral = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<bool, T>, int>;
template<typename T> using if_floating_point = std::enable_if_t<std::is_floating_point_v<T>, int>;
template<typename T> using if_string_like = std::enable_if_t<std::is_convertible_v<T, string_view>, int>;
template<typename T> using if_struct = std::enable_if_t<describe::is_described_struct_v<T>, int>;
template<typename T> using if_enum = std::enable_if_t<describe::is_described_enum_v<T>, int>;
template<typename T> using if_vector_like = std::enable_if_t<meta::is_index_container_v<T> && !std::is_constructible_v<string_view, T>, int>;
template<typename T> using if_map_like = std::enable_if_t<meta::is_assoc_container_v<T>, int>;

template<typename T, typename = void> struct Convert;

struct JsonView
{
    struct sorted_tag {};
    using value_type = JsonView;
    JsonView(Data d) noexcept : data(d) {}
    JsonView(std::nullptr_t = {}) noexcept {
        data.type = t_null;
    }
    explicit JsonView(bool val) noexcept {
        data.d.boolean = val;
        data.type = t_boolean;
    }
    JsonView(const char* str, int64_t len = -1) noexcept {
        if (len == -1) {
            len = unsigned(strlen(str));
        }
        data.size = unsigned(len);
        data.d.string = str;
        data.type = t_string;
    }
    JsonView(string_view str) noexcept {
        assert(str.size() <= (std::numeric_limits<unsigned>::max)());
        data.size = unsigned(str.size());
        data.d.string = str.data();
        data.type = t_string;
    }
    template<typename T, if_integral<T> = 1>
    JsonView(T val) noexcept {
        if constexpr (std::is_signed_v<T>) {
            data.d.integer = static_cast<int64_t>(val);
            data.type = t_signed;
        } else {
            data.d.uinteger = static_cast<uint64_t>(val);
            data.type = t_unsigned;
        }
    }
    template<typename T, if_floating_point<T> = 1>
    JsonView(T val) noexcept {
        data.d.number = static_cast<double>(val);
        data.type = t_number;
    }
    explicit JsonView(const JsonView* array, unsigned size) noexcept {
        data.size = size;
        data.d.array = array;
        data.type = t_array;
    }
    template<unsigned size>
    JsonView(const JsonView(&arr)[size]) noexcept :
        JsonView(arr, size)
    {}

    explicit JsonView(JsonPair* object, unsigned size) noexcept;
    explicit JsonView(const JsonPair* object, unsigned size, sorted_tag) noexcept {
        data.size = size;
        data.d.object = object;
        data.type = t_object;
    }

    template<unsigned size>
    JsonView(JsonPair(&obj)[size]) noexcept :
        JsonView(obj, size)
    {}
    template<unsigned size>
    JsonView(const JsonPair(&obj)[size], sorted_tag) noexcept :
        JsonView(obj, size, sorted_tag{})
    {}

    static JsonView Custom(void* data, unsigned size = 0) noexcept {
        Data res;
        res.size = size;
        res.d.custom = data;
        res.type = t_custom;
        return res;
    }
    static JsonView Binary(string_view data) noexcept {
        Data res;
        res.size = unsigned(data.size());
        res.d.binary = data.data();
        res.type = t_binary;
        return res;
    }
    static JsonView Discarded(string_view why = {}) noexcept {
        JsonView res = {why};
        res.data.type = t_discarded;
        return res;
    }
    bool Is(Type t) const noexcept {
        return t ? (data.type & t) : !data.type;
    }
    struct AsObj;
    struct AsArr;
    AsObj Object(bool check = true) const;
    AsArr Array(bool check = true) const;
    JsonView WithFlagsUnsafe(Flags flags) noexcept {
        data.flags = flags;
        return *this;
    }
    template<typename T>
    void GetTo(T& out, TraceFrame const& frame = {}) const {
        Convert<T>::DoFromJson(out, *this, frame);
    }
    template<typename T, typename Arena>
    static JsonView From(T const& object, Arena& alloc) {
        return Convert<T>::DoIntoJson(object, alloc);
    }
    template<typename T>
    std::decay_t<T> Get(TraceFrame const& frame = {}) const {
        std::decay_t<T> res;
        GetTo(res, frame);
        return res;
    }
    string_view GetString(TraceFrame const& frame = {}) const {
        AssertType(t_string, frame);
        return GetStringUnsafe();
    }
    string_view GetBinary(TraceFrame const& frame = {}) const {
        AssertType(t_binary, frame);
        return GetBinaryUnsafe();
    }
    string_view GetStringUnsafe() const {
        return string_view{data.d.string, data.size};
    }
    string_view GetBinaryUnsafe() const {
        return {static_cast<const char*>(data.d.binary), data.size};
    }
    string_view GetDiscardReason() const {
        AssertType(t_discarded);
        return {data.d.string, data.size};
    }
    const JsonView* Find(const JsonPointer& ptr, TraceFrame const& frame = {}) const;
    const JsonPair* Find(string_view key, TraceFrame const& frame = {}) const;
    const JsonView* FindVal(string_view key, TraceFrame const& frame = {}) const;
    const JsonView At(string_view key, TraceFrame const& frame = {}) const;
    const JsonView operator[](string_view key) const {return At(key, {});}
    const JsonView operator[](unsigned idx) const {return At(idx, {});}
    const JsonView At(unsigned idx, TraceFrame const& frame = {}) const {
        if (auto res = Find(idx, frame)) {
            return *res;
        } else {
            throwIndexError(idx, frame);
        }
    }
    const JsonView *Find(unsigned idx, TraceFrame const& frame = {}) const {
        AssertType(t_array, frame);
        if (meta_Unlikely(data.size <= idx))
            return nullptr;
        return data.d.array + idx;
    }
    template<typename U, typename T>
    U Value(const T& key, const U& adefault = {}, TraceFrame const& frame = {}) const {
        AssertType(t_object, frame);
        if (const JsonView* res = FindVal(key)) {
            return res->Get<U>(TraceFrame(key, frame));
        } else {
            return U{adefault};
        }
    }
    template<typename U>
    U Value(unsigned idx, const U& adefault = {}, TraceFrame const& frame = {}) const {
        AssertType(t_array);
        if (data.size > idx) {
            return data.d.array[idx].Get<U>(TraceFrame(idx, frame));
        } else {
            return U{adefault};
        }
    }
    template<typename T>
    string_view Value(const T& key, const char* adefault) const {
        return Value(key, string_view{adefault});
    }
    unsigned Size() const {
        constexpr Type sized = t_array | t_object | t_string | t_binary;
        AssertType(sized);
        return data.size;
    }
    string_view GetTypeName() const noexcept;
    Type GetType() const noexcept {return data.type;}
    Flags GetFlags() const noexcept {return data.flags;}
    bool HasFlag(Flags f) const noexcept {return data.flags & f;}
    void AssertType(Type wanted, TraceFrame const& frame = {}) const;
    static string_view PrintType(Type t) noexcept;
    std::string Dump(bool pretty = false) const;
    std::string DumpMsgPack() const;
    [[noreturn]] void throwMissmatch(Type wanted, TraceFrame const& frame = {}) const;
    [[noreturn]] void throwKeyError(string_view key, TraceFrame const& frame = {}) const;
    [[noreturn]] void throwIndexError(unsigned key, TraceFrame const& frame = {}) const;
    [[noreturn]] void throwIntRangeError(int64_t min, uint64_t max, TraceFrame const& frame = {}) const;
    const Data& GetUnsafe() const noexcept {
        return data;
    }
protected:
    Data data;
};

inline JsonView EmptyObject() {
    return JsonView{static_cast<JsonPair*>(nullptr), 0};
}

inline JsonView EmptyArray() {
    return JsonView{static_cast<JsonView*>(nullptr), 0};
}

struct JsonPair {
    std::string_view key;
    JsonView value;
};

struct KeyLess {
    bool operator()(const JsonPair& lhs, const JsonPair& rhs) const noexcept {
        return lhs.key < rhs.key;
    }
};

struct KeyEq {
    bool operator()(const JsonPair& lhs, const JsonPair& rhs) const noexcept {
        return lhs.key == rhs.key;
    }
};

struct JsonView::AsObj {
    AsObj(JsonView j, bool check = true) : j(j) {
        if (check) j.AssertType(t_object);
    }
    unsigned size() const noexcept {
        return j.GetUnsafe().size;
    }
    const JsonPair *begin() const noexcept {
        return j.GetUnsafe().d.object;
    }
    const JsonPair* end() const noexcept {
        return j.GetUnsafe().d.object + j.GetUnsafe().size;
    }
protected:
    JsonView j;
};

struct JsonView::AsArr {
    AsArr(JsonView j, bool check = true) : j(j) {
        if (check) j.AssertType(t_array);
    }
    unsigned size() const noexcept {
        return j.GetUnsafe().size;
    }
    const JsonView *begin() const noexcept {
        return j.GetUnsafe().d.array;
    }
    const JsonView* end() const noexcept {
        return j.GetUnsafe().d.array + j.GetUnsafe().size;
    }
protected:
    JsonView j;
};

inline JsonView::JsonView(JsonPair *object, unsigned int size) noexcept {
    std::sort(object, object + size, KeyLess{});
    auto newEnd = std::unique(object, object + size, KeyEq{});
    auto newSize = unsigned(newEnd - object);
    data.size = newSize;
    data.d.object = object;
    data.type = t_object;
}

inline typename JsonView::AsObj JsonView::Object(bool check) const {
    return {*this, check};
}

inline typename JsonView::AsArr JsonView::Array(bool check) const {
    return {*this, check};
}

[[nodiscard]]
inline std::string_view CopyString(std::string_view src, Arena& alloc) {
    if (src.empty()) return "";
    auto ptr = alloc(src.size(), alignof(char));
    if (meta_Unlikely(!ptr)) {
        throw std::bad_alloc{};
    }
    ::memcpy(ptr, src.data(), src.size());
    return {static_cast<const char*>(ptr), src.size()};
}

[[nodiscard]]
inline JsonView* MakeArrayOf(unsigned count, Arena& alloc) {
    if (!count) return nullptr;
    auto res = static_cast<JsonView*>(alloc(sizeof(JsonView) * count, alignof(JsonView)));
    if (meta_Unlikely(!res)) {
        throw std::bad_alloc{};
    }
    return res;
}

[[nodiscard]]
inline JsonPair* MakeObjectOf(unsigned count, Arena& alloc) {
    if (!count) return nullptr;
    auto res = static_cast<JsonPair*>(alloc(sizeof(JsonPair) * count, alignof(JsonPair)));
    if (meta_Unlikely(!res)) {
        throw std::bad_alloc{};
    }
    return res;
}

namespace detail {

inline const JsonPair* sortedFind(const JsonPair* object, unsigned len, string_view key) {
    auto first = object;
    while (len > 0) {
        auto half = len >> 1;
        auto middle = first + half;
        if (middle->key < key) {
            first = middle;
            ++first;
            len = len - half - 1;
        } else {
            if (middle->key == key) {
                return middle;
            }
            len = half;
        }
    }
    return nullptr;
}
}

inline const JsonPair* JsonView::Find(string_view key, TraceFrame const& frame) const {
    AssertType(t_object, frame);
    return detail::sortedFind(data.d.object, data.size, key);
}

inline const JsonView* JsonView::FindVal(string_view key, TraceFrame const& frame) const {
    if (auto res = Find(key, frame)) {
        return &res->value;
    } else {
        return nullptr;
    }
}

inline const JsonView JsonView::At(string_view key, TraceFrame const& frame) const {
    if (auto res = Find(key, frame)) {
        return res->value;
    } else {
        throwKeyError(key, frame);
    }
}

inline string_view JsonView::GetTypeName() const noexcept {
    return PrintType(data.type);
}


inline void JsonView::AssertType(Type wanted, const TraceFrame &frame) const {
    bool ok = Is(wanted);
    if (meta_Unlikely(!ok)) {
        throwMissmatch(wanted, frame);
    }
}

inline string_view JsonView::PrintType(Type t) noexcept {
    switch (t) {
    case t_array: return std::string_view("array");
    case t_string: return std::string_view("string");
    case t_object: return std::string_view("object");
    case t_null: return std::string_view("null");
    case t_signed: return std::string_view("signed");
    case t_boolean: return std::string_view("boolean");
    case t_unsigned: return std::string_view("unsigned");
    case t_binary: return std::string_view("binary");
    case t_discarded: return std::string_view("discarded");
    case t_number: return std::string_view("number");
    case t_custom: return std::string_view("custom");
    default: return std::string_view("<invalid>");
    }
}

struct DepthError : std::exception {
    DepthError() noexcept {}
    const char* what() const noexcept {
        return "Json is too deep";
    }
    meta_alwaysInline static void Check(unsigned depth) {
        if (meta_Unlikely(!depth)) {
            throw DepthError{};
        }
    }
};

struct JsonException : std::exception
{
    JsonException(TraceFrame const& frame = {});
    JsonException(JsonPointer const& ptr);
    void SetTrace(JsonPointer const& ptr);
    void SetTrace(TraceFrame const& frame);
    std::string trace;
protected:
    mutable std::string msg;
};

struct ForeignError : JsonException
{
    using JsonException::JsonException;
    ForeignError(std::string msg, TraceFrame const& frame = {});
    ForeignError(std::string msg, JsonPointer const& ptr);
    std::exception_ptr nested;
    const char* what() const noexcept override;
};

struct KeyError : JsonException
{
    using JsonException::JsonException;
    std::string missing;
    const char* what() const noexcept override;
};

struct IndexError : JsonException
{
    using JsonException::JsonException;
    unsigned wanted;
    unsigned actualSize;
    const char* what() const noexcept override;
};

struct TypeMissmatch : JsonException
{
    using JsonException::JsonException;
    Type wanted;
    Type was;
    const char* what() const noexcept override;
};

struct IntRangeError : JsonException
{
    using JsonException::JsonException;
    bool isUnsigned = false;
    union {
        int64_t i;
        uint64_t u = 0;
    } was;
    int64_t min = 0;
    uint64_t max = 0;
    const char* what() const noexcept override;
};

template<typename T>
struct is_optional : std::false_type {};
template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};

namespace detail {

template<typename From, typename To>
struct is_lossless {
    static constexpr auto same_sign = std::is_signed_v<From> == std::is_signed_v<To>;
    static constexpr auto value = same_sign && sizeof(To) >= sizeof(From);
};

template<typename To, typename FromT>
To intChecked(JsonView j, FromT our, TraceFrame const& frame) noexcept(is_lossless<FromT, To>::value);

template<typename T> void deserializeFields(T& obj, JsonView json, TraceFrame const& frame);
template<typename T> void deserializeFromTuple(T& obj, JsonView json, TraceFrame const& frame);
template<typename T> JsonView serializeAsTuple(T const& value, Arena& alloc);
template<typename Validator, typename T> void runValidator(T& output, TraceFrame const& next);

} //detail

/// Conversions
template<typename T, typename>
struct Convert
{
    static JsonView DoIntoJson(T const& value, Arena& ctx) {
        constexpr bool trivial = std::is_arithmetic_v<T>
                                 || std::is_same_v<T, JsonView>
                                 || std::is_integral_v<T>
                                 || std::is_convertible_v<T, string_view>;
        if constexpr (trivial) {
            (void)ctx;
            return JsonView{value};
        } else if constexpr(describe::is_described_struct_v<T>) {
            if constexpr (describe::has_v<StructAsTuple, T>) {
                return detail::serializeAsTuple(value, ctx);
            } else {
                constexpr auto desc = describe::Get<T>();
                constexpr auto size = describe::fields_count<T>();
                auto obj = MakeObjectOf(size, ctx);
                unsigned count = 0;
                desc.for_each([&](auto f) {
                    if constexpr (f.is_field) {
                        auto entry = JsonPair{f.name, JsonView::From(f.get(value), ctx)};
                        count = SortedInsertJson(obj, count, entry, size);
                    }
                });
                Data result;
                result.type = t_object;
                result.size = count;
                result.d.object = obj;
                return JsonView(result);
            }
        } else if constexpr(describe::is_described_enum_v<T>) {
            if constexpr (describe::has_v<EnumAsInteger, T>) {
                return JsonView(std::underlying_type_t<T>(value));
            } else {
                string_view name;
                using fallback = describe::extract_t<EnumFallbackBase, T>;
                if (!describe::enum_to_name(value, name)) {
                    if constexpr (std::is_void_v<fallback>) {
                        throw std::runtime_error(
                            "invalid enum value for '" + std::string{describe::Get<T>().name}
                            + "': "+std::to_string(std::underlying_type_t<T>(value)));
                    } else {
                        (void)describe::enum_to_name(T(fallback::value), name);
                    }
                }
                return name;
            }
        } else {
            return IntoJson(value, ctx);
        }
    }
    static void DoFromJson(T& value, JsonView json, TraceFrame const& frame) {
        if constexpr (std::is_same_v<T, JsonView>) {
            value = json; (void)frame;
        } else if constexpr (std::is_same_v<T, bool>) {
            json.AssertType(t_boolean, frame);
            value = json.GetUnsafe().d.boolean;
        } else if constexpr (std::is_arithmetic_v<T>) {
            if constexpr (std::is_floating_point_v<T>) {
                switch(json.GetType()) {
                case t_signed: {
                    value = static_cast<T>(json.GetUnsafe().d.integer);
                    break;
                }
                case t_unsigned: {
                    value = static_cast<T>(json.GetUnsafe().d.uinteger);
                    break;
                }
                case t_number: {
                    value = static_cast<T>(json.GetUnsafe().d.number);
                    break;
                }
                default: {
                    json.throwMissmatch(t_any_number, frame);
                }
                }
            } else {
                switch(json.GetType()) {
                case t_signed: {
                    value = detail::intChecked<T>(json, json.GetUnsafe().d.integer, frame);
                    break;
                }
                case t_unsigned: {
                    value = detail::intChecked<T>(json, json.GetUnsafe().d.uinteger, frame);
                    break;
                }
                default: {
                    json.throwMissmatch(t_any_integer, frame);
                }
                }
            }
        } else if constexpr (std::is_convertible_v<T, std::string_view>) {
            json.AssertType(t_string, frame);
            value = static_cast<std::decay_t<T>>(json.GetStringUnsafe());
        } else if constexpr (describe::is_described_struct_v<T>) {
            if constexpr (describe::has_v<StructAsTuple, T>) {
                json.AssertType(t_array, frame);
                detail::deserializeFromTuple(value, json, frame);
            } else {
                json.AssertType(t_object, frame);
                detail::deserializeFields(value, json, frame);
            }
            using validator = describe::extract_t<Validator, T>;
            detail::runValidator<validator>(value, TraceFrame(describe::Get<T>().name, frame));
        } else if constexpr (describe::is_described_enum_v<T>) {
            using as_int = describe::extract_t<EnumAsInteger, T>;
            if constexpr (!std::is_void_v<as_int>) {
                auto asUnder = json.Get<std::underlying_type_t<T>>(frame);
                if constexpr (as_int::validate) {
                    bool ok = false;
                    describe::Get<T>().for_each([&](auto f){
                        if constexpr (f.is_field) {
                            if (!ok && asUnder == f.value) {
                                ok = true;
                            }
                        }
                    });
                    if (!ok) {
                        auto msg = "invalid integer for enum '"+std::string{describe::Get<T>().name} +"': "+std::to_string(asUnder);
                        throw ForeignError(std::move(msg), frame);
                    }
                }
                value = T(asUnder);
            } else {
                auto name = json.Get<string_view>(frame);
                using fallback = describe::extract_t<EnumFallbackBase, T>;
                if (!describe::name_to_enum(name, value)) {
                    if constexpr (std::is_void_v<fallback>) {
                        auto msg = "invalid string for enum '"
                                   + std::string{describe::Get<T>().name}
                                   + "': " + std::string{name};
                        throw ForeignError(std::move(msg), frame);
                    } else {
                        value = fallback::value;
                    }
                }
            }
        } else {
            FromJson(value, json, frame);
        }
    }
};

namespace detail {

template<typename Tuple, size_t...Is>
void impl_into(JsonView* arr, const Tuple& object, Arena& ctx, std::index_sequence<Is...>) {
    ((void)(arr[Is] = JsonView::From(std::get<Is>(object), ctx)), ...);
}
template<typename Tuple, size_t...Is>
void impl_from(const JsonView* source, Tuple& out, TraceFrame const& frame, std::index_sequence<Is...>) {
    ((void)(source[Is].GetTo(std::get<Is>(out), TraceFrame(Is, frame))), ...);
}

}

template<typename...Ts>
struct Convert<std::tuple<Ts...>>
{
    static JsonView DoIntoJson(std::tuple<Ts...> const& value, Arena& ctx) {
        constexpr unsigned count = sizeof...(Ts);
        auto arr = static_cast<JsonView*>(ctx(sizeof(JsonView) * count));
        detail::impl_into(arr, value, ctx, std::make_index_sequence<count>());
        return JsonView{arr, count};
    }
    static void DoFromJson(std::tuple<Ts...>& value, JsonView json, TraceFrame const& frame) {
        constexpr unsigned count = sizeof...(Ts);
        json.AssertType(t_array, frame);
        if (json.GetUnsafe().size <= count) {
            json.throwIndexError(count, frame);
        }
        detail::impl_from(json.GetUnsafe().d.array, value, frame, std::make_index_sequence<count>());
    }
};

template<typename T>
struct Convert<std::optional<T>>
{
    static JsonView DoIntoJson(std::optional<T> const& value, Arena& ctx) {
        return value ? JsonView::From(*value, ctx) : JsonView(nullptr);
    }
    static void DoFromJson(std::optional<T>& value, JsonView json, TraceFrame const& frame) {
        if (json.Is(t_null)) {
            value.reset();
        } else {
            json.GetTo(value.emplace(), frame);
        }
    }
};

template<typename T, if_vector_like<T> = 1>
JsonView IntoJson(T const& value, Arena& ctx) {
    auto arr = MakeArrayOf(unsigned(value.size()), ctx);
    unsigned count = 0;
    for (auto& v: value) {
        arr[count++] = JsonView::From(v, ctx);
    }
    return JsonView(arr, count);
}

template<typename T, if_vector_like<T> = 1>
void FromJson(T& out, JsonView json, TraceFrame const& frame) {
    json.AssertType(t_array, frame);
    out.clear();
    unsigned count = 0;
    for (JsonView i: json.Array(false)) {
        i.GetTo(out.emplace_back(), TraceFrame(count++, frame));
    }
}

template<typename T, if_map_like<T> = 1>
JsonView IntoJson(T const& value, Arena& ctx) {
    auto obj = MakeObjectOf(unsigned(value.size()), ctx);
    unsigned count = 0;
    for (auto& [k, v]: value) {
        auto& current = obj[count++];
        current.key = string_view{k};
        current.value = JsonView::From(v, ctx);
    }
    return JsonView(obj, count);
}

template<typename T, if_map_like<T> = 1>
void FromJson(T& out, JsonView json, TraceFrame const& frame) {
    json.AssertType(t_object, frame);
    out.clear();
    for (auto [k, v]: json.Object(false)) {
        v.GetTo(out[typename T::key_type{k}], TraceFrame(k, frame));
    }
}

template<typename T, typename U>
JsonView IntoJson(std::pair<T, U> const& value, Arena& ctx) {
    auto arr = MakeArrayOf(2, ctx);
    arr[0] = JsonView::From(value.first, ctx);
    arr[1] = JsonView::From(value.second, ctx);
    return JsonView(arr, 2);
}

template<typename T, typename U>
void FromJson(std::pair<T, U>& out, JsonView json, TraceFrame const& frame) {
    json.AssertType(t_array, frame);
    if (auto sz = json.GetUnsafe().size; sz < 2) {
        json.throwIndexError(2, frame);
    }
    json.GetUnsafe().d.array[0].GetTo(out.first, TraceFrame(0, frame));
    json.GetUnsafe().d.array[1].GetTo(out.second, TraceFrame(1, frame));
}

struct StorageRequirements {
    unsigned objects;
};

template<typename T>
constexpr void ComputeStaticStorage(StorageRequirements& out) {
    describe::Get<T>::for_each([&](auto f){
        if constexpr (f.is_field) {
            using F = decltype(f);
            using FT = typename F::type;
            out.objects += 1;
            if constexpr (describe::is_described_struct_v<FT>) {
                ComputeStaticStorage<FT>(out);
            }
        }
    });
}

template<typename T>
constexpr StorageRequirements ComputeStaticStorage() {
    StorageRequirements res{};
    ComputeStaticStorage<T>(res);
    return res;
}

namespace detail
{

template<typename T>
JsonView staticView(T const& obj, JsonPair* storage, unsigned& consumed) {
    (void)consumed;
    if constexpr (describe::is_described_struct_v<T>) {
        constexpr auto total = describe::fields_count<T>();
        unsigned count = 0;
        unsigned current_consumed = 0;
        describe::Get<T>::for_each([&](auto f) {
            if constexpr (f.is_field) {
                auto& curr = storage[count++];
                curr.key = f.name;
                curr.value = staticView(f.get(obj), storage + total + current_consumed, current_consumed);
            }
        });
        consumed += total + current_consumed;
        return JsonView(storage, total);
    } else if constexpr (std::is_constructible_v<JsonView, T>) {
        return JsonView(obj);
    } else {
        NullArena null;
        return JsonView::From(obj, null);
    }
}

}

template<typename T, typename = std::enable_if_t<describe::is_described_struct_v<T>>>
struct StaticJsonView {
    StaticJsonView() noexcept = default;
    void Setup(T const& obj) {
        unsigned consumed = 0;
        JsonPair* out = storage;
        root = detail::staticView(obj, out, consumed);
    }
    StaticJsonView(StaticJsonView&&) = delete;
    JsonView View() const noexcept {
        return root;
    }
protected:
    JsonView root;
    static constexpr StorageRequirements reqs = ComputeStaticStorage<T>();
    JsonPair storage[reqs.objects ? reqs.objects : 1];
};

namespace detail {
struct fieldHelper {
    string_view name;
    bool hit = {};
    bool required = {};
};

template<typename F>
constexpr bool isRequired() {
    constexpr bool is_opt = describe::has_v<SkipMissing, typename F::cls>
                            || is_optional<typename F::type>::value;
    return !is_opt;
}

template<typename Cls>
constexpr auto prepFields() {
    constexpr auto desc = describe::Get<Cls>();
    std::array<fieldHelper, desc.fields_count> res = {};
    size_t idx = 0;
    desc.for_each_field([&](auto field){
        auto curr = res[idx++];
        curr.name = field.name;
        curr.hit = false;
        curr.required = isRequired<decltype(field)>();
    });
    return res;
}

template<typename Validator, typename T>
void runValidator(T& output, TraceFrame const& next) {
    if constexpr (!std::is_void_v<Validator>) {
        try {
            Validator::validate(output);
        } catch (...) {
            ForeignError exc(next);
            exc.nested = std::current_exception();
            throw exc;
        }
    }
}

inline JsonView tupleGet(bool required, unsigned idx, const JsonView* arr, unsigned sz, TraceFrame const& frame) {
    if (sz <= idx) {
        if (required) {
            IndexError err(frame);
            err.actualSize = sz;
            err.wanted = idx;
            throw err;
        } else {
            return JsonView();
        }
    }
    return arr[idx];
}

template<typename T>
void deserializeFromTuple(T& obj, JsonView json, TraceFrame const& frame) {
    constexpr auto desc = describe::Get<T>();
    unsigned count = 0;
    auto arr = json.GetUnsafe().d.array;
    auto sz = json.GetUnsafe().size;
    desc.for_each([&](auto f) {
        if constexpr (f.is_field) {
            using F = decltype(f);
            using Idx = describe::extract_t<FieldIndexBase, F>;
            unsigned index;
            if constexpr (!std::is_void_v<Idx>) index = Idx::value;
            else index = count;
            auto& output = f.get(obj);
            auto src = tupleGet(isRequired<F>(), index, arr, sz, frame);
            TraceFrame fieldFrame(f.name, frame);
            src.GetTo(output, fieldFrame);
            using validator = describe::extract_t<Validator, F>;
            runValidator<validator>(output, fieldFrame);
            count++;
        }
    });
}

template<typename Field>
constexpr unsigned getIdxFor() {
    using Explicit = describe::extract_t<FieldIndexBase, Field>;
    if constexpr (std::is_void_v<Explicit>) {
        return 0;
    } else {
        return Explicit::value;
    }
}

template<typename T>
constexpr unsigned maxIdxFor() {
    unsigned result = describe::fields_count<T>();
    describe::Get<T>::for_each([&](auto f){
        if constexpr (f.is_field) {
            using F = decltype(f);
            using Explicit = describe::extract_t<FieldIndexBase, F>;
            if constexpr (!std::is_void_v<Explicit>) {
                if (Explicit::value > result) {
                    result = Explicit::value;
                }
            }
        }
    });
    return result;
}

template<typename T>
JsonView serializeAsTuple(const T &value, Arena &alloc)
{
    constexpr auto desc = describe::Get<T>();
    constexpr auto total = maxIdxFor<T>();
    auto arr = MakeArrayOf(total, alloc);
    unsigned count = 0;
    desc.for_each([&](auto f){
        if constexpr (f.is_field) {
            using F = decltype(f);
            constexpr auto manual = getIdxFor<F>();
            auto idx = manual ? manual : count;
            arr[idx] = JsonView::From(f.get(value), alloc);
            count++;
        }
    });
    return JsonView(arr, total);
}


template<typename T>
void deserializeFields(T& obj, JsonView json, TraceFrame const& frame) {
    constexpr auto desc = describe::Get<T>();
    desc.for_each([&](auto field){
        if constexpr (field.is_field) {
            using F = decltype(field);
            auto& output = field.get(obj);
            auto next = TraceFrame(field.name, frame);
            if constexpr (isRequired<F>()) {
                json.At(field.name, frame).GetTo(output, next);
            } else {
                if (auto f = json.FindVal(field.name, frame)) {
                    f->GetTo(output, next);
                }
            }
            using validator = describe::extract_t<Validator, F>;
            runValidator<validator>(output, next);
        }
    });
}

template<typename To, typename FromT>
inline To intChecked(JsonView j, FromT our, TraceFrame const& frame)
    noexcept(is_lossless<FromT, To>::value)
{
    (void)j;
    using is_lossless = is_lossless<FromT, To>;
    if constexpr (!is_lossless::value) {
        constexpr auto _min = (std::numeric_limits<To>::min)();
        constexpr auto _max = (std::numeric_limits<To>::max)();
        auto fail = [&]{
            j.throwIntRangeError(_min, _max, frame);
        };
        if constexpr (is_lossless::same_sign) {
            if (meta_Unlikely(our < _min || _max < our)) fail();
        } else if constexpr (std::is_signed_v<FromT>) { //TO is unsigned
            if constexpr ((std::numeric_limits<FromT>::max)() > _max) {
                if (meta_Unlikely(our < 0 || our > _max)) fail();
            } else {
                if (meta_Unlikely(our < 0)) fail();
            }
        } else /*signed To*/ {
            if (meta_Unlikely(our > FromT(_max))) fail();
        }
    }
    return static_cast<To>(our);
}

} //detail


} //jv

#endif // JV_JSON_VIEW_HPP
