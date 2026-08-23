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

#include "rpcxx/transport.hpp"
#include "json_view/dump.hpp"
#include "rpcxx/protocol.hpp"
#include <optional>
#include <unordered_map>
using namespace rpcxx;
using namespace std::chrono;
namespace {
struct Transact {
    string method;
    Promise<JsonView> prom;
    millis timeout;
};

static void error(string loc, std::exception& e) {
    fprintf(stderr, "RPC: Unexpected in '%s': %s\n", loc.c_str(), e.what());
}

}

struct IAsyncTransport::Impl {
    std::atomic<size_t> id{0};
    Protocol proto = {};
    std::mutex mut; // guards pending, handler, last
    std::unordered_map<size_t, Transact> pending;
    rc::Weak<IHandler> handler = nullptr;
    steady_clock::time_point last = steady_clock::now();
    rc::Strong<StoppableExecutor> exec = new StoppableExecutor;

    ~Impl() {
        // Quiesce in-flight sendResult/addBatchResp jobs first: they capture
        // the transport as a raw pointer and must not run past destruction.
        exec->Stop();
        decltype(pending) rest;
        {
            std::lock_guard lk(mut);
            rest.swap(pending);
        }
        // Broken-promise rejection of still-pending requests happens here,
        // outside the lock (continuations may re-enter the transport).
    }

    template<typename Fn>
    void wrapNotif(string_view method, JsonView params, Fn f) {
        if (proto == Protocol::json_v2_compliant) {
            Formatter<Protocol::json_v2_compliant> fmt;
            f(fmt.MakeNotify(method, params));
        } else {
            Formatter<Protocol::json_v2_minified> fmt;
            f(fmt.MakeNotify(method, params));
        }
    }
    template<typename Fn>
    void wrapMethod(size_t next, string_view method, JsonView params, Fn f) {
        if (proto == Protocol::json_v2_compliant) {
            Formatter<Protocol::json_v2_compliant> fmt;
            f(fmt.MakeRequest(next, method, params));
        } else {
            Formatter<Protocol::json_v2_minified> fmt;
            f(fmt.MakeRequest(next, method, params));
        }
    }

    template<Protocol proto>
    static void sendResult(IAsyncTransport* self, JsonView id, Result<JsonView> res) noexcept try {
        Formatter<proto> fmt;
        try {
            self->Send(fmt.MakeResponce(id, res.get()));
        } catch (RpcException& e) {
            self->Send(fmt.MakeError(id, e));
        } catch (std::exception& e) {
            RpcException wrap(e.what(), ErrorCode::internal);
            self->Send(fmt.MakeError(id, wrap));
        }
    } catch (std::exception& e) {
        error("sendResult()", e);
    }

    rc::Strong<IHandler> getHandler(IAsyncTransport* self) {
        rc::Strong<IHandler> h;
        {
            std::lock_guard lk(mut);
            h = handler.lock();
        }
        if (meta_Unlikely(!h)) {
            self->NoServerFound();
            return nullptr;
        } else {
            return h;
        }
    }

    template<Protocol proto>
    void handleServer(IAsyncTransport* self, string_view method, JsonView req, ContextPtr ctx, Arena& alloc) {
        using F = Fields<proto>;
        TraceFrame root;
        TraceFrame reqFrame("<request>", root);
        const JsonView* idPtr = req.FindVal(F::Id);
        auto p = req.Value(F::Params, EmptyArray(), reqFrame);
        Request prepReq{alloc};
        prepReq.method = Method{method, rpcxx::NoTimeout};
        prepReq.params = p;
        prepReq.context = ctx;
        if (!idPtr) {
            // No 'id' member: a notification — no response is ever sent.
            if (auto h = getHandler(self)) {
                try {
                    h->HandleNotify(prepReq);
                } catch (std::exception& e) {
                    // Notifications never produce a response; a throwing
                    // notify handler must not escape Receive().
                    error("HandleNotify (" + string{method} + ')', e);
                }
            }
            return;
        }
        auto id = *idPtr;
        if (meta_Unlikely(id.Is(t_null))) {
            // "id": null is not a notification but an invalid Request object;
            // JSON-RPC 2.0 answers it with an Invalid Request error (id null).
            try {
                Formatter<proto> fmt;
                RpcException err("The 'id' must not be null", ErrorCode::invalid_request);
                self->Send(fmt.MakeError(JsonView{}, err));
            } catch (std::exception& e) {
                error("send null-id error", e);
            }
            return;
        }
        Promise<JsonView> cb;
        cb.GetFuture()
            .AtLast(exec, [self, id = jv::Json(id)](auto result) mutable noexcept {
                sendResult<proto>(self, id.View(), result);
            });
        if (auto h = getHandler(self)) {
            h->Handle(prepReq, std::move(cb));
        }
    }

    struct Batch : rc::DefaultBase {
        IAsyncTransport* self;
        size_t left;
        std::mutex mut; // guards left + parts: parts complete on arbitrary threads
        std::vector<JsonView> parts;
        DefaultArena<1024> alloc;

        JsonView Result() {
            return JsonView(parts.data(), unsigned(parts.size()));
        }
    };

    // Decrement the pending-parts counter; send the assembled batch response
    // once the last part completes. Parts complete on arbitrary threads
    // (continuations run inline), so left/parts are guarded by b.mut.
    static void finishBatchPart(Batch& b) noexcept {
        std::lock_guard lk(b.mut);
        if (!--b.left && !b.parts.empty()) {
            try {
                b.self->Send(b.Result());
            } catch (std::exception& e) {
                error("send batch responce", e);
            }
        }
    }

    template<Protocol proto>
    static void addBatchResp(JsonView id, Batch& b, Result<JsonView> res) noexcept try {
        Formatter<proto> fmt;
        JsonView part;
        {
            // The arena and parts are shared with the other parts of this
            // batch completing concurrently: hold the lock while copying.
            std::lock_guard lk(b.mut);
            try {
                part = Copy(fmt.MakeResponce(id, res.get()), b.alloc);
            } catch (RpcException& e) {
                part = Copy(fmt.MakeError(id, e), b.alloc);
            } catch (std::exception& e) {
                RpcException wrap(e.what(), ErrorCode::internal);
                part = Copy(fmt.MakeError(id, wrap), b.alloc);
            }
            try {
                b.parts.push_back(part);
            } catch (std::exception& e) {
                error("push into batch", e);
            }
        }
        finishBatchPart(b);
    } catch (std::exception& e) {
        error("addBatchResponce()", e);
    }

