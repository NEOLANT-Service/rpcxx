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
#include <type_traits>
#include <utility>

namespace rc
{

struct WeakableVirtual;

template<typename T>
struct Strong;

template<typename T, typename... Args>
Strong<T> MakeStrong(Args&&... args);

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
    // Excluded for WeakableVirtual-derived types: those must go through
    // WeakableVirtual's own Unref, which invalidates the weak block under
    // its spinlock before deleting. (A plain template here is an exact match
    // in overload resolution and would silently shadow the weakable path.)
    template<typename T, std::enable_if_t<!std::is_base_of_v<WeakableVirtual, T>, int> = 0>
    friend void Unref(T* d) noexcept {
        if (d->_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete d;
        }
    }
    // Weak::lock() reads _refs to refuse objects that no rc::Strong owns.
    template<typename> friend struct Weak;
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
    // Passkey enforcing heap allocation: only rc::MakeStrong can create a
    // Key, and WeakableVirtual has no other constructor — so every subclass
    // must take a Key and can only be constructed by rc::MakeStrong, which
    // heap-allocates it and immediately hands ownership to rc::Strong.
    // Stack/static allocation or a raw `new` of a weakable object is a
    // compile error; rc::Weak can therefore rely on its lock() contract.
    class Key {
        Key() = default;
        template<typename T, typename... Args>
        friend Strong<T> MakeStrong(Args&&...);
    };
    explicit WeakableVirtual(Key) noexcept {}
    friend void Unref(WeakableVirtual* d) noexcept {
        // _sync serializes against GetWeak()/_make() (which lazily creates
        // _block); block->_sync serializes the refcount-to-zero decision
        // against Weak::lock(), which holds block->_sync across its
        // load+AddRef. Lock order is always _sync -> block->_sync.
        _lock(d->_sync);
        auto* block = d->_block.get();
        if (block) {
            _lock(block->_sync);
        }
        bool last = d->_refs.fetch_sub(1, std::memory_order_acq_rel) == 1;
        if (last && block) {
            // Invalidate the weak block BEFORE delete, still under the
            // spinlock: a concurrent Weak::lock() either AddRef'ed first
            // (so `last` is false) or observes nullptr from here on.
            block->data.store(nullptr, std::memory_order_release);
        }
        if (block) {
            _unlock(block->_sync);
        }
        _unlock(d->_sync);
        if (last) {
            delete d;
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

    //! Lock the weak reference into a strong one. Returns nullptr when the
    //! object cannot be locked: either it has expired, or it is alive but NOT
    //! owned by any rc::Strong (e.g. stack- or statically-allocated, or a raw
    //! pointer published before ownership was taken) — locking such an object
    //! would make the temporary Strong delete memory it does not own. Objects
    //! that take part in weak references must be heap-allocated and owned via
    //! rc::Strong from the moment they are published — see rc::MakeStrong.
    Strong<T> lock() const noexcept {
        if (!block) return nullptr;
        _lock(block->_sync);
        // The spinlock makes load+AddRef atomic w.r.t. WeakableVirtual::Unref
        // reaching zero: an object whose block was nulled stays dead. Since
        // Unref nulls the block in the same critical section that drops the
        // refcount to zero, a non-null pointer here implies _refs >= 1 for
        // any properly rc-owned object; _refs == 0 therefore means the object
        // is not owned by a Strong and must not be locked.
        char* p = block->data.load(std::memory_order_acquire);
        Strong<T> r = nullptr;
        if (p) {
            T* obj = reinterpret_cast<T*>(p + offset);
            if (obj->_refs.load(std::memory_order_relaxed) != 0) {
                r = obj;
            }
        }
        _unlock(block->_sync);
        return r;
    }
private:
    int offset{};
    Strong<WeakBlock> block;
};

using WeakableKey = WeakableVirtual::Key;

//! Create a heap object immediately owned by rc::Strong — the ONLY way to
//! construct objects derived from rc::WeakableVirtual (IHandler,
//! IClientTransport, Server, transports, ...): their constructors require a
//! rc::WeakableKey that only this factory can create.
template<typename T, typename... Args>
Strong<T> MakeStrong(Args&&... args) {
    if constexpr (std::is_base_of_v<WeakableVirtual, T>) {
        return Strong<T>(new T(WeakableVirtual::Key{}, std::forward<Args>(args)...));
    } else {
        return Strong<T>(new T(std::forward<Args>(args)...));
    }
}

}

#endif //RC_HPP
