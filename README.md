# RPCXX - Transport-agnostic (JsonRPC 2.0) cpp library

```cpp
rpcxx::Server server;
rpcxx::Transport transport;

server->SetTransport(&transport);

transport.OnReply([](rpcxx::JsonView reply){
    MySendString(reply.Dump());
});
OnMyRequest([&](string_view msg){
    transport.Receive(rpcxx::Json::From(msg).View());
});

// params as array
server.Method("add", [](int a, int b){
    return a + b;
});
// Receive params as object
server.Method("named_sub", [](size_t a, size_t b){
    return a - b
}, rpcxx::NamesMap("a", "b"));

myEventLoop.run();
```

## JsonView
* Makes a structured View into the object to be serialized.
* Support great customizability with DESCRIBE attributes
* Uses Arenas for allocating the DOM - perfect memory model for RPC requests
* Arenas make freeing deep DOMs very fast
* Arenas make JsonViews trivially-copyable
* Arenas provide great cache-locality
```cpp
struct My {
    string field;
};
DESCRIBE(My, &_::field)

My obj{"VeryLongString.............. etc."};
jv::DefaultArena alloc;
jv::JsonView::From(obj, alloc); 
// No copies. 
// 16 bytes bump-allocated for single JsonPair (Json object of size 1)
// jv::DefaultArena<N = 2048> has an extra size N stack buffer
```

## Codegen - Generate Server and Client stubs!

* Custom DSL for describing RPC and Structures. 
* Based on LUA 5.4+. 
* Supports: cpp, go(limited)
* Can be built and immedieately used in single CMAKE build.
* Can be used for generating Json config boilerplate

```lua
namespace "my"

config = include("config.lua")

CompressionAlgo = Alias(config.CompressionAlgo)

SessionResult = struct() {
    id = string,
    version = string,
}

Locale = enum() {
    "ru_RU"
}

L10n = struct() {
    errors = Locale("ru_RU"),
}

SessionFeatures = struct() {
    rpc_compression = {
        enabled = bool(false),
        threshhold = u64(1024),
        algo = CompressionAlgo(CompressionAlgo.gzip)
    },
    localization = Optional(L10n),
}

SessionParams = struct() {
    reconnectId = Optional(string),
    features = Optional(SessionFeatures)
}

CloseParams = struct() {
    allow_reconnect = Optional(bool),
    reason = Optional(string),
}

methods("Server_Name") {
    initialize_session = async {#SessionParams} >> SessionResult,
    close_session = {#CloseParams},
}
```