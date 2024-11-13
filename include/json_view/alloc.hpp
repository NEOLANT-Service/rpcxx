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

#ifndef JV_ALLOC_HPP
#define JV_ALLOC_HPP

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <forward_list>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include "meta/compiler_macros.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#define jv_arena_attrs __attribute__((__returns_nonnull__,__alloc_size__(2),__alloc_align__(3)))
#else
#define jv_arena_attrs
#endif

namespace jv
{

struct Arena {
    static constexpr auto max_align = alignof(std::max_align_t);
    meta_alwaysInline
    void* operator()(size_t sz, size_t align = max_align)
    jv_arena_attrs
    {
        return ::operator new(sz, Allocate(sz, align));
    }
    meta_alwaysInline
    void* Allocate(size_t sz, size_t align = max_align)
    jv_arena_attrs
    {
        auto res = DoAllocate(sz, align);
        if (meta_Unlikely(!res)) throw std::bad_alloc{};
        return res;
    }
protected:
    virtual void* DoAllocate(size_t sz, size_t align) = 0;
};

struct NullArena final : Arena {
    void* DoAllocate(size_t, size_t) override {
        throw std::bad_alloc{};
    }
};

namespace detail {
template<size_t sz>
struct stackBuff {
    stackBuff() = default;
    stackBuff(stackBuff&&) = delete;
    alignas(std::max_align_t) char buff[sz];
};
template<>
struct stackBuff<size_t(0)> {
    static constexpr char* buff = nullptr;
};
struct arena {
    arena() = default;
    arena(arena const&) = delete;
    arena(arena && o) noexcept {
        moveIn(o);
    }
    arena& operator=(arena && o) noexcept {
        if (this != &o) {
            clear();
            moveIn(o);
        }
        return *this;
    }
    void moveIn(arena& o) noexcept {
        buffptr = std::exchange(o.buffptr, nullptr);
        space = std::exchange(o.space, 0);
        blockSize = o.blockSize;
        allocs = std::exchange(o.allocs, std::forward_list<void*>{});
    }
    ~arena() {
        clear();
    }
    void newBlock() {
        buffptr = allocs.emplace_front(::operator new(blockSize, std::align_val_t(Arena::max_align)));
        space = blockSize;
    }
    void clear() {
        for (auto a: allocs) {
            ::operator delete(a, std::align_val_t(Arena::max_align));
        }
        allocs.clear();
        buffptr = nullptr;
        space = 0;
    }
    void* doAlloc(size_t bytes, size_t align) {
        if (meta_Unlikely(bytes > blockSize)) {
            return allocs.emplace_front(::operator new(bytes, std::align_val_t(Arena::max_align)));
        }
        if (meta_Unlikely(!std::align(align, bytes, buffptr, space))) {
            newBlock();
        }
        space -= bytes;
        return std::exchange(buffptr, static_cast<char*>(buffptr) + bytes);
    }

    void* buffptr{};
    size_t space{};
    size_t blockSize{};
    std::forward_list<void*> allocs;
};
} //detail

template<size_t onStack = 2048>
struct DefaultArena final : Arena,
                            protected detail::stackBuff<onStack>,
                            protected detail::arena
{
    DefaultArena(size_t blockSize = 4096) noexcept {
        SetBlockSize(blockSize);
        Clear();
    }
    void SetBlockSize(size_t sz) {
        this->blockSize = sz;
    }
    void Clear() {
        arena::clear();
        this->buffptr = this->buff;
        this->space = onStack;
    }
protected:
    void* DoAllocate(size_t bytes, size_t align) final {
        return detail::arena::doAlloc(bytes, align);
    }
};

template<typename T>
struct arena_allocator {
    Arena* a = nullptr;
    using value_type = T;
    arena_allocator(Arena& alloc) noexcept : a(&alloc) {}
    template<typename U>
    arena_allocator(arena_allocator<U> const& other) : a(other.a) {}
    meta_alwaysInline T* allocate(std::size_t n) {
        return static_cast<T*>(a->Allocate(sizeof(T) * n, alignof(T)));
    }
    void deallocate(T*, std::size_t) noexcept {}
    template<typename U>
    bool operator==(arena_allocator<U> const& other) const noexcept {
        return a == other.a;
    }
};

template<typename T>
using ArenaVector = std::vector<T, arena_allocator<T>>;

struct ArenaString : public ArenaVector<char> {
    using ArenaVector<char>::ArenaVector;
    using ArenaVector<char>::operator=;

    operator std::string_view() const noexcept {
        return {data(), size()};
    }

    ArenaString(std::string_view part, arena_allocator<char> alloc) : ArenaVector<char>(alloc) {
        reserve(part.size() + 1);
        Append(part);
    }

    void Append(std::string_view part) {
        auto was = size();
        resize(was + part.size());
        ::memcpy(data() + was, part.data(), part.size());
    }
};

} //jv

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif //JV_ALLOC_HPP
