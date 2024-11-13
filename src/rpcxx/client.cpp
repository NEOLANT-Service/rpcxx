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

#include "rpcxx/client.hpp"

using namespace rpcxx;

Client::Client(rc::Weak<IClientTransport> t) {
    SetTransport(std::move(t));
}

Client::~Client()
{

}

rc::Weak<IClientTransport> Client::GetTransport() noexcept {
    return transport;
}

rc::Weak<IClientTransport> Client::SetTransport(rc::Weak<IClientTransport> t) noexcept {
    return std::exchange(transport, std::move(t));
}

static string addPref(string_view base, string_view prefix) {
    if (prefix.empty()) {
        return string{base};
    } else {
        return string{prefix} + '/' + string{base};
    }
}

void Client::NotifyRaw(string_view method, JsonView params) {
    if (batchActive) {
        currentBatch.notifs.push_back(RequestNotify{addPref(method, prefix), Json{params}});
    } else {
        tr().SendNotify(addPref(method, prefix), params);
    }
}

void Client::SetPrefix(string prefix)
{
    this->prefix.swap(prefix);
}

IClientTransport &Client::tr() {
    auto tr = transport.peek();
    if (meta_Unlikely(!tr)) {
        throw ClientTransportMissing{};
    }
    return *tr;
}

void Client::batchDone() {
    if (!batchActive) {
        throw std::runtime_error("Batch was not active");
    }
    tr().SendBatch(std::move(currentBatch));
    batchActive = false;
}

void Client::sendRequest(Promise<JsonView> cb, Method method, JsonView params) {
    if (batchActive) {
        auto name = addPref(method.name, prefix);
        RequestMethod meth;
        meth.method = name;
        meth.params = Json{params};
        meth.timeout = method.timeout;
        meth.cb = std::move(cb);
        currentBatch.methods.push_back(std::move(meth));
    } else {
        tr().SendMethod(Method{addPref(method.name, prefix), method.timeout}, params, std::move(cb));
    }
}
