# rpcxx

**A fast, transport-agnostic JSON-RPC 2.0 library for C++17.**

`rpcxx` lets you expose plain C++ functions as RPC methods and call remote
methods as if they were local — over any transport you like (WebSocket, TCP,
in-process, a message queue…). Requests and responses are described by your own
structs through compile-time reflection, serialized to **JSON or MessagePack**,
and dispatched asynchronously through a lightweight `Future`/`Promise` model.

It also ships a **Lua-based code generator** that turns a small interface DSL
into typed server and client stubs, and a handful of reusable header-only
sub-libraries (futures, a zero-copy JSON DOM, ref-counting, buffers).

```cpp
// Servers (and transports) are heap objects owned by rc::Strong
auto server = rc::MakeStrong<rpcxx::Server>();

// Params received positionally (JSON array)
server->Method("add", [](int a, int b){ return a + b; });

// Params received by name (JSON object)
server->Method("sub", [](int a, int b){ return a - b; },
               rpcxx::NamesMap("a", "b"));

// Async method: return a Future and resolve it whenever you like
server->Method("slow_add", [](int a, int b) -> rpcxx::Future<int> {
    return rpcxx::Future<int>::FromFunction([=](auto promise){
        std::thread([=]() mutable { promise(a + b); }).detach();
    });
});
```

---

## Highlights

- **Transport-agnostic.** The core only produces and consumes JSON-RPC
  messages (`JsonView`). You bind it to a socket / event loop with two hooks:
  one to send bytes out, one to feed bytes in.
- **Two wire formats.** JSON-RPC 2.0 (`json_v2_compliant`) and a minified
  variant (`json_v2_minified`, shorter keys, no envelope) — selected per
  transport. Each can be carried as JSON text or MessagePack binary.
- **Reflection-based (de)serialization.** Describe a struct once with the
  `DESCRIBE` macro and it can travel over RPC, with no hand-written
  marshalling.
- **Async by default.** Handlers may return a value or a `Future<T>`; clients
  always get a `Future<T>` back. Compose with `Then` / `Try` / `AtLast` /
  `Gather`, or block via `ToStdFuture`.
- **Zero-copy JSON DOM.** `JsonView` is an arena-allocated, trivially-copyable
  view over a document — ideal for the short-lived request/response lifetime.
- **Routing, middleware, per-call context and structured exception handling**
  on the server.
- **Request batching** on the client.
- **Code generation** of typed C++ (and limited Go) stubs from a Lua spec, with
  one-step CMake integration.

---

## Table of contents

