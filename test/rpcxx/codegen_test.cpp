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
#include ".generated/spec.hpp"

// forward-declared attrs defs
namespace my { struct attr {}; }
namespace my { struct enumAttrs {}; }
namespace my { struct deep {}; }
namespace my { struct deep_field {}; }

struct validated{};

struct Consumer : test::RPC_Server
{
    Consumer(rc::MakeStrongRef key) : RPC_Server(key) {}
};

struct ForeignConsumer : test::RPC_Server
{
    ~ForeignConsumer() override = default; // public: the foreign owner deletes
    ForeignConsumer() : RPC_Server(rc::foreign_owned) {}
    void b(test::Params) override {}
    int32_t a() override { return 42; }
    void c(test::Params) override {}
};

int main()
{
    // Foreign-owned generated server: created with plain `new`, lockable via
    // rc::Weak although no rc::Strong owns it, expired when the owner deletes.
    rc::Weak<rpcxx::IHandler> weak;
    auto* server = new ForeignConsumer();
    weak = server;
    {
        rc::Strong<rpcxx::IHandler> locked = weak.lock();
        if (locked.get() == nullptr) return 1;
        // The constructor body must have registered the generated methods.
        if (!server->IsMethodRegistered("a")) return 1;
    } // Unref drops the refcount to zero — must NOT delete
    if (weak.lock().get() == nullptr) return 1;
    delete server; // the "Qt parent" deletes it
    if (weak.lock().get() != nullptr) return 1;
    return 0;
}
