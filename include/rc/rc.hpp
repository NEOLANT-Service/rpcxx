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

#ifndef RC_HPP
#define RC_HPP

#include <atomic>
#include <cstddef>
#include <utility>

namespace rc
{

template<typename T>
struct Strong {
    template<typename U>
    using if_compatible = std::enable_if_t<std::is_convertible_v<U*, T*>, int>;

    Strong(T* data = nullptr) noexcept : data(data) {
        if (data) AddRef(data);
    }
    Strong(const Strong& o) noexcept : data(o.data) {
        if (data) AddRef(data);
    }
    explicit operator bool() const noexcept {
        return data;
    }
    T* get() const noexcept {
        return data;
    }
    T& operator*() const noexcept {
        return *data;
    }
    T* Release() noexcept {
        return std::exchange(data, nullptr);
    }
    template<typename U, if_compatible<U> = 1>
    Strong(Strong<U> const& other) : Strong(other.data) {}
    template<typename U, if_compatible<U> = 1>
    Strong(Strong<U> && other) : data(std::exchange(other.data, nullptr)) {}
    Strong(Strong&& o) noexcept : data(std::exchange(o.data, nullptr)) {}
    Strong& operator=(const Strong& o) noexcept {
        if (this != &o) {
            if(data) Unref(data);
            data = o.data;
            if(data) AddRef(data);
        }
        return *this;
    }
    T* operator->() const noexcept {
        return data;
    }
    Strong& operator=(Strong&& o) noexcept {
        std::swap(data, o.data);
        return *this;
    }
    ~Strong() {
        if(data) Unref(data);
    }
protected:
    template<typename> friend struct Strong;
    T* data;
};

struct DefaultBase {
    friend void AddRef(DefaultBase* d) noexcept {
        d->_refs.fetch_add(1, std::memory_order_acq_rel);
    }
    template<typename T>
    friend void Unref(T* d) noexcept {
        if (d->_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete d;
        }
    }
protected:
    std::atomic<int> _refs{0};
    std::atomic_bool _sync{false};
};

struct WeakBlock : DefaultBase {
    std::atomic<char*> data;
    using DefaultBase::_sync;
};

//! Should not be multi-inherited! (multiple separate ref-counts to same object will appear)
struct SingleVirtualBase : public DefaultBase {
    virtual ~SingleVirtualBase() = default;
};

struct VirtualBase : public virtual SingleVirtualBase {};

inline static void _lock(std::atomic_bool& lock) {
    bool was = false;
    while(!lock.compare_exchange_strong(was, true, std::memory_order_acq_rel)) {
        was = false;
    }
}

inline static void _unlock(std::atomic_bool& lock) {
    lock.store(false, std::memory_order_release);
}

struct WeakableVirtual : VirtualBase {
    friend void Unref(WeakableVirtual* d) noexcept {
        _lock(d->_block->_sync);
        if (d->_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (d->_block) {
                d->_block->data.store(nullptr, std::memory_order_release);
            }
            _unlock(d->_block->_sync);
            delete d;
        } else {
            _unlock(d->_block->_sync);
        }
    }
    template<typename T>
    friend Strong<WeakBlock> GetWeak(T* d, int* offset) {
        _make(d->_block, d->_sync, reinterpret_cast<char*>(d), offset);
        return d->_block;
    }
    ~WeakableVirtual() override {
        if (_block) {
            _block->data.store(nullptr, std::memory_order_release);
        }
    }
protected:
    static void _make(Strong<WeakBlock>& block, std::atomic_bool &lock, char* d, int* offset) {
        _lock(lock);
        if (!block) {
            try {block = new WeakBlock;}
            catch (...) {
                _unlock(lock);
                throw;
            }
            block->data.store(d, std::memory_order_release);
        }
        _unlock(lock);
        if (offset) {
            *offset = int(d - block->data.load(std::memory_order_acquire));
        }
    }
    Strong<WeakBlock> _block;
};

template<typename T>
struct Weak {
    template<typename U>
    using if_compatible = std::enable_if_t<std::is_convertible_v<U*, T*>, int>;

    Weak() noexcept = default;
    Weak(std::nullptr_t) noexcept {};

    template<typename U, if_compatible<U> = 1>
    Weak(U* obj) : block(obj ? GetWeak(static_cast<T*>(obj), &offset) : nullptr) {}
    template<typename U, if_compatible<U> = 1>
    Weak(Strong<U> const& obj) : Weak(obj.get()) {}

    T* peek() const noexcept {
        if (!block) return nullptr;
        return reinterpret_cast<T*>(block->data.load(std::memory_order_acquire) + offset);
    }
    Strong<T> lock() const noexcept {
        if (!block) return nullptr;
        _lock(block->_sync);
        Strong<T> r = reinterpret_cast<T*>(block->data.load(std::memory_order_acquire) + offset);
        _unlock(block->_sync);
        return r;
    }
private:
    int offset{};
    Strong<WeakBlock> block;
};

}

#endif //RC_HPP
