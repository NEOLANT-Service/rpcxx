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


cmake_minimum_required(VERSION 3.16)

project(rpcxx VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS         "Build libs as shared"         OFF)
option(RPCXX_WITH_CODEGEN        "Compile Code Generator"       ON)
option(RPCXX_TEST_SANITIZERS     "Enable sanitizers"            OFF)
option(RPCXX_TEST_RPS            "Build QT5 based RPS test"     OFF)
option(RPCXX_WITH_TESTS          "Build tests"                  OFF)

include(cmake/RPCXXCodegen.cmake)
include(cmake/CPM.cmake)

CPMAddPackage("gh:p-ranav/argparse@3.0")
CPMAddPackage("gh:fmtlib/fmt#11.0.2")
CPMAddPackage("gh:cyanidle/describe@3.0")

add_library(rpcxx-warnings INTERFACE)
add_library(rpcxx-options INTERFACE)
add_library(rpcxx-headers INTERFACE)
target_include_directories(rpcxx-headers INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(rpcxx-warnings INTERFACE -Wall -Wextra)
endif()

add_library(rpcxx-future STATIC src/future.cpp)
target_link_libraries(rpcxx-future PRIVATE rpcxx-options rpcxx-warnings)
target_link_libraries(rpcxx-future PUBLIC rpcxx-headers)

file(GLOB JSON_VIEW_SOURCES CONFIGURE_DEPENDS src/json_view/*.cpp)
add_library(rpcxx-json STATIC ${JSON_VIEW_SOURCES})
target_link_libraries(rpcxx-json PRIVATE rpcxx-options rpcxx-warnings)
target_link_libraries(rpcxx-json PUBLIC rpcxx-headers describe)

file(GLOB RPCXX_SOURCES CONFIGURE_DEPENDS src/rpcxx/*.cpp)
add_library(rpcxx STATIC ${RPCXX_SOURCES})
add_library(rpcxx::rpcxx ALIAS rpcxx)

target_link_libraries(rpcxx PRIVATE rpcxx-options rpcxx-warnings)
target_link_libraries(rpcxx PUBLIC rpcxx-json rpcxx-headers rpcxx-future)

install(TARGETS rpcxx OPTIONAL)

# Do not clone submodules
cmake_policy(SET CMP0097 NEW)
CPMAddPackage(NAME rapidjson
    GITHUB_REPOSITORY Tencent/rapidjson
    VERSION 1.1.1
    GIT_TAG 7c73dd7
    GIT_SUBMODULES ""
    DOWNLOAD_ONLY YES
)

target_include_directories(rpcxx-json PRIVATE ${rapidjson_SOURCE_DIR}/include)

if(RPCXX_WITH_TESTS)
    CPMAddPackage("gh:doctest/doctest@2.4.11")
    CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        VERSION 1.8.3
        OPTIONS "BENCHMARK_ENABLE_TESTING OFF"
        GIT_SHALLOW YES
        EXCLUDE_FROM_ALL YES
    )
endif()

if(WIN32)
    target_link_libraries(rpcxx-json PRIVATE ws2_32)
    target_link_libraries(rpcxx PRIVATE ws2_32)
endif()

if(RPCXX_WITH_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()
if(RPCXX_WITH_CODEGEN)
  add_subdirectory(codegen)
endif()
