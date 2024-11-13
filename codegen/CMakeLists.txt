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


add_executable(rpcxx-codegen 
    src/codegen.cpp
    src/cppgen.cpp
    src/gogen.cpp
    src/populate.cpp
)
target_link_libraries(rpcxx-codegen PRIVATE 
    rpcxx-options rpcxx-warnings fmt argparse rpcxx-json
)
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(rpcxx-codegen PRIVATE stdc++fs)
endif()
target_include_directories(rpcxx-codegen PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
install(TARGETS rpcxx-codegen RUNTIME DESTINATION bin EXCLUDE_FROM_ALL)
install(FILES ../cmake/RPCXXCodegen.cmake DESTINATION ${CMAKE_ROOT}/Modules/ EXCLUDE_FROM_ALL)

CPMAddPackage(NAME lua
  URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
  URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30 
  VERSION 5.4.7
  DOWNLOAD_ONLY YES
)

set(LUA_SRC_FILES
    lapi.c lcode.c lctype.c ldebug.c ldo.c
    ldump.c lfunc.c lgc.c llex.c lmem.c
    lobject.c lopcodes.c lparser.c lstate.c
    lstring.c ltable.c ltm.c lundump.c
    lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c
    ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c
    ltablib.c lutf8lib.c linit.c
)

list(TRANSFORM LUA_SRC_FILES PREPEND ${lua_SOURCE_DIR}/src/)
add_library(codegen-lua STATIC ${LUA_SRC_FILES})
target_include_directories(codegen-lua PUBLIC $<BUILD_INTERFACE:${lua_SOURCE_DIR}/src/>)
target_link_libraries(rpcxx-codegen PRIVATE codegen-lua)
if (UNIX AND NOT MSYS)
    target_compile_definitions(codegen-lua PRIVATE LUA_USE_POSIX)
endif()
