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

#include "json_view/dump.hpp"
#include "json_view/parse.hpp"
#include "rpcxx/rpcxx.hpp"
#include <QCoreApplication>
#include <QWebSocket>
#include <QTimer>
#include <QThread>
#include <chrono>
#include <iostream>
#include <ostream>
#include "common.hpp"

using namespace rpcxx;

class TestClient final: public QObject, public rpcxx::Client
{
    Q_OBJECT
public:
    TestClient(QWebSocket* sock) : sock(sock)
    {
        SetTransport(new WsTransport(sock));
        connect(sock, &QWebSocket::binaryMessageReceived, this, [&](const QByteArray& frame){
            DefaultArena alloc;
            auto msg = ParseMsgPackInPlace({frame.constData(), unsigned(frame.size())}, alloc);
            if (prom.IsValid()) {
                prom(Json{msg});
            }
        });
    }

    QWebSocket* sock;
    Promise<Json> prom;
};

static std::atomic<unsigned> runs = {};
static std::atomic<bool> print = {};

void onDone() {
    runs.fetch_add(1, std::memory_order_relaxed);
    bool was = true;
    if (print.compare_exchange_strong(was, false, std::memory_order_relaxed, std::memory_order_relaxed)) {
        was = true;
        std::cout << "RPS: " << runs << std::endl;
        runs = 0;
    }
}

const std::string bigStr = std::string(3000, '#');

void run_concat(TestClient* cli)
{
    cli->Request<std::string_view>(rpcxx::Method{"concat", 3000}, bigStr, bigStr).AtLastSync([cli](auto res){
        onDone();
        run_concat(cli);
    });
}

void run_calc(TestClient* cli)
{
    cli->Request<int>(rpcxx::Method{"calc", 3000}, 512312, -5123123).AtLastSync([cli](auto res){
        onDone();
        run_calc(cli);
    });
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        qDebug() << "Usage: " << argv[0] << " <connections> <per/threads> <method>";
        return -1;
    }
    auto conns = strtoul(argv[1], nullptr, 10);
    auto thrs = strtoul(argv[2], nullptr, 10);
    std::string_view method = argv[3];
    if (!conns) conns = 1;
    if (!thrs) thrs = 1;
    QCoreApplication app(argc, argv);
    QTimer tmr;
    tmr.callOnTimeout([&]{
        print = true;
    });
    if (method != "calc" && method != "concat") {
        qDebug() << "Invalid method: " << method.data();
        qDebug() << "Available: [calc, concat]";
        return -2;
    }
    tmr.start(1000);
    qDebug() << "starting rps test with " << conns
             << "connections per thread (threads: " << thrs << ")";
    for (auto t = 0u; t < thrs; ++t) {
        auto thread = new QThread;
        app.connect(thread, &QThread::started, [conns, method]{
            for (auto i = 0u; i < conns; ++i) {
                auto sock = new QWebSocket;
                auto cli = new TestClient(sock);
                sock->connect(sock, &QWebSocket::stateChanged, [](auto state){
                    if (state == QAbstractSocket::SocketState::UnconnectedState) {
                        qDebug() << "could not connect";
                        std::exit(1);
                    }
                });
                sock->connect(sock, &QWebSocket::connected, [cli, method]{
                    if (method == "calc") {
                        run_calc(cli);
                    } else {
                        run_concat(cli);
                    }
                });
                sock->open(QUrl("ws://127.0.0.0:6000"));
            }
        });
        thread->start();
    }
    return app.exec();
}

#include "rps_client.moc"