- [Building](#building)
- [Using rpcxx in your project](#using-rpcxx-in-your-project)
- [Core concepts](#core-concepts)
- [The server](#the-server)
- [The client](#the-client)
- [Transports](#transports)
- [Serialization & `DESCRIBE`](#serialization--describe)
- [Code generation](#code-generation)
- [Bundled sub-libraries (`include/`)](#bundled-sub-libraries-include)
- [Testing](#testing)
- [License](#license)

---

## Building

Requires a C++17 compiler and CMake ≥ 3.16. Third-party dependencies are fetched
automatically via [CPM](https://github.com/cpm-cmake/CPM.cmake)
(`fmt`, [`describe`](https://github.com/cyanidle/describe), `argparse`,
`rapidjson` for parsing, and `doctest` / `benchmark` for tests).

```sh
cmake -B build -DRPCXX_WITH_TESTS=ON
cmake --build build -j
ctest --test-dir build
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `RPCXX_WITH_CODEGEN` | `ON` | Build the `rpcxx-codegen` generator executable |
| `RPCXX_WITH_TESTS` | `OFF` | Build the test suite and benchmarks |
| `BUILD_SHARED_LIBS` | `OFF` | Build the libraries as shared |
| `RPCXX_TEST_SANITIZERS` | `OFF` | Build tests with ASan + UBSan |
| `RPCXX_TEST_TSAN` | `OFF` | Build with ThreadSanitizer |
| `RPCXX_TEST_RPS` | `OFF` | Build the Qt5 WebSocket throughput example |

The build produces three static libraries that you can link individually:

- **`rpcxx`** — the full RPC stack (`rpcxx::rpcxx`).
- **`rpcxx-json`** — the `JsonView` DOM, parsers and dumpers.
- **`rpcxx-future`** — the standalone future/promise library.

## Using rpcxx in your project

With CPM:

```cmake
CPMAddPackage("gh:NEOLANT-Service/rpcxx#<tag-or-commit>")
target_link_libraries(my_app PRIVATE rpcxx::rpcxx)
```

Or as a subdirectory / installed package, then link `rpcxx::rpcxx` (which pulls
in the headers, the JSON library and the future library transitively).

---

## Core concepts

- **`Server`** (an `IHandler`) — holds your registered methods and dispatches
  incoming requests to them.
- **`Client`** — turns typed calls into JSON-RPC messages and resolves the
  returned `Future` when the response arrives.
- **`IClientTransport` / `IAsyncTransport` / `Transport`** — move messages
  between client and server. The only thing that knows about your socket.
- **`Future<T>` / `Promise<T>`** — the async glue (see
  [`include/future`](#futures-includefuture)).

The smallest possible setup is fully in-process, with no serialization at all,
using `ForwardToHandler`:

```cpp
#include <rpcxx/rpcxx.hpp>
#include <future/to_std_fut.hpp>
using namespace rpcxx;

auto server = rc::MakeStrong<Server>();
server->Method("add", [](int a, int b){ return a + b; });

Client client;
rc::Strong<IClientTransport> link = rc::MakeStrong<ForwardToHandler>(server);
client.SetTransport(link);

int sum = ToStdFuture(
    client.Request<int>(Method{"add", NoTimeout}, 2, 3)
).get();                                   // == 5
```

---

## The server

### Registering methods

`Method` registers a request handler (produces a response); `Notify` registers a
fire-and-forget handler (no response). Both accept any callable.

```cpp
auto server = rc::MakeStrong<Server>();

server->Method("ping", []{ return "pong"; });
server->Method("add",  [](int a, int b){ return a + b; });
server->Notify("log",  [](std::string line){ /* ... */ });
```

**Optional parameters** use `std::optional`; **custom types** just need a
`DESCRIBE` (see below):

```cpp
server->Method("add", [](int a, std::optional<int> b){
    return a + b.value_or(0);
});
server->Method("copy", [](MyStruct s){ return s; });
```

### Positional vs. named parameters

By default parameters are read positionally (from a JSON array). Pass a
`NamesMap` to read them by name (from a JSON object) instead:

```cpp
server->Method("sub", [](int a, int b){ return a - b; },
               rpcxx::NamesMap("a", "b"));
```

To take the **whole params object as a single struct**, use `PackParams<T>`
(the field names of `T` become the parameter names):

```cpp
server->Method("configure", [](Config cfg){ /* ... */ },
               rpcxx::PackParams<Config>());
```

### Member functions

Methods can be bound to member functions of a `Server` subclass, either by
pointer or as a non-type template argument:

```cpp
struct MyServer : rpcxx::Server {
    MyServer() {
        Method("calc", &MyServer::calc);          // by member pointer
        Method<&MyServer::calc>("calc_alt");       // by template argument
        Method("ping", &MyServer::ping, NamesMap("ping"));
    }
    int calc(int a, int b) { return a + b; }
    std::string ping(std::string s) { return s == "ping" ? "pong" : ""; }
};
```

Like `Server` itself, subclasses must be heap-allocated and owned by
`rc::Strong` (`auto srv = rc::MakeStrong<MyServer>();`) — see
[Ref-counting](#ref-counting-includerc).

### Async handlers

Return a `Future<T>` to answer later. The server holds the response open until
the future resolves (or rejects, which is turned into a JSON-RPC error):

```cpp
server->Method("async_ping", [](std::string s) -> rpcxx::Future<std::string> {
    Promise<std::string> p;
    auto fut = p.GetFuture();
    std::thread([p = std::move(p)]() mutable { p("pong"); }).detach();
    return fut;
});
```

Override `Server::GetExecutor()` to control which executor async completions run
on.

### Routing

A server can mount other handlers under named routes (addressed with
JSON-pointer-like paths). Leading/trailing/duplicate slashes are tolerated:

```cpp
server->SetRoute("self", server);
// now reachable as "self/add", "/self/add", "/////self///add//", ...
```

### Middleware, context and exceptions

```cpp
server->AddMiddleware([](Request& req){ /* runs before every call */ });
server->AddRouteMiddleware([](string_view route, Request& req){ /* ... */ });

// Per-call key/value scratch space (see include/rpcxx/context.hpp)
ContextPtr ctx = server->CurrentContext();

// Translate C++ exceptions into RPC errors (return a replacement to override)
server->AddExceptionHandler([](ExceptionContext& ec){
    // inspect ec.method / ec.exception, log, etc.
});
```

Throwing `rpcxx::RpcException{message, ErrorCode::invalid_params, data}` from a
handler produces a structured JSON-RPC error; any other `std::exception` becomes
an `internal` error.

---

## The client

```cpp
Client client;
client.SetTransport(transport);

// Request<Ret>(Method{name, timeout_ms}, args...) -> Future<Ret>
client.Request<int>(Method{"add", NoTimeout}, 1, 2)
      .ThenSync([](int sum){ /* sum == 3 */ });

// Named arguments
client.Request<MyStruct>(Method{"copy", 5000}, rpcxx::Arg("arg", value));

// Whole struct as the params object
client.RequestPack<Result>(Method{"configure", NoTimeout}, config);

// Fire-and-forget
client.Notify("log", "hello");
```

`Method{name, timeout}` takes a timeout in milliseconds, or `NoTimeout`. The
returned `Future` can be composed (`Then`/`Try`/`AtLast`) or turned into a
`std::future` with `ToStdFuture(...).get()`.

### Batching

Group several calls into one JSON-RPC batch. Nothing is sent until the
`BatchGuard` finishes:

```cpp
auto batch = client.StartBatch();
client.Notify("log", "a");
auto f1 = client.Request<int>(Method{"add", NoTimeout}, 1, 2);
auto f2 = client.Request<std::string>(Method{"ping", NoTimeout});
batch.Finish();                 // (or let the guard go out of scope)
```

---

## Transports

A transport is the only component that touches your I/O. Implementations of
`IClientTransport` provide `SendNotify` / `SendMethod` / `SendBatch`.

- **`ForwardToHandler`** — calls a local `IHandler` directly, no serialization.
  Great for tests and in-process wiring.
- **`IAsyncTransport`** — bidirectional base for real, serialized transports.
  It correlates responses to pending requests by id, handles timeouts
  (`CheckTimeouts`, `ClearAllPending`) and serves both a `Client` and a
  server-side `IHandler` over one connection. Subclasses implement `Send`.
- **`Transport`** — a ready-made `IAsyncTransport` with a sender callback,
  perfect for hooking into an existing event loop:

```cpp
auto server = rc::MakeStrong<Server>();
server->Method("add", [](int a, int b){ return a + b; });

auto transport = rc::MakeStrong<Transport>(Protocol::json_v2_compliant);
transport->SetHandler(server);               // route inbound requests here

transport->OnReply([&](JsonView outgoing){   // outbound: serialize & ship
    my_socket.send(jv::DumpJson(outgoing));  // or DumpMsgPack(outgoing)
});

// inbound: feed received bytes back in
my_socket.onMessage([&](std::string_view bytes){
    jv::DefaultArena arena;
    transport->Receive(jv::ParseJson(bytes, arena));
});
```

The same `Transport` can also drive a `Client` (`client.SetTransport(transport)`),
which is how rpcxx supports bidirectional RPC over a single connection. See
[`test/rps`](test/rps) for a complete Qt WebSocket client/server using
MessagePack.

---

## Serialization & `DESCRIBE`

Any type that should cross an RPC boundary is described once using the
[`describe`](https://github.com/cyanidle/describe) reflection macro:

```cpp
struct User {
    std::string name;
    int age;
};

DESCRIBE("User", User) {
    MEMBER("name", &_::name);
    MEMBER("age",  &_::age);
}
```

From then on `User` can be a parameter, a return value, or nested inside other
described types. Under the hood values are converted to/from `JsonView` via
`JsonView::From(obj, arena)` and `view.Get<T>()`. Attributes on the type or
members (extra arguments to `DESCRIBE` / `MEMBER`) drive behaviours such as
`EnumAsInteger`, `SkipMissing`, `StructAsTuple` and custom validators — see
[`include/json_view/json_view.hpp`](include/json_view/json_view.hpp).

---

## Code generation

Instead of registering methods by hand, you can describe an interface in a small
Lua DSL and generate typed server and client stubs. Given `spec.lua`:

```lua
namespace "calc"

Params = struct() {
    a = int,
    b = int(0),            -- default value
}

methods("RPC") {
    add   = {#Params} >> int,    -- '#' = take Params as the params object
    sub   = {int, int} >> int,   -- positional args
    touch = {int},               -- no '>>' = request that returns void
    slow  = async {#Params} >> int,  -- server returns rpcxx::Future<int>
}

notify("RPC") {                  -- fire-and-forget (must return void)
    log = {string},
}
```

Wire it into CMake — the spec is generated and compiled in the same build:

```cmake
rpcxx_codegen(spec.lua PREFIX .generated TARGET calc_api)
target_link_libraries(my_app PRIVATE calc_api)
```

This produces `calc::RPC_Server` (abstract — you implement the virtual methods)
and `calc::RPC_Client` (typed methods returning `rpcxx::Future<T>`):

```cpp
#include ".generated/spec.hpp"

struct MyCalc : calc::RPC_Server {
    int add(calc::Params p) override { return p.a + p.b; }
    int sub(int a, int b) override   { return a - b; }
    void touch(int x) override       { /* ... */ }
    rpcxx::Future<int> slow(calc::Params p) override { /* ... */ }
    void log(std::string s) override { /* notification */ }
};

calc::RPC_Client client(transport);
client.add(calc::Params{2, 3});   // -> rpcxx::Future<int>
```

The DSL supports structs (with defaults and `Optional` fields), enums, type
aliases, `include()` of other specs, and arbitrary **attributes** (e.g.
`:attrs("my.validated")`) that are forwarded onto the generated `DESCRIBE`
declarations. The generator (`rpcxx-codegen`) targets C++ and, with limited
support, Go (`rpcxx_codegen_go`). Flags like `NO_SERVER` / `NO_CLIENT` /
`DESCRIBE_SERVER` are available on the `rpcxx_codegen` CMake function.

---

## Bundled sub-libraries (`include/`)

rpcxx is built on several independent, reusable headers. They have no dependency
on the RPC layer and can be used on their own.

### Futures (`include/future`)

A compact, allocation-light future/promise library with continuation chaining
and pluggable executors. The shared state is mutex-guarded, so a `Promise` may
be resolved from a different thread than the one attaching continuations.

| Header | What it gives you |
|---|---|
| `future/future.hpp` | `Future<T>`, `Promise<T>`, `SharedPromise<T>`, `Result<T>`; `Then` / `ThenSync`, `Try` / `TrySync`, `AtLast` / `AtLastSync`, `Catch`; `Resolved` / `Rejected` |
| `future/executor.hpp` | `Executor` interface (run continuations inline, on a pool, etc.) and `StoppableExecutor` |
| `future/gather.hpp` | `Gather(range)` and `GatherTuple(futs...)` to await many futures |
| `future/multi_future.hpp` | `MultiFuture<T>` — a future that can be awaited by many consumers |
| `future/signal.hpp` | `Signal<T>` — a settable, executor-aware callback slot |
| `future/cancel_token.hpp` | `CancelController` / `CancelSignal` for cooperative cancellation |
| `future/move_func.hpp` | `MoveFunc<Sig>` — a move-only `std::function` with small-object optimization |
| `future/to_std_fut.hpp` | `ToStdFuture(Future<T>)` → `std::future<T>` to block when you must |

```cpp
using namespace fut;

Promise<int> p;
Future<int> f = p.GetFuture();
f.ThenSync([](int x){ return x + 1; })
 .ThenSync([](int x){ /* x == 43 */ });
p(42);

// await several at once
std::vector<Future<int>> all = /* ... */;
Gather(std::move(all)).ThenSync([](std::vector<int> results){ /* ... */ });
```

### JSON view (`include/json_view`)

A zero-copy JSON document model designed for the request/response lifecycle.

- **`JsonView`** — a small, trivially-copyable view over a node. Construct from
  C++ values, navigate (`At`, `FindVal`, `[]`), and convert with `Get<T>()`.
- **Arenas (`alloc.hpp`)** — `DefaultArena<N>` bump-allocates the DOM into an
  inline stack buffer (size `N`) plus overflow; freeing a deep document is
  essentially free, and views stay cache-friendly and copyable.
- **`Json` / `MutableJson` (`json.hpp`)** — owning wrappers. `Json::Parse(str)`,
  `Json::From(obj)`, `Json::FromMsgPack(bytes)`, `.View()`; `MutableJson` for
  building/mutating documents, plus `MergePatch` / `Unflatten`.
- **Dump / parse (`dump.hpp`, `parse.hpp`)** — `DumpJson` / `DumpMsgPack` and
  `ParseJson` / `ParseMsgPack(InPlace)` over `membuff` streams, `std::istream`,
  files or in-memory buffers.
- **`pointer.hpp`** — RFC 6901 JSON Pointer support (`JsonPointer`).

```cpp
struct My { std::string field; };
DESCRIBE("My", My) { MEMBER("field", &_::field); }

jv::DefaultArena arena;
jv::JsonView view = jv::JsonView::From(My{"hello"}, arena);   // no copies
std::string text = jv::DumpJson(view);

jv::Json parsed = jv::Json::Parse(text);
auto back = parsed.View().Get<My>();
```

### Ref-counting (`include/rc`)

Intrusive smart pointers: `rc::Strong<T>` / `rc::Weak<T>` with base classes
(`DefaultBase`, `SingleVirtualBase`, `WeakableVirtual`). Used throughout for
transports and shared state.

Ownership rule: any object that takes part in a `rc::Weak` reference (servers,
handlers, transports, executors) must be heap-allocated and owned by an
`rc::Strong` from the moment it is shared. This is enforced at compile time:
`rc::WeakableVirtual` is constructible only with a passkey
(`rc::MakeStrongRef`) that just `rc::MakeStrong<T>(...)` can create, so
user-defined subclasses take the key as their first constructor parameter and
forward it to the base — stack allocation or raw `new` simply does not
compile. `rc::Weak::lock()` is the only way to dereference a weak reference;
it returns a `Strong` that keeps the object alive during use and `nullptr`
for expired ones. Objects owned by a foreign scheme (e.g. Qt parent/child)
can opt out via the `rc::foreign_owned` constructor tag: `rc` never deletes
them, `Weak` expiry is signaled by the destructor, and the object is
confined to its creator thread (asserted in debug builds).

### Buffers (`include/membuff`)

`membuff::In` / `membuff::Out` — minimal half-virtual buffer abstractions (one
virtual `Refill` / `Grow`) that the JSON parsers and dumpers stream through,
with ready helpers like `membuff::StringOut`.

### Meta (`include/meta`)

Small metaprogramming utilities (`TypeList`, function-trait helpers, container
detection, `compiler_macros.hpp` for `meta_Likely` / `meta_alwaysInline`, and a
`visit` helper) used by the rest of the codebase.

---

## Testing

With `-DRPCXX_WITH_TESTS=ON` the suite is registered with CTest:

- `rpcxx-rpc-test` — end-to-end RPC over direct / JSON / MessagePack transports
  and both protocol variants.
- `rpcxx-json-test`, `rpcxx-msgpack-test` — serialization round-trips
  (plus libFuzzer targets under Clang).
- `rpcxx-future-test` — future/promise semantics, including multithreaded cases.
- `rpcxx-future-race-test` — a threaded stress test for the future
  synchronization, intended to be run under ThreadSanitizer
  (`-DRPCXX_TEST_TSAN=ON`).
- `rpcxx-codegen-test-exe` — compiles generated stubs from
  [`test/spec.lua`](test/spec.lua).

```sh
cmake -B build -DRPCXX_WITH_TESTS=ON && cmake --build build -j
ctest --test-dir build --output-on-failure
```

---

## License

MIT. Copyright 2024 "NEOLANT Service", "NEOLANT Kaliningrad", Alexey Doronin,
Anastasia Lugovets, Dmitriy Dyakonov. See [LICENCE.txt](LICENCE.txt).
