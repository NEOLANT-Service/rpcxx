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

- **Passkey construction (compile-time, airtight):** `rc::WeakableVirtual`
  has a single constructor taking `rc::WeakableVirtual::Key` (aliased as
  `rc::MakeStrongRef`), a passkey type whose constructor is private and
  befriended only to `rc::MakeStrong`. Every subclass must therefore accept
  the key as its first constructor parameter and pass it down the chain, and
  the only way to obtain one is `rc::MakeStrong<T>(args...)` — which
  heap-allocates `T` and immediately wraps it in `rc::Strong`. Stack/static
  allocation, raw `new`, and placement into foreign containers are compile
  errors for every weakable type, including user-defined subclasses.
- `rc::Weak::lock()` refuses an object whose refcount is zero, i.e. one that
  no `rc::Strong` owns: it simply returns `nullptr`, exactly as for an
  expired object. This remains as the runtime backstop.
- The destructors of `IHandler`, `IClientTransport`, `IAsyncTransport` and
  `Server` are **protected**; the destructors of the final classes
  `Transport` and `ForwardToHandler` are **private**. Keep derived
  destructors non-public too: a public one invites a raw `delete` that
  bypasses the refcount.
- Destruction happens exclusively through `rc::Strong`. Do not `delete` these
  objects by hand and do not hand them to foreign ownership schemes (e.g. Qt
  parent/child) without releasing the `rc` ownership first
  (`Strong::Release()`), see `test/rps` for an example.

Migration for your own handlers/transports/servers:

```cpp
// before
struct MyServer : rpcxx::Server {
    MyServer(int port) : port(port) {}
};

// after — take rc::MakeStrongRef first and forward it to the base
struct MyServer : rpcxx::Server {
    MyServer(rc::MakeStrongRef key, int port) : Server(key), port(port) {}
};

auto server = rc::MakeStrong<MyServer>(8080);   // unchanged call site
```

`using Base::Base;` constructor inheritance keeps working unchanged. The
code generator emits key-accepting constructors, so regenerated stubs follow
the new rule automatically (existing hand-written subclasses of generated
servers need the extra parameter).

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

## 5. JSON-RPC behavior corrections

- **`"id": null`** is no longer treated as a notification. Only a request
  with *no* `id` member is a notification; an explicit null id is an invalid
  Request object and now gets an `Invalid Request` (-32600) error response
  with id null, per JSON-RPC 2.0.
- **`IHandler::SetRoute` rejects route cycles** (assert +
  `std::runtime_error`): installing a route whose target can route back to
  the same handler throws instead of enabling caller-controlled recursion
  depth. Direct self-routes (`SetRoute("self", self)`) remain allowed.
- **`Send()` failure rejects pending requests immediately** with an
  `RpcException` wrapping the send error, instead of leaving the caller's
  `Future` hanging until the timeout.
- **Exceptions from notify handlers no longer escape
  `IAsyncTransport::Receive()`** (and no longer produce error parts inside
  batch responses); they are logged via `error()` after the server's
  exception handlers have run.
- **Async methods rejected after their `Server` was destroyed** now fail the
  client request with a sanitized `RpcException("Server dead", internal)`
  instead of forwarding the raw, unwrapped exception — exception handlers
  exist precisely to hide such details from the client, and forwarding
  bypassed them. (With the default server executor the continuation is
  dropped at server teardown and the client sees a generic
  `FutureError("Broken Promise")` — also free of sensitive details.)

## 6. Build system

- The libFuzzer targets (`rpcxx-json-fuzz`, `rpcxx-msgpack-fuzz`) are now
  skipped when `RPCXX_TEST_TSAN=ON` — `-fsanitize=fuzzer,address` and
  `-fsanitize=thread` cannot coexist, so the TSAN build previously could not
  build all targets.
- doctest is kept at 2.4.11; `CMAKE_POLICY_VERSION_MINIMUM` is set to 3.5 for
  dependencies so CMake >= 4 configures succeed without extra command-line
  flags. (doctest 2.4.12 fixes the floor upstream but changes
  expression-decomposition semantics that the existing tests rely on.)

## 7. New opt-out: `rc::foreign_owned` for foreign-owned (Qt) objects

Section 2's passkey makes `rc::MakeStrong` the only way to construct weakable
objects. There is one deliberate, explicit escape hatch for objects owned by
a foreign scheme such as Qt parent/child:

```cpp
struct QObjectServer : QObject, rpcxx::Server {
    QObjectServer(QObject* parent)
        : QObject(parent), Server(rc::foreign_owned) {}
    ~QObjectServer() override = default; // public: the owner deletes
};

auto* server = new QObjectServer(parent); // no MakeStrong, no rc::Strong
```

- `rc` **never deletes** such an object (`Unref` only decrements); deletion
  is the owner's job (parent teardown, `deleteLater()`, ...).
- `rc::Weak` references keep working: expiry is signaled by the destructor
  nulling the weak block, so `lock()` returns `nullptr` once the owner
  deletes the object.
- **Not thread-safe by design**: construction, destruction and every
  `Weak::lock()` must happen on the same thread. Debug builds assert this
  (thread affinity recorded at construction).
- `rc::Strong` references to foreign-owned objects do not extend their
  lifetime — use them only as short-lived `lock()` results on the owner
  thread.

`IHandler`, `IClientTransport`, `IAsyncTransport` and `Server` each gained a
`rc::foreign_owned_t` constructor overload. `test/rps/rps_server.cpp` shows
the idiom: the per-connection server is parented to its `QWebSocket` and
dies with the connection, replacing the previous `Release()`/`deleteLater()`
hand-off dance.

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
- `JsonPointer::FromString`: "/" and "#/" now produce the single empty-string
  token per RFC 6901 (previously an uninitialized token), "#" is the root
  pointer, the URI-form leading '/' is skipped, and '~' escapes no longer
  stick for the rest of the token. URI-form Json Pointers now accept the
  full RFC 3986 fragment charset unencoded (`/`, `?`, `:`, `@`, sub-delims),
  so e.g. "#/a/b" no longer throws.
- `Server` async completions (`Wrap`, `OnForward`) capture the server
  weakly: rejecting an async method after the server was destroyed is no
  longer a use-after-free.
- Arena: allocations with `align > alignof(std::max_align_t)` now actually
  honor their alignment and are freed with the matching aligned delete.
- Dead code removed: `jv::detail` `fieldHelper`/`prepFields`.
