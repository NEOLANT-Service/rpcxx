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
#include <iostream>

using namespace rpcxx;

// see http://llvm.org/docs/LibFuzzer.html
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    try
    {
        DefaultArena alloc;
        auto orig = rpcxx::ParseMsgPackInPlace(data, size, alloc).result;
        try
        {
            auto back = orig.DumpMsgPack();
            auto round = rpcxx::Json::FromMsgPack(back);
            if(!DeepEqual(round.View(), orig)) {
                std::cerr << "Missmatch: \n"
                          << orig.Dump(true)
                          << "\n not equal to: "
                          << round.View().Dump(true)
                          << "\n serialized: ";
                for (auto ch: back) {
                    std::cerr << ",0x" << std::hex << ch;
                }
                std::cerr << std::endl;
                std::exit(1);
            }
        }
        catch (std::bad_alloc&) {}
        catch (jv::DepthError&) {}
        catch (...) {
            // parsing a MessagePack serialization must not fail
            assert(false);
        }
    }
    catch (std::bad_alloc&) {}
    catch (std::runtime_error&) {}
    catch (jv::DepthError&) {}
    // return 0 - non-zero return values are reserved for future use
    return 0;
}
