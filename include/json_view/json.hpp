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
#ifndef JV_JSON_HPP
#define JV_JSON_HPP

#include "json_view/parse.hpp"
#include "json_view.hpp"
#include "algo.hpp"
#include "pointer.hpp"
#include <map>


namespace jv {

template<typename Config>
struct JsonConfigTraits {
    static constexpr bool ObjectSorted = false;
};

struct JsonConfig;

struct JsonConfig {
    template<typename T>
    using Allocator = std::allocator<T>;
    template<typename V>
    using Object = std::map<std::string, V, std::less<>>;
    template<typename V>
    using Array = std::vector<V>;
    using String = std::string;
    using Binary = std::vector<char>;
};

template<>
struct JsonConfigTraits<JsonConfig> {
    static constexpr bool ObjectSorted = true;
};

template<typename Config = JsonConfig>
struct BasicMutJson {
    template<typename T>
    using Alloc = typename Config::template Allocator<T>;
    struct Value;
    using Array = typename Config::template Array<BasicMutJson>;
    using Object = typename Config::template Object<BasicMutJson>;
    using Key_t = typename Object::key_type;
    using String = typename Config::String;
    using Binary = typename Config::Binary;

    struct Value {
        Type type = t_null;
        union {
            Array* arr = {};
            Object* obj;
            String* str;
            Binary* bin;
            double number;
            int64_t integer;
            uint64_t uinteger;
            bool boolean;
        };
    };
    BasicMutJson() noexcept = default;
    Type GetType() const noexcept {
        return data.type;
    }
    BasicMutJson(double number) noexcept {
        data.type = t_number;
        data.number = number;
    }
    template<typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 1>
    BasicMutJson(T num) noexcept {
        if constexpr (std::is_signed_v<T>) {
            data.type = t_signed;
            data.integer = int64_t(num);
        } else {
            data.type = t_unsigned;
            data.uinteger = uint64_t(num);
        }
    }
    BasicMutJson(Array&& v) noexcept : BasicMutJson(t_array) {
        GetArray() = std::move(v);
    }
    BasicMutJson(Object&& v) noexcept : BasicMutJson(t_object) {
        GetObject() = std::move(v);
    }
    BasicMutJson(String&& v) noexcept : BasicMutJson(t_string) {
        GetString() = std::move(v);
    }
    BasicMutJson(Binary&& v) noexcept : BasicMutJson(t_binary) {
        GetBinary() = std::move(v);
    }
    BasicMutJson(Array const& v, unsigned depth = JV_DEFAULT_DEPTH) noexcept : BasicMutJson(t_array) {
        DepthError::Check(depth--);
        auto& out = GetArray();
        out.reserve(v.size());
        for (auto& i: v) {
            copy(out.emlace_back(), i, depth);
        }
    }
    BasicMutJson(Object const& v, unsigned depth = JV_DEFAULT_DEPTH) noexcept : BasicMutJson(t_object) {
        DepthError::Check(depth--);
        auto& out = GetObject();
        auto hint = out.begin();
        for (auto& it: v) {
            hint = out.emplace_hint(hint, Key_t{it.first}, BasicMutJson(it.second, depth));
        }
    }
    BasicMutJson(String const& v) noexcept : BasicMutJson(t_string) {
        GetString() = v;
    }
    BasicMutJson(Binary const& v) noexcept : BasicMutJson(t_binary) {
        GetBinary() = v;
    }
    BasicMutJson& operator[](string_view key) {
        if (Is(t_null)) {
            *this = {t_object};
        }
        AssertType(t_object);
        return (*data.obj)[Key_t{key}];
    }
    BasicMutJson& operator[](unsigned idx) {
        AssertType(t_array);
        return (*data.arr).at(idx);
    }
    BasicMutJson(string_view str) : BasicMutJson(t_string) {
        (*data.str) = String(str);
    }
    BasicMutJson(const char* str, int64_t len = -1) : BasicMutJson(t_string) {
        (*data.str) = String(str, len == -1 ? strlen(str) : size_t(len));
    }
    BasicMutJson(std::nullptr_t) noexcept : BasicMutJson(t_null) {}
    explicit BasicMutJson(bool boolean) noexcept {
        data.type = t_boolean;
        data.number = boolean;
    }
    bool& GetBool(TraceFrame const& frame = {}) {
        AssertType(t_boolean, frame);
        return data.boolean;
    }
    bool GetBool(TraceFrame const& frame = {}) const {
        AssertType(t_boolean, frame);
        return data.boolean;
    }
    int64_t& GetInt(TraceFrame const& frame = {}) {
        AssertType(t_signed, frame);
        return data.integer;
    }
    int64_t GetInt(TraceFrame const& frame = {}) const {
        AssertType(t_signed, frame);
        return data.integer;
    }
    uint64_t& GetUint(TraceFrame const& frame = {}) {
        AssertType(t_unsigned, frame);
        return data.uinteger;
    }
    uint64_t GetUint(TraceFrame const& frame = {}) const {
        AssertType(t_unsigned, frame);
        return data.uinteger;
    }
    String& GetString(TraceFrame const& frame = {}) {
        AssertType(t_string, frame);
        return *data.str;
    }
    const String& GetString(TraceFrame const& frame = {}) const {
        AssertType(t_string, frame);
        return *data.str;
    }
    Binary& GetBinary(TraceFrame const& frame = {}) {
        AssertType(t_binary, frame);
        return *data.bin;
    }
    const Binary& GetBinary(TraceFrame const& frame = {}) const {
        AssertType(t_binary, frame);
        return *data.bin;
    }
    Array& GetArray(TraceFrame const& frame = {}) {
        AssertType(t_array, frame);
        return *data.arr;
    }
    const Array& GetArray(TraceFrame const& frame = {}) const {
        AssertType(t_array, frame);
        return *data.arr;
    }
    Object& GetObject(TraceFrame const& frame = {}) {
        AssertType(t_object, frame);
        return *data.obj;
    }
    const Object& GetObject(TraceFrame const& frame = {}) const {
        AssertType(t_object, frame);
        return *data.obj;
    }
    BasicMutJson(Type t) {
        switch (t) {
        case t_array:
            init(t, data.arr);
            break;
        case t_object:
            init(t, data.obj);
            break;
        case t_string:
            init(t, data.str);
            break;
        case t_binary:
            init(t, data.bin);
            break;
        default: {
            data = {};
            data.type = t;
        }
        }
    }
    BasicMutJson(BasicMutJson&& o) noexcept {
        data = o.data;
        o.data.type = t_null;
    }
    bool Is(Type t) const noexcept {
        return t ? data.type & t : data.type == t;
    }
    BasicMutJson& operator=(BasicMutJson&& o) noexcept {
        if (this != &o) {
            DefaultArena arena;
            Destroy(arena);
            data = o.data;
            o.data.type = t_null;
        }
        return *this;
    }
    BasicMutJson Copy(unsigned depth = JV_DEFAULT_DEPTH) const {
        return BasicMutJson{*this, depth};
    }
    BasicMutJson& Assign(JsonPointer ptr, TraceFrame const& frame = {});
    void Destroy(Arena& alloc);
    ~BasicMutJson() noexcept(false) {
        DefaultArena arena;
        Destroy(arena);
    }
    void AssertType(Type wanted, const TraceFrame &frame = {}) const {
        bool ok = Is(wanted);
        if (meta_Unlikely(!ok)) {
            TypeMissmatch exc(frame);
            exc.wanted = wanted;
            exc.was = data.type;
            throw exc;
        }
    }
    Value DataUnsafe() const noexcept {
        return data;
    }
    Value TakeUnsafe() noexcept {
        return std::exchange(data, Value{});
    }
    explicit BasicMutJson(Value v) noexcept : data(v) {}
    explicit BasicMutJson(JsonView source, unsigned depth = JV_DEFAULT_DEPTH) {
        copy(*this, source, depth);
    }
    [[nodiscard]]
    JsonView View(Arena& alloc, unsigned depth = JV_DEFAULT_DEPTH) const;
protected:
    template<typename T>
    void init(Type t, T*& o) {
        Alloc<T> ownAlloc;
        using Traits = std::allocator_traits<Alloc<T>>;
        o = Traits::allocate(ownAlloc, 1);
        if constexpr (std::is_nothrow_constructible_v<T>) {
            Traits::construct(ownAlloc, o);
        } else {
            try {Traits::construct(ownAlloc, o);}
            catch (...) {
                Traits::deallocate(ownAlloc, o, 1);
                o = nullptr;
                throw;
            }
        }
        data.type = t;
    }
    template<typename T>
    void disposeOf(T* o) noexcept {
        assert(o);
        Alloc<T> ownAlloc;
        using Traits = std::allocator_traits<Alloc<T>>;
        Traits::destroy(ownAlloc, o);
        Traits::deallocate(ownAlloc, o, 1);
    }
    static void copy(BasicMutJson& to, JsonView src, unsigned depth);
    static void copy(Array& to, Array const& from, unsigned depth);
    static void copy(Object& to, Object const& from, unsigned depth);
    static void copy(BasicMutJson& to, BasicMutJson const& from, unsigned depth);
    BasicMutJson(const BasicMutJson& other, unsigned depth) {
        copy(*this, other, depth);
    }
    Value data;
};

//mutable and persistent version of json
using MutableJson = BasicMutJson<>;

//persistent but immutable version of json
struct Json {
    Json() noexcept = default;
    explicit Json(JsonView source, unsigned depth = JV_DEFAULT_DEPTH) :
        alloc(512)
    {
        view = Copy(source, alloc, depth);
    }
    Json(const Json& o) {
        view = Copy(o.view, alloc);
    }
    Json& operator=(const Json& o) {
        if (this != &o) {
            view = Copy(o.view, alloc);
        }
        return *this;
    }
    const JsonView operator[](unsigned idx) {
        return view[idx];
    }
    const JsonView operator[](string_view key) {
        return view[key];
    }
    template<typename T> [[nodiscard]]
    static Json From(T const& object) {
        Json res;
        res.view = JsonView::From(object, res.alloc);
        return res;
    }
    [[nodiscard]]
    static Json ParseFile(std::filesystem::path path, unsigned depth = JV_DEFAULT_DEPTH) {
        Json res;
        res.view = ParseJsonFile(path, res.alloc, {depth});
        return res;
    }
    [[nodiscard]]
    static Json Parse(string_view json, unsigned depth = JV_DEFAULT_DEPTH) {
        Json res;
        res.view = ParseJson(json, res.alloc, {depth});
        return res;
    }
    [[nodiscard]]
    static Json FromMsgPack(string_view msgpack, unsigned depth = JV_DEFAULT_DEPTH) {
        Json res;
        res.view = ParseMsgPack(msgpack, res.alloc, {depth});
        return res;
    }
    template<typename Fn> [[nodiscard]]
    static Json FromInit(Fn&& f) {
        Json res;
        res.view = JsonView(f(res.alloc));
        return res;
    }
    const JsonView* operator->() const noexcept {
        return &view;
    }
    Json(Json&&) noexcept = default;
    Json& operator=(Json&&) noexcept = default;
    JsonView View() const noexcept {
        return view;
    }
protected:
    JsonView view;
    DefaultArena<0> alloc;
};

template<typename Config>
void MergePatch(BasicMutJson<Config>& target, JsonView patch, unsigned depth = JV_DEFAULT_DEPTH)
{
    DepthError::Check(depth--);
    if (patch.Is(t_object)) {
        if (!target.Is(t_object)) {
            target = {t_object};
        }
        auto& result = *target.DataUnsafe().obj;
        for (auto& pair: patch.Object()) {
            if (pair.value.Is(t_null)) {
                if (auto it = result.find(pair.key); it != result.end()) {
                    result.erase(it);
                }
            } else if (auto it = result.find(pair.key); it != result.end()) {
                MergePatch(it->second, pair.value, depth);
            } else {
                auto pos = result.emplace_hint(it, pair.key, BasicMutJson<Config>(pair.value, depth));
                MergePatch(pos->second, pair.value, depth);
            }
        }
    } else {
        target = BasicMutJson<Config>{patch, depth};
    }
}

template<typename Config>
void Unflatten(BasicMutJson<Config>& result, JsonView flat, TraceFrame const& frame = {}, unsigned depth = JV_DEFAULT_DEPTH) {
    for (auto [k, v]: flat.Object()) {
        DefaultArena alloc;
        auto ptr = JsonPointer::FromString(k, alloc);
        if (ptr.size > depth) {
            throw DepthError{};
        }
        result.Assign(ptr, frame) = BasicMutJson<Config>(v, depth - ptr.size);
    }
}

template<typename Config>
struct Convert<BasicMutJson<Config>> {
    static JsonView DoIntoJson(BasicMutJson<Config> const& value, Arena& alloc) {
        return value.View(alloc);
    }
    static void DoFromJson(BasicMutJson<Config>& out, JsonView json, TraceFrame const&) {
        out = BasicMutJson<Config>{json};
    }
};

template<>
struct Convert<Json> {
    static JsonView DoIntoJson(Json const& value, Arena&) {
        return value.View();
    }
    static void DoFromJson(Json& out, JsonView json, TraceFrame const&) {
        out = Json{json};
    }
};

template<typename Config>
JsonView BasicMutJson<Config>::View(Arena& alloc, unsigned depth) const {
    DepthError::Check(depth--);
    switch (data.type) {
    case t_number: {
        return data.number;
    }
    case t_signed: {
        return data.integer;
    }
    case t_unsigned: {
        return data.uinteger;
    }
    case t_boolean: {
        return JsonView(data.boolean);
    }
    case t_array: {
        auto sz = unsigned(data.arr->size());
        auto arr = MakeArrayOf(sz, alloc);
        for (auto i = 0u; i < sz; ++i) {
            arr[i] = (*data.arr)[i].View(alloc, depth);
        }
        return JsonView(arr, sz);
    }
    case t_object: {
        constexpr bool already_sorted = JsonConfigTraits<Config>::ObjectSorted;
        auto sz = unsigned(data.obj->size());
        auto obj = MakeObjectOf(sz, alloc);
        unsigned count = 0;
        if (!already_sorted) {
            for (auto& it: *data.obj) {
                JsonPair curr;
                curr.key = string_view(it.first);
                curr.value = it.second.View(alloc, depth);
                count = SortedInsertJson(obj, count, curr, sz);
            }
            Data res{t_object, {}, count, {}};
            res.d.object = obj;
            return JsonView(res);
        } else {
            for (auto& it: *data.obj) {
                JsonPair& curr = obj[count++];
                curr.key = string_view(it.first);
                curr.value = it.second.View(alloc, depth);
            }
            Data res{t_object, {}, sz, {}};
            res.d.object = obj;
            return JsonView(res);
        }
    }
    case t_string: {
        return string_view(*data.str);
    }
    case t_binary: {
        return JsonView::Binary(string_view(data.bin->data(), data.bin->size()));
    }
    default: return {};
    }
}

template<typename Config>
void BasicMutJson<Config>::copy(BasicMutJson &to, JsonView src, unsigned int depth) {
    DepthError::Check(depth--);
    to = {src.GetType()};
    switch (src.GetType()) {
    case t_array: {
        to.data.arr->resize(src.GetUnsafe().size);
        unsigned idx = 0;
        for (auto i: src.Array(false)) {
            copy((*to.data.arr)[idx++], i, depth);
        }
        break;
    }
    case t_object: {
        auto hint = to.data.obj->begin();
        for (auto [k, v]: src.Object(false)) {
            hint = to.data.obj->emplace_hint(hint, k, BasicMutJson(v, depth));
        }
        break;
    }
    case t_string: {
        *to.data.str = String{src.GetStringUnsafe()};
        break;
    }
    case t_binary: {
        auto b = src.GetBinaryUnsafe();
        to.data.bin->resize(src.GetUnsafe().size);
        memcpy(to.data.bin->data(), b.data(), b.size());
        break;
    }
    case t_number: {
        to.data.number = src.GetUnsafe().d.number;
        break;
    }
    case t_signed: {
        to.data.integer = src.GetUnsafe().d.integer;
        break;
    }
    case t_unsigned: {
        to.data.uinteger = src.GetUnsafe().d.uinteger;
        break;
    }
    case t_boolean: {
        to.data.boolean = src.GetUnsafe().d.boolean;
        break;
    }
    default:
        to.data = {};
    }
}

template<typename Config>
void BasicMutJson<Config>::copy(Array &to, const Array &from, unsigned int depth) {
    to.reserve(from.size());
    for (auto& i: from) {
        to.emplace_back(BasicMutJson(i, depth));
    }
}

template<typename Config>
void BasicMutJson<Config>::copy(Object &to, const Object &from, unsigned int depth) {
    auto hint = to.begin();
    for (auto& it: from) {
        hint = to.emplace_hint(hint, it.first, BasicMutJson(it.second, depth));
    }
}

template<typename Config>
void BasicMutJson<Config>::copy(BasicMutJson &to, const BasicMutJson &from, unsigned int depth) {
    DepthError::Check(depth--);
    to = BasicMutJson{from.data.type};
    switch (from.data.type) {
    case t_array: {
        copy(*to.data.arr, *from.data.arr, depth);
        break;
    }
    case t_object: {
        copy(*to.data.obj, *from.data.obj, depth);
        break;
    }
    case t_string: {
        *to.data.str = *from.data.str;
        break;
    }
    case t_binary: {
        *to.data.str = *from.data.str;
        break;
    }
    default: {
        to.data = from.data;
        break;
    }
    }
}

template<typename Config>
void BasicMutJson<Config>::Destroy(Arena& alloc) try {
    ArenaVector<Value> stack(alloc);
    stack.push_back(data);
    data = {};
    while(!stack.empty()) {
        auto it = stack.back();
        stack.pop_back();
        switch(it.type) {
        case t_array: {
            for (auto& nit: *it.arr) {
                stack.push_back(nit.TakeUnsafe());
            }
            it.arr->clear();
            disposeOf(it.arr);
            break;
        }
        case t_object: {
            for (auto& nit: *it.obj) {
                stack.push_back(nit.second.TakeUnsafe());
            }
            it.obj->clear();
            disposeOf(it.obj);
            break;
        }
        case t_binary: {
            disposeOf(it.bin);
            break;
        }
        case t_string: {
            disposeOf(it.str);
            break;
        }
        default: break;
        }
    }
} catch (...) {
    fputs(" ### POSSIBLE LEAK DETECTED while deleting Json!\n", stderr);
    throw;
}

template<typename Config>
auto BasicMutJson<Config>::Assign(JsonPointer ptr, TraceFrame const& frame) -> BasicMutJson& {
    BasicMutJson* curr = this;
    unsigned idx = 0;
    for (auto part: ptr) {
        try {
            part.Visit(
                [&](string_view key){
                    if (curr->Is(t_null)) {
                        *curr = BasicMutJson(t_object);
                    }
                    auto& obj = curr->GetObject();
                    auto found = obj.find(key);
                    if (found != obj.end()) {
                        curr = &found->second;
                    } else {
                        curr = &obj[Key_t{key}];
                    }
                },
                [&](unsigned idx){
                    if (curr->Is(t_null)) {
                        *curr = BasicMutJson(t_array);
                    }
                    auto& arr = curr->GetArray();
                    if (arr.size() <= idx) {
                        arr.resize(idx + 1);
                    }
                    curr = &arr[idx];
                });
        } catch (JsonException& e) {
            e.trace = frame.PrintTrace() + ptr.SubPtr(0, idx).Join('.');
            throw;
        }
        idx++;
    }
    return *curr;
}

}

#endif // JV_JSON_HPP
