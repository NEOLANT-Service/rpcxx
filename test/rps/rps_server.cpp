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

#include "rpcxx/rpcxx.hpp"
#include <QWebSocketServer>
#include <QCoreApplication>
#include <QWebSocket>
#include <QTimer>
#include "common.hpp"

static size_t conns = 0;

using namespace rpcxx;

class TestServer final: public QObject, public rpcxx::Server
{
    Q_OBJECT
public:
    ~TestServer() override {
        conns--;
    }
    // Foreign-owned (rc::foreign_owned): parented to the socket, so Qt
    // deletes us when the connection dies — no rc::Strong, no MakeStrong.
    // Single-threaded (GUI thread) by contract; the transport's Weak<IHandler>
    // to us expires via ~WeakableVirtual when we are destroyed.
    TestServer(QWebSocket* sock_) :
        QObject(sock_),
        rpcxx::Server(rc::foreign_owned),
        sock(sock_)
    {
        conns++;
        // Deleting the socket on disconnect deletes us (its child) too.
        connect(sock_, &QWebSocket::disconnected, sock_, &QObject::deleteLater);
        transport = rc::MakeStrong<WsTransport>(sock);
        transport->SetHandler(this);
        Method("calc", [](int a, int b){
            return a + b;
        });
        Method("concat", [](string_view a, string_view b){
            string res;
            res.reserve(a.size() + b.size());
            res += a;
            res += b;
            return res;
        });
    }
    QWebSocket* sock;
    rc::Strong<WsTransport> transport;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QWebSocketServer server("test", QWebSocketServer::NonSecureMode);
    app.connect(&server, &QWebSocketServer::newConnection, [&]{
        while(server.hasPendingConnections()) {
            // Owned by the socket via Qt parent/child — dies with the
            // connection (see TestServer's ctor), no rc ownership at all.
            new TestServer(server.nextPendingConnection());
        }
    });
    if (!server.listen(QHostAddress("0.0.0.0"), 6000)) {
        return -1;
    }
    auto tmr = QTimer{};
    tmr.callOnTimeout([]{
        qDebug() << "Serving for: " << conns << " connections";
    });
    tmr.start(1000);
    return app.exec();
}

#include "rps_server.moc"
