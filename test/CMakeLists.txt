# This file is a part of RPCXX project

#[[
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
]]

enable_testing()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(rpcxx-test-deps INTERFACE)
target_link_libraries(rpcxx-test-deps INTERFACE Threads::Threads)

if(RPCXX_TEST_SANITIZERS AND NOT MSVC)
    target_compile_options(rpcxx-test-deps INTERFACE -fsanitize=undefined,address)
    target_link_options(rpcxx-test-deps INTERFACE -fsanitize=undefined,address)
endif()
target_link_libraries(rpcxx-test-deps INTERFACE rpcxx doctest)
if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(rpcxx-test-deps INTERFACE -Wpedantic -Wall -Wextra)
endif()
if(MSVC)
    target_link_options(rpcxx-test-deps INTERFACE /STACK:10000000)
endif()

function(do_register target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PUBLIC rpcxx-test-deps)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endfunction()

do_register(rpcxx-rpc-test rpcxx/rpc_test.cpp)
do_register(rpcxx-json-test rpcxx/json_test.cpp)
do_register(rpcxx-msgpack-test rpcxx/msgpack_test.cpp)
do_register(rpcxx-future-test rpcxx/future_test.cpp)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(rpcxx-test-deps INTERFACE -ftime-trace)

    add_library(rpcxx-fuzz-deps INTERFACE)
    target_link_libraries(rpcxx-fuzz-deps INTERFACE rpcxx)
    target_compile_options(rpcxx-fuzz-deps INTERFACE -fsanitize=fuzzer,address)
    target_link_options(rpcxx-fuzz-deps INTERFACE -fsanitize=fuzzer,address)

    add_executable(rpcxx-msgpack-fuzz rpcxx/msgpack_fuzzing.cpp)
    target_link_libraries(rpcxx-msgpack-fuzz PRIVATE rpcxx-fuzz-deps)

    add_executable(rpcxx-json-fuzz rpcxx/json_fuzzing.cpp)
    target_link_libraries(rpcxx-json-fuzz PRIVATE rpcxx-fuzz-deps)
endif()

add_library(rpcxx-bench-deps INTERFACE)
target_link_libraries(rpcxx-bench-deps INTERFACE rpcxx benchmark)
target_compile_options(rpcxx-bench-deps INTERFACE -fno-omit-frame-pointer)

add_executable(rpcxx-json-bench rpcxx/json_bench.cpp)
target_link_libraries(rpcxx-json-bench PUBLIC rpcxx-bench-deps)
add_executable(rpcxx-rpc-bench rpcxx/rpc_bench.cpp)
target_link_libraries(rpcxx-rpc-bench PUBLIC rpcxx-bench-deps)

if (RPCXX_WITH_CODEGEN)
    rpcxx_codegen(spec.lua
        PREFIX .generated
        TARGET rpcxx-codegen-test)
    add_executable(rpcxx-codegen-test-exe rpcxx/codegen_test.cpp)
    target_link_libraries(rpcxx-codegen-test-exe PRIVATE rpcxx-codegen-test)
endif()

if (RPCXX_TEST_RPS)
    add_subdirectory(rps)
endif()
