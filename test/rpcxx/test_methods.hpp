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

#include <rpcxx/rpcxx.hpp>
#include <string>
#include <thread>

using namespace std::chrono_literals;

struct Small {
    std::string field0;
    std::string field1;
    std::string field2;
    std::string field3;
    std::string field4;
    std::string field5;
    std::string field6;
    std::string field7;
    std::string field8;
    std::string field9;
    std::string field10;
    std::string field11;
    std::string field12;
    std::string field13;
};

DESCRIBE("Small", Small) {
    MEMBER("field0", &_::field0);
    MEMBER("field1", &_::field1);
    MEMBER("field2", &_::field2);
    MEMBER("field3", &_::field3);
    MEMBER("field4", &_::field4);
    MEMBER("field5", &_::field5);
    MEMBER("field6", &_::field6);
    MEMBER("field7", &_::field7);
    MEMBER("field8", &_::field8);
    MEMBER("field9", &_::field9);
    MEMBER("field10", &_::field10);
    MEMBER("field11", &_::field11);
    MEMBER("field12", &_::field12);
    MEMBER("field13", &_::field13);
}

struct RpcBig {
    std::vector<Small> parts;
    std::vector<double> nums;
};

DESCRIBE("RpcBig", RpcBig) {
    MEMBER("parts", &_::parts);
    MEMBER("nums", &_::nums);
}

struct TestServer : rpcxx::Server {
    TestServer() {
        Notify("notification2", &TestServer::notification2);
        Notify("notification3", [](int, int, std::string){

        });
        Notify("notification", &TestServer::notification);
        Method("throws_error", &TestServer::throws_error);
        Method("ping", &TestServer::ping);
        Method("method", &TestServer::method);
        Method("big", &TestServer::big_method);
        Method("big_named", &TestServer::big_method, rpcxx::NamesMap("body"));
        Method("async_ping", &TestServer::async_ping);
        Method("async_ping_2", []() -> rpcxx::Future<int> {
            return rpcxx::Future<int>::FromFunction([](auto prom){
                std::thread([MV(prom)]() noexcept {
                    std::this_thread::sleep_for(30ms);
                    prom(2);
                }).detach();
            });
        });
        Notify("notification_silent", &TestServer::notification_silent);
        Notify("notification_silent_named", &TestServer::notification_silent, rpcxx::NamesMap("a", "b"));
        Method("calc", &TestServer::calc);
        Method<&TestServer::calc>("calc_template");
        Method("calc_named", &TestServer::calc, rpcxx::NamesMap("a", "b"));
        Notify("notification2_named",
               &TestServer::notification2,
               rpcxx::NamesMap("a", "b"));
        Notify("notification2_named_second", [](int, int){}, rpcxx::NamesMap("a_1", "b_1"));
        Method("ping_named",
               &TestServer::ping,
               rpcxx::NamesMap("ping"));
    }
    RpcBig big_method(RpcBig request) {
        for (auto& part: request.parts) {
            part.field0 += part.field8;
            part.field1 += part.field9;
            part.field2 += part.field10;
            part.field3 += part.field11;
            part.field4 += part.field12;
            part.field5 += part.field13;
            part.field6 += part.field7;
        }
        uint32_t sum = 0;
        for (auto& i : request.nums) {
            i = sum += i;
        }
        return request;
    }
    void method() {}
    void notification_silent(int&& a, int b) {
        a = b;
    }
    void notification() {}
    void notification2(int, int) {}
    void notification3(int, int, std::string) {}
    int calc(int a, int b) {
        return a + b;
    }
    fut::Future<std::string> async_ping(std::string ping) {
        if (ping != "ping") {
            throw std::runtime_error("not ping");
        }
        fut::Promise<std::string> prom;
        auto fut = prom.GetFuture();
        std::thread([MV(prom)]() noexcept {
            std::this_thread::sleep_for(50ms);
            prom("pong");
        }).detach();
        return fut;
    }
    std::string throws_error(std::string param) {
        throw std::runtime_error(std::move(param));
    }
    std::string ping(std::string ping) {
        if (ping != "ping") {
            throw std::runtime_error("not ping");
        }
        return "pong";
    }
};
