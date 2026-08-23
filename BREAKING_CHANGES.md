# Breaking changes on branch `fix/future-races-and-review`

This branch fixes data races, memory-corruption bugs and UB found during a
ThreadSanitizer-assisted review. Most fixes are internal, but a few change
public API or observable behavior. They are collected here for the merge
review.

## 1. `rc::Weak::peek()` removed — use `lock()`

`rc::Weak<T>` no longer has `peek()`. The only way to dereference a weak
reference is `lock()`, which returns an owning `rc::Strong<T>` that keeps the
object alive for the whole duration of its use.

`peek()` returned a borrowed raw pointer that could dangle as soon as the last
`Strong` went away on another thread — every use was a potential
use-after-free under cross-thread teardown.

## 2. Enforced ownership: no more stack-allocated servers/transports

Objects that take part in `rc::Weak` references — `Server` (and subclasses),
`IHandler`, `IClientTransport`, `IAsyncTransport`, `Transport`,
`ForwardToHandler`, executors — must now be **heap-allocated and owned by
`rc::Strong`** from the moment they are handed out:

```cpp
// before
Server server;
rc::Strong<IClientTransport> link = new ForwardToHandler(&server);

// after
auto server = rc::MakeStrong<Server>();              // rc::Strong<Server>
rc::Strong<IClientTransport> link = rc::MakeStrong<ForwardToHandler>(server);
```

Enforcement:

- `rc::Weak::lock()` refuses an object whose refcount is zero, i.e. one that
  no `rc::Strong` owns (stack/static allocation, or a raw pointer published
  before ownership was taken): it simply returns `nullptr`, exactly as for an
  expired object. Locking such an object previously worked by luck with
  `peek()` and would have deleted stack memory with `lock()`.
- The destructors of `IHandler`, `IClientTransport`, `IAsyncTransport` and
  `Server` are now **protected**; the destructors of the final classes
  `Transport` and `ForwardToHandler` are **private**. Direct stack allocation
  of these types is a compile error. Derived classes can still declare a
  public destructor (C++ cannot forbid that), so the runtime check in
  `lock()` remains the backstop — keep derived destructors protected and
  create instances via `rc::MakeStrong<Derived>()` anyway.
- Destruction happens exclusively through `rc::Strong`. Do not `delete` these
  objects by hand and do not hand them to foreign ownership schemes (e.g. Qt
  parent/child) without releasing the `rc` ownership first
  (`Strong::Release()`), see `test/rps` for an example.

A new factory `rc::MakeStrong<T>(args...)` (in `include/rc/rc.hpp`) is the
idiomatic way to create such objects.

## 3. Futures: invalid continuations now reject instead of crashing/hanging

- A continuation that returns a **default-constructed / empty `Future<T>`**
  (or one that already has a continuation chained) now rejects the chain with
  `fut::FutureError`. Previously this dereferenced a null state (segfault) or
  silently hijacked the existing continuation.
- When an `Executor` drops a continuation job (`Execute` returns `Cancel`),
  the chained future is now rejected with `fut::FutureError("continuation
  cancelled by executor")` and the rejection propagates downstream.
  Previously the chain simply never resolved and every waiter hung forever.

Code that (accidentally) relied on the old behavior will now observe an
exception where it previously crashed, hung, or silently lost a callback.

## 4. `Server::CurrentContext()` is thread-local during dispatch

The current per-call context is now tracked per thread. Code that relied on
observing the "current" context from a thread *other* than the one executing
the handler will now get the fallback context instead. Within the handler
thread itself nothing changes.

## 5. Build system

- The libFuzzer targets (`rpcxx-json-fuzz`, `rpcxx-msgpack-fuzz`) are now
  skipped when `RPCXX_TEST_TSAN=ON` — `-fsanitize=fuzzer,address` and
  `-fsanitize=thread` cannot coexist, so the TSAN build previously could not
  build all targets.
- doctest is kept at 2.4.11; `CMAKE_POLICY_VERSION_MINIMUM` is set to 3.5 for
  dependencies so CMake >= 4 configures succeed without extra command-line
  flags. (doctest 2.4.12 fixes the floor upstream but changes
  expression-decomposition semantics that the existing tests rely on.)

## Non-breaking fixes (for completeness)

These changed no API and no valid observable behavior — they only remove UB:

- `MultiFuture`: result/exception publication and waiter dispatch are now
  mutex-protected and reentrancy-safe (waiters run outside the lock).
- `Signal`: the callback is snapshotted under the lock before being deferred
  to an executor (concurrent re-subscription no longer races).
- `IAsyncTransport`: pending-request map, handler and id generation are
  mutex/atomic-protected; promises are fulfilled outside the lock;
  erase-before-fulfil makes inline continuations reentrancy-safe.
  `StoppableExecutor::Stop()` now also waits for in-flight jobs.
- `rc::WeakableVirtual::Unref` / `Weak::lock()`: weak-block invalidation and
  the resurrect-check are serialized under the block spinlock (documented
  lock order `_sync` → `block->_sync`).
- `membuff::In::Read` no longer over-reads; `ReadByte` no longer uses a stale
  pointer after `Refill`; `StringOut::Grow(0)` grows instead of looping.
- `json_view`: `BasicMutJson` bool assignment, binary/string `copy()` and
  `algo::Copy` no-copy flags fixed (union member confusion); `maxIdxFor`
  off-by-one and `FieldIndex<0>` tuple serialization fixed.