    // I HATE JSONRPC 2.0
    template<Protocol proto>
    void handleServerBatch(IAsyncTransport* self, JsonView req, ContextPtr ctx, Arena& alloc) {
        using F = Fields<proto>;
        unsigned idx = 0;
        TraceFrame root;
        TraceFrame batchFrame("<batch>", root);
        rc::Strong<Batch> batch = new Batch;
        batch->left = 1; // one reference held by this dispatch loop itself
        batch->self = self;
        auto h = getHandler(self);
        if (!h) return;
        for (auto part: req.Array(false)) {
            try {
                TraceFrame frame(idx++, batchFrame);
                const JsonView* idPtr = part.FindVal(F::Id);
                auto p = part.Value(F::Params, EmptyArray(), frame);
                auto method = part.At(F::Method, frame).GetString(TraceFrame(F::Method, frame));
                Request req{alloc};
                req.method = Method{method, rpcxx::NoTimeout};
                req.params = p;
                req.context = ctx;
                if (!idPtr) {
                    // No 'id' member: a notification — never answered.
                    try {
                        h->HandleNotify(req);
                    } catch (std::exception& e) {
                        // Notifications never produce a response, not even an
                        // error one inside a batch — log instead of adding a
                        // part or letting the exception escape Receive().
                        error("HandleNotify (" + string{method} + ')', e);
                    }
                } else if (meta_Unlikely(idPtr->Is(t_null))) {
                    // "id": null is an invalid Request object, not a
                    // notification: answer it with an error part (id null).
                    Formatter<proto> fmt;
                    RpcException err("The 'id' must not be null", ErrorCode::invalid_request);
                    std::lock_guard lk(batch->mut);
                    batch->parts.push_back(Copy(fmt.MakeError(nullptr, err), batch->alloc));
                } else {
                    auto id = *idPtr;
                    Promise<JsonView> cb;
                    cb.GetFuture()
                        .AtLast(exec, [batch, id = Json(id)](auto result) mutable noexcept {
                            addBatchResp<proto>(id.View(), *batch, result);
                        });
                    {
                        // Mark the part as pending BEFORE dispatching: the
                        // handler may complete it inline (or on another
                        // thread) from inside Handle().
                        std::lock_guard lk(batch->mut);
                        batch->left++;
                    }
                    h->Handle(req, std::move(cb));
                }
            } catch (RpcException& e) {
                Formatter<proto> fmt;
                std::lock_guard lk(batch->mut);
                batch->parts.push_back(Copy(fmt.MakeError(nullptr, e), batch->alloc));
            } catch (std::exception& e) {
                Formatter<proto> fmt;
                RpcException wrap(e.what(), ErrorCode::internal);
                std::lock_guard lk(batch->mut);
                batch->parts.push_back(Copy(fmt.MakeError(nullptr, wrap), batch->alloc));
            }
        }
        // Release the dispatch loop's own reference. If every part already
        // completed (inline), this sends the batch response. An all-notify
        // batch has no parts and gets no response, per JSON-RPC 2.0.
        finishBatchPart(*batch);
    }
    template<Protocol proto>
    void handleRespToClient(JsonView resp) {
        using F = Fields<proto>;
        const JsonView* id = resp.FindVal(F::Id);
        if (meta_Unlikely(!id)) {
            throw RpcException("Could not find 'id' in responce to client", ErrorCode::parse);
        }
        TraceFrame root;
        auto num = id->Get<size_t>(TraceFrame{F::Id, root});
        Promise<JsonView> prom;
        {
            std::lock_guard lk(mut);
            auto p = pending.find(num);
            if (meta_Unlikely(p == pending.end())) {
                JsonPair data[] = {{"was_id", num}};
                throw RpcException("Could not find match id with any pending request => " + resp.Dump(),
                                   ErrorCode::invalid_request,
                                   jv::Json(data));
            }
            prom = std::move(p->second.prom);
            // Erase BEFORE fulfilling: the promise's continuations run inline
            // and may re-enter addPending(), invalidating the iterator.
            pending.erase(p);
        }
        if (const JsonView* r = resp.FindVal(F::Result); meta_Likely(r)) {
            prom(*r);
        } else if (const JsonView* e = resp.FindVal(F::Error)) {
            prom(e->Get<RpcException>(TraceFrame{F::Error, TraceFrame{}}));
        } else {
            throw RpcException("missing 'error' or 'result' fields", ErrorCode::invalid_request);
        }
    }
    template<Protocol proto>
    void handle(IAsyncTransport* self, JsonView msg, ContextPtr ctx, Arena& alloc) {
        using F = Fields<proto>;
        auto method = msg.FindVal(F::Method);
        if (!method) {
            handleRespToClient<proto>(msg);
        } else {
            handleServer<proto>(self, method->GetString(TraceFrame{F::Method, TraceFrame{}}), msg, ctx, alloc);
        }
    }
    template<Protocol proto>
    void handleBatch(IAsyncTransport* self, JsonView msg, ContextPtr ctx, Arena& alloc) {
        using F = Fields<proto>;
        auto arr = msg.Array(false);
        auto method = arr.begin()[0].FindVal(F::Method, TraceFrame("<batch.part>", TraceFrame{}));
        if (!method) {
            for (auto part: msg.Array()) {
                try {
                    handleRespToClient<proto>(part);
                } catch (...) {
                    //pass => exception in one of responces is not an error
                }
            }
        } else {
            handleServerBatch<proto>(self, msg, ctx, alloc);
        }
    }
    void addPending(string method, size_t id, Promise<JsonView> cb, millis timeout) {
        Transact tr{std::move(method), std::move(cb), timeout};
        std::optional<Promise<JsonView>> displaced;
        string displacedMethod;
        {
            std::lock_guard lk(mut);
            auto [iter, ok] = pending.try_emplace(id, std::move(tr));
            if (meta_Unlikely(!ok)) {
                displaced.emplace(std::move(iter->second.prom));
                displacedMethod = std::move(iter->second.method);
                iter->second = std::move(tr);
            }
        }
        // Reject the displaced request outside the lock (continuations run
        // inline and may re-enter the transport).
        if (displaced) {
            (*displaced)(FutureError(displacedMethod + ": Timeout Error"));
        }
    }
    // Erase a pending request and reject it with `exc`. Used when the
    // outgoing Send() failed: the request never reached the wire, so leaving
    // it pending would just hang the caller until the timeout fires.
    void rejectPending(size_t id, std::exception_ptr exc) noexcept {
        std::optional<Promise<JsonView>> prom;
        {
            std::lock_guard lk(mut);
            auto p = pending.find(id);
            if (p == pending.end()) return;
            prom.emplace(std::move(p->second.prom));
            // Erase BEFORE fulfilling: continuations run inline.
            pending.erase(p);
        }
        (*prom)(std::move(exc));
    }
};

