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

#ifndef FUT_FUTURE_HPP
#define FUT_FUTURE_HPP

#include <cassert>
#include <atomic>
#include <mutex>
#include <new>
#include <stdexcept>
#include "executor.hpp"
#include "rc/rc.hpp"

#define MV(x) x=std::move(x)

namespace fut
{

struct FutureError : std::logic_error {
    using std::logic_error::logic_error;
};

template<typename T> struct Future;
template<typename T> struct is_future : std::false_type {};
template<typename T> struct is_future<Future<T>> : std::true_type {};

struct Base {
    friend void AddRef(Base* d) noexcept {
        d->_refs.fetch_add(1, std::memory_order_acq_rel);
    }
    friend void Unref(Base* d) noexcept {
        if (d->_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            d->deleter(d);
        }
    }
    enum Flags : short {
        fullfilled      = 1 << 0,
        has_val         = 1 << 1,
        future_taken    = 1 << 2,
    };
    using Notify = void(*)(Base* self, bool call);
    using Deleter = void(*)(Base* self);

    Base(Deleter deleter) noexcept : deleter(deleter) {}
    Base(Base&&) = delete;
    ~Base();

    template<typename T> static void DeleterFor(Base* s);

    Deleter deleter = nullptr;
    std::mutex mtx;
    rc::Strong<Executor> exec = nullptr;
    rc::Strong<Base> chain = nullptr;
    Notify notify = nullptr;
    void* ctx = nullptr;
    std::exception_ptr exc = nullptr;
    short flags = 0;                  // guarded by mtx
    std::atomic<short> promises{0};
    std::atomic<int> _refs{0};
};

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable: 4324 )
#endif

template<typename T>
struct Data final : Base {
    Data() noexcept : Base(DeleterFor<T>) {}
    alignas(T) char buff[sizeof(T)];
    T* data() noexcept {
        return std::launder(reinterpret_cast<T*>(buff));
    }
    ~Data() {
        if (flags & has_val) {
            data()->~T();
        }
    }
};

#ifdef _MSC_VER
#pragma warning( pop )
#endif

template<> struct Data<void> final : Base {
    Data() noexcept : Base(DeleterFor<void>) {}
    void* data() noexcept {
        return reinterpret_cast<void*>(1);
    }
};

template<typename T>
using StatePtr = rc::Strong<Data<T>>;

template<typename T>
struct Result {
    Result(T* res) noexcept : res(res) {}
    Result(std::exception_ptr exc) noexcept : exc(std::move(exc)) {}
    //Result(Result const&) = delete;
    //Result(Result&&) = delete;

    std::exception_ptr get_exception() const& noexcept {
        return res ? std::exception_ptr{} : exc;
    }
    [[nodiscard]] std::exception_ptr get_exception() && noexcept {
        return res ? std::exception_ptr{} : std::move(exc);
    }
    explicit operator bool() const noexcept {
        return res;
    }
    T* get_ptr() noexcept {
        return res;
    }
    [[nodiscard]] T get() {
        if (meta_Likely(res)) {
            if constexpr (std::is_void_v<T>) return;
            else return std::move(*std::exchange(res, nullptr));
        } else if (exc) {
            std::rethrow_exception(std::move(exc));
        } else {
            throw std::runtime_error("get() already called");
        }
    }
protected:
    T* res = {};
    std::exception_ptr exc;
};

template<typename T>
struct Future;
template<typename T>
struct Promise;

namespace d {
void continueChain(rc::Strong<Base> data) noexcept;
template<typename T> struct strip_fut {using type = T;};
template<typename T> struct strip_fut<Future<T>> {using type = T;};
template<typename T, typename Fn> struct GetRet {
    using type = std::invoke_result_t<Fn, T>;
    using strip = typename strip_fut<type>::type;
};
template<typename Fn> struct GetRet<void, Fn> {
    using type = std::invoke_result_t<Fn>;
    using strip = typename strip_fut<type>::type;
};
template<typename Fn, typename T>
void notifyImpl(Base* self, bool call) noexcept;
template<typename Fn, typename T>
void notifyLastImpl(Base* self, bool call) noexcept;
template<typename Fn, typename T>
void notifyTryImpl(Base* self, bool call) noexcept;

// Store the result + raise `fullfilled` under the state's lock. Returns true if
// this call was the one that fulfilled the state (i.e. it was pending before).
inline bool fulfilExc(Base* d, std::exception_ptr e) noexcept {
    std::lock_guard<std::mutex> lk(d->mtx);
    if (d->flags & Base::fullfilled) return false;
    d->exc = std::move(e);
    d->flags |= Base::fullfilled;
    return true;
}
inline bool fulfilVoid(Base* d) noexcept {
    std::lock_guard<std::mutex> lk(d->mtx);
    if (d->flags & Base::fullfilled) return false;
    d->flags |= Base::fullfilled;
    return true;
}
template<typename T, typename U>
bool fulfilValue(Data<T>* d, U&& v) {
    std::lock_guard<std::mutex> lk(d->mtx);
    if (d->flags & Base::fullfilled) return false;
    new (d->buff) T{std::forward<U>(v)};
    d->flags |= Base::fullfilled | Base::has_val;
    return true;
}
} //detail

template<typename T>
struct [[nodiscard]] Future {
    using value_type = T;

    template<typename Fn>
    using IfValidTryOrLast = std::enable_if_t<std::is_invocable_v<Fn, Result<T>>>;
    template<typename Fn>
    using IfValidCatch = std::enable_if_t<std::is_invocable_v<Fn, std::exception&>>;
    template<typename Fn>
    using IfValidThen = d::GetRet<T, Fn>;

    Future(StatePtr<T> state = nullptr) noexcept : state(state) {}

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    template<typename Fn>
    static Future FromFunction(Fn f) {
        Promise<T> prom;
        auto fut = prom.GetFuture();
        f(std::move(prom));
        return fut;
    }
    StatePtr<T> TakeState() noexcept {
        return std::move(state);
    }
    Data<T>* PeekState() noexcept {
        return state.get();
    }
    bool IsValid() const noexcept {
        return bool(state);
    }
    template<typename Fn, typename = IfValidThen<Fn>>
    auto Then(rc::Strong<Executor> exec, Fn f) {
        using Ret = d::GetRet<T, Fn>;
        using Next = typename Ret::strip;
        rc::Strong<Data<Next>> chain = new Data<Next>;
        install(std::move(exec), &d::notifyImpl<Fn, T>, chain, std::move(f));
        return Future<Next>(chain);
    }
    template<typename Fn, typename = IfValidThen<Fn>>
    auto ThenSync(Fn f) {
        return Then(nullptr, std::move(f));
    }
    template<typename Fn, typename = IfValidTryOrLast<Fn>>
    auto Try(rc::Strong<Executor> exec, Fn f) {
        using Ret = d::GetRet<Result<T>, Fn>;
        using Next = typename Ret::strip;
        rc::Strong<Data<Next>> chain = new Data<Next>;
        install(std::move(exec), &d::notifyTryImpl<Fn, T>, chain, std::move(f));
        return Future<Next>(chain);
    }
    template<typename Fn, typename = IfValidTryOrLast<Fn>>
    auto TrySync(Fn f) {
        return Try(nullptr, std::move(f));
    }
    template<typename Fn, typename = IfValidTryOrLast<Fn>>
    void AtLast(rc::Strong<Executor> exec, Fn f) {
        install(std::move(exec), &d::notifyLastImpl<Fn, T>, nullptr, std::move(f));
    }
    template<typename Fn, typename = IfValidTryOrLast<Fn>>
    void AtLastSync(Fn f) {
        AtLast(nullptr, std::move(f));
    }
    template<typename Fn, typename = IfValidCatch<Fn>>
    void Catch(rc::Strong<Executor> exec, Fn f) {
        return AtLast(exec, [MV(f)](Result<T> ok) mutable {
            try {(void)ok.get();} catch (std::exception& e) {
                f(e);
            }
        });
    }
    template<typename Fn, typename = IfValidCatch<Fn>>
    void CatchSync(Fn f) {
        Catch(nullptr, std::move(f));
    }
protected:
    // Publish a continuation onto the state under the lock and then drive it.
    template<typename Fn>
    void install(rc::Strong<Executor> exec, Base::Notify n,
                 rc::Strong<Base> chain, Fn f) {
        Data<T>* sp = PeekState();
        if (!sp) throw FutureError("Invalid Future");
        auto* fctx = new Fn{std::move(f)};
        {
            std::lock_guard<std::mutex> lk(sp->mtx);
            if (sp->notify) {
                delete fctx;
                throw FutureError("Then() Called Twice");
            }
            sp->exec = std::move(exec);
            sp->chain = std::move(chain);
            sp->ctx = fctx;
            sp->notify = n;
        }
        d::continueChain(TakeState());
    }

