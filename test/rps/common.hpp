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

#pragma once
#include <QWebSocket>
#include <rpcxx/rpcxx.hpp>

struct WsTransport final : public QObject, rpcxx::IAsyncTransport {
    WsTransport(QWebSocket* ws) :
        QObject(ws),
        IAsyncTransport(rpcxx::Protocol::json_v2_minified),
        sock(ws)
    {
        connect(ws, &QWebSocket::binaryMessageReceived, this, [this](QByteArray msg){
            jv::DefaultArena alloc;
            Receive(jv::ParseMsgPackInPlace(msg.constData(), msg.size(), alloc).result);
        });
    }
    void Send(jv::JsonView msg) final {
        membuff::StringOut<QByteArray> out;
        DumpMsgPackInto(out, msg);
        sock->sendBinaryMessage(out.Consume());
    }
    QWebSocket* sock;
};