IAsyncTransport::IAsyncTransport(rc::WeakableKey key, Protocol proto, rc::Weak<IHandler> h)
    : IClientTransport(key)
{
    d->handler = h;
    d->proto = proto;
}

rc::Weak<IHandler> IAsyncTransport::SetHandler(rc::Weak<IHandler> handler)
{
    std::lock_guard lk(d->mut);
    return std::exchange(d->handler, handler);
}

void IAsyncTransport::ClearAllPending()
{
    std::unordered_map<size_t, Transact> all;
    {
        std::lock_guard lk(d->mut);
        all.swap(d->pending);
    }
    // Reject outside the lock: continuations run inline and may re-enter.
    for (auto& [_, t]: all) {
        t.prom(FutureError("Manual Cancel"));
    }
}

void IAsyncTransport::TimeoutHappened(string_view method, Promise<JsonView> &target)
{
    target(FutureError(string{method} + ": Timeout Error"));
}

void IAsyncTransport::NoServerFound()
{
    throw RpcException("Server not registered", ErrorCode::internal);
}

void IAsyncTransport::SendBatch(Batch batch)
{
    DefaultArena alloc;
    auto _totalCount = batch.methods.size() + batch.notifs.size();
    auto size = unsigned(_totalCount);
    if (meta_Unlikely(size < _totalCount)) {
        throw std::runtime_error("Batch is too big");
    }
    auto arr = MakeArrayOf(size, alloc);
    unsigned idx = 0;
    std::vector<size_t> ids;
    ids.reserve(batch.methods.size());
    for (auto& n: batch.notifs) {
        d->wrapNotif(n.method, n.params.View(), [&](JsonView formatted){
            arr[idx++] = Copy(formatted, alloc);
        });
    }
    for (auto& m: batch.methods) {
        auto next = d->id++;
        ids.push_back(next);
        d->addPending(m.method, next, std::move(m.cb), m.timeout);
        d->wrapMethod(next, m.method, m.params.View(), [&](JsonView formatted){
            arr[idx++] = Copy(formatted, alloc);
        });
    }
    try {
        Send(JsonView(arr, size));
    } catch (std::exception& e) {
        // The batch never reached the wire: reject every pending request of
        // the batch immediately instead of letting them hang until timeout.
        for (auto id: ids) {
            d->rejectPending(id, std::make_exception_ptr(
                RpcException("Send Batch Request failed: " + string{e.what()}, ErrorCode::internal)));
        }
    }
}

void IAsyncTransport::SendNotify(string_view method, JsonView params) try
{
    d->wrapNotif(method, params, [&](JsonView req){
        Send(req);
    });
} catch (std::exception& e) {
    error("Send Notify (" + string{method} + ')', e);
}

void IAsyncTransport::SendMethod(Method method, JsonView params, Promise<JsonView> cb)
{
    auto next = d->id++;
    d->addPending(string{method.name}, next, std::move(cb), method.timeout);
    try {
        d->wrapMethod(next, method.name, params, [&](JsonView req){
            Send(req);
        });
    } catch (std::exception& e) {
        // The request never reached the wire: reject it immediately instead
        // of leaving it pending until the timeout fires.
        d->rejectPending(next, std::make_exception_ptr(
            RpcException("Send Method failed: " + string{e.what()}, ErrorCode::internal)));
    }
}