    StatePtr<T> state;
};

template<typename T>
struct [[nodiscard]] SharedPromise {
    template<typename X, bool is>
    using if_exception = std::enable_if_t<std::is_base_of_v<std::exception, std::decay_t<X>> == is, bool>;

    SharedPromise(StatePtr<T> ptr = new Data<T>) noexcept : state(ptr) {
        ref();
    }
    bool IsValid() const noexcept {
        if (!state) return false;
        std::lock_guard<std::mutex> lk(state->mtx);
        return !(state->flags & Base::fullfilled);
    }
    bool operator()(Result<T> res) const {
        if (auto ptr = res.get_ptr()) {
            if constexpr (std::is_void_v<T>) {
                return (*this)();
            } else {
                return (*this)(std::move(*ptr));
            }
        } else {
            return (*this)(std::move(res).get_exception());
        }
    }
    bool operator()(std::exception_ptr exc) const {
        if (!state) throw FutureError("Invalid Promise");
        bool first = d::fulfilExc(state.get(), std::move(exc));
        d::continueChain(state.get());
        return first;
    }
    template<typename E, if_exception<E, true> = 1>
    bool operator()(E exc) const {
        return (*this)(std::make_exception_ptr(std::move(exc)));
    }
    bool operator()() const {
        static_assert(std::is_void_v<T>);
        if (!state) throw FutureError("Invalid Promise");
        bool first = d::fulfilVoid(state.get());
        d::continueChain(state.get());
        return first;
    }
    template<typename U, if_exception<U, false> = 1>
    bool operator()(U && value) const {
        if (!state) throw FutureError("Invalid Promise");
        bool first = d::fulfilValue(state.get(), std::forward<U>(value));
        d::continueChain(state.get());
        return first;
    }
    SharedPromise(SharedPromise const & p) noexcept {
        state = p.state;
        ref();
    }
    SharedPromise(SharedPromise&& p) noexcept :
        state(std::move(p.state))
    {}
    Future<T> GetFuture() {
        if (!state) throw FutureError("Invalid Promise");
        Data<T>& data = *state;
        {
            std::lock_guard<std::mutex> lk(data.mtx);
            if (data.flags & Base::future_taken) throw FutureError("Future already taken");
            data.flags |= Base::future_taken;
        }
        return {state};
    }
    SharedPromise& operator=(SharedPromise const & p) noexcept {
        if (state.data != p.state.data) {
            deref();
            state = p.state;
            ref();
        }
        return *this;
    }
    SharedPromise& operator=(SharedPromise&& p) noexcept {
        std::swap(state, p.state);
        return *this;
    }
    ~SharedPromise() {
        deref();
    }
protected:
    void ref() noexcept {
        if (Data<T>* d = state.get()) {
            d->promises.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void deref() noexcept {
        if (Data<T>* d = state.get()) {
            if (d->promises.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                bool done;
                {
                    std::lock_guard<std::mutex> lk(d->mtx);
                    done = d->flags & Base::fullfilled;
                }
                if (!done) {
                    (*this)(FutureError("Broken Promise"));
                }
            }
        }
    }
    StatePtr<T> state;
};

template<typename T>
struct [[nodiscard]] Promise : SharedPromise<T> {
    Promise(StatePtr<T> ptr = new Data<T>) noexcept : SharedPromise<T>(std::move(ptr)) {}
    using SharedPromise<T>::operator();
    using SharedPromise<T>::SharedPromise;
    using SharedPromise<T>::operator=;
    Promise(const Promise&) = delete;
    Promise(Promise&&) noexcept = default;
    Promise& operator=(Promise&&) noexcept = default;
};

template<typename T>
Future<T> Rejected(std::exception_ptr exc) {
    Promise<T> prom;
    prom(std::move(exc));
    return prom.GetFuture();
}

template<typename T, typename E>
Future<T> Rejected(E v) {
    Promise<T> prom;
    prom(std::move(v));
    return prom.GetFuture();
}

Future<void> Resolved();

template<typename T>
Future<T> Resolved(T value) {
    Promise<T> prom;
    prom(std::move(value));
    return prom.GetFuture();
}

namespace d {

template<typename T>
inline Result<T> getRes(Base* d) noexcept {
    return d->exc ? Result<T>(d->exc) : Result<T>(static_cast<Data<T>*>(d)->data());
}

// Move the resolved result of `self` into its `chain` and fulfil the chain.
// Used to splice a future returned from a continuation into the existing chain.
template<typename strip>
void notifyForward(Base* _self, bool call) {
    auto* self = static_cast<Data<strip>*>(_self);
    if (call) {
        auto* chain = static_cast<Data<strip>*>(self->chain.get());
        assert(chain);
        assert(self->flags & Base::fullfilled);
        if (self->exc) {
            fulfilExc(chain, std::move(self->exc));
        } else if constexpr (!std::is_void_v<strip>) {
            fulfilValue(chain, std::move(*self->data()));
        } else {
            fulfilVoid(chain);
        }
    }
}

template<bool unpack, typename Fn, typename T>
void setResult(rc::Strong<Base>& chain, Fn& fn, Result<T> res) noexcept try {
    using ret = GetRet<std::conditional_t<unpack, T, Result<T>>, Fn>;
    using type = typename ret::type;
    using strip = typename ret::strip;
    constexpr auto is_future_returned = !std::is_same_v<type, strip>;
    [[maybe_unused]] Data<strip>* next = static_cast<Data<strip>*>(chain.get());
    if constexpr (unpack) {
        if (!res) {
            fulfilExc(chain.get(), std::move(res).get_exception());
            return;
        }
    }
    if constexpr (is_future_returned) {
        // attach received future as parent: when it resolves, notifyForward
        // splices its result into our chain.
        Future<strip> fut(nullptr);
        if constexpr (unpack && std::is_void_v<T>) {
            fut = fn();
        } else if constexpr (unpack) {
            fut = fn(res.get());
        } else {
            fut = fn(std::move(res));
        }
        Data<strip>* parent = fut.PeekState();
        if (meta_Unlikely(!parent)) {
            fulfilExc(chain.get(), std::make_exception_ptr(
                FutureError("Continuation returned an invalid Future")));
            return;
        }
        bool taken = false;
        {
            std::lock_guard<std::mutex> lk(parent->mtx);
            if (meta_Unlikely(parent->notify)) {
                taken = true; // already has a continuation: must not overwrite
            } else {
                parent->chain = chain;
                parent->notify = notifyForward<strip>;
            }
        }
        if (meta_Unlikely(taken)) {
            fulfilExc(chain.get(), std::make_exception_ptr(
                FutureError("Continuation returned a Future that is already chained")));
            return;
        }
        continueChain(parent);
    } else if constexpr (!std::is_void_v<type>) {
        // next future result set from f()
        if constexpr (unpack && std::is_void_v<T>) {
            fulfilValue(next, fn());
        } else if constexpr (unpack) {
            fulfilValue(next, fn(res.get()));
        } else {
            fulfilValue(next, fn(std::move(res)));
        }
    } else {
        // next future is void
        if constexpr (unpack && std::is_void_v<T>) {
            fn();
        } else if constexpr (unpack) {
            fn(res.get());
        } else {
            fn(std::move(res));
        }
        fulfilVoid(next);
    }
} catch (...) {
    fulfilExc(chain.get(), std::current_exception());
}

[[noreturn]] void onLastExc();

}

template<typename Fn, typename T>
void d::notifyLastImpl(Base* _self, bool call) noexcept {
    Data<T>* self = static_cast<Data<T>*>(_self);
    Fn* fn = static_cast<Fn*>(self->ctx);
    if (call) {
        assert(self->flags & Base::fullfilled);
        try {
            // last handler should not throw
            if (self->exc) {
                (void)(*fn)(Result<T>(self->exc));
            } else {
                (void)(*fn)(Result<T>(static_cast<Data<T>*>(self)->data()));
            }
        } catch (...) {
            onLastExc();
        }
    }
    delete fn;
}

template<typename Fn, typename T>
void d::notifyImpl(Base* self, bool call) noexcept {
    Fn* fn = static_cast<Fn*>(self->ctx);
    if (call) {
        assert(self->flags & Base::fullfilled);
        assert(self->chain);
        setResult<true>(self->chain, *fn, getRes<T>(self));
    }
    delete fn;
}

template<typename Fn, typename T>
void d::notifyTryImpl(Base* self, bool call) noexcept {
    Fn* fn = static_cast<Fn*>(self->ctx);
    if (call) {
        assert(self->flags & Base::fullfilled);
        assert(self->chain);
        setResult<false>(self->chain, *fn, getRes<T>(self));
    }
    delete fn;
}

template<typename T> void Base::DeleterFor(Base* s) {
    delete static_cast<Data<T>*>(s);
}

} //fut

#endif // FUT_FUTURE_HPP