IAsyncTransport::~IAsyncTransport()
{

}

void IAsyncTransport::CheckTimeouts()
{
    auto now = steady_clock::now();
    std::vector<std::pair<string, Promise<JsonView>>> expired;
    {
        std::lock_guard lk(d->mut);
        auto diff = duration_cast<milliseconds>(now - d->last).count();
        d->last = now;
        auto it = d->pending.begin();
        while (it != d->pending.end()) {
            if (it->second.timeout == NoTimeout) {
                ++it;
            } else if (it->second.timeout > diff) {
                it->second.timeout -= diff;
                ++it;
            } else {
                // Take the entry out before fulfilling: continuations run
                // inline from TimeoutHappened() and may re-enter the map.
                expired.emplace_back(std::move(it->second.method), std::move(it->second.prom));
                it = d->pending.erase(it);
            }
        }
    }
    for (auto& [method, prom]: expired) {
        TimeoutHappened(method, prom);
    }
}

void IAsyncTransport::Receive(JsonView msg, ContextPtr ctx)
{
    DefaultArena alloc;
    if (msg.Is(t_array)) {
        if (meta_Unlikely(msg.Array(false).size() == 0)) {
            throw RpcException("Empty batch array", ErrorCode::invalid_request);
        }
        switch (d->proto) {
        case Protocol::json_v2_compliant:
            return d->handleBatch<Protocol::json_v2_compliant>(this, msg, ctx, alloc);
        case Protocol::json_v2_minified:
            return d->handleBatch<Protocol::json_v2_minified>(this, msg, ctx, alloc);
        }
    } else if (meta_Unlikely(!msg.Is(jv::t_object))) {
        JsonPair data[] = {{"was_type", msg.GetTypeName()}};
        throw RpcException("Request/Responce should be an array or object",
                           ErrorCode::invalid_request,
                           jv::Json(data));
    } else {
        switch (d->proto) {
        case Protocol::json_v2_compliant:
            return d->handle<Protocol::json_v2_compliant>(this, msg, ctx, alloc);
        case Protocol::json_v2_minified:
            return d->handle<Protocol::json_v2_minified>(this, msg, ctx, alloc);
        }
    }
}

void IAsyncTransport::Receive(JsonView msg)
{
    Receive(msg, new Context);
}

rc::Weak<IHandler> ForwardToHandler::SetHandler(rc::Weak<IHandler> handler)
{
    return std::exchange(h, handler);
}

void ForwardToHandler::SendBatch(Batch batch)
{
    for (auto& m: batch.methods) {
        SendMethod(Method{m.method, m.timeout}, m.params.View(), std::move(m.cb));
    }
    for (auto& n: batch.notifs) {
        SendNotify(n.method, n.params.View());
    }
}

void ForwardToHandler::SendNotify(string_view method, JsonView params)
{
    DefaultArena alloc;
    if (auto handler = h.lock(); meta_Likely(handler)) {
        Request req{alloc};
        req.method = Method{method, rpcxx::NoTimeout};
        req.params = params;
        handler->HandleNotify(req);
    }
}

void ForwardToHandler::SendMethod(Method method, JsonView params, Promise<JsonView> cb)
{
    DefaultArena alloc;
    if (auto handler = h.lock(); meta_Likely(handler)) {
        Request req{alloc};
        req.method = method;
        req.params = params;
        handler->Handle(req, std::move(cb));
    }
}

void IClientTransport::DoHandle(Request& req, Promise<JsonView> cb) noexcept try {
    SendMethod(req.method, req.params, std::move(cb));
} catch (std::exception& e) {
    error("Client => forward method", e);
}

void IClientTransport::DoHandleNotify(Request& req) noexcept try {
    SendNotify(req.method.name, req.params);
} catch (std::exception& e) {
    error("Client => forward notify", e);
}

Transport::Transport(rc::WeakableKey key, Protocol proto) : IAsyncTransport(key, proto)
{

}

void Transport::OnReply(Sender cb)
{
    sender = std::move(cb);
}

void Transport::Send(JsonView msg)
{
    if (!sender) {
        throw std::runtime_error("Could not send: sender not registered");
    }
    sender(msg);
}
