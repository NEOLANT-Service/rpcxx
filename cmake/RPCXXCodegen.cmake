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


include_guard()
macro(_codegen_find_exec)
    if (NOT RPCXX_CODE_GENERATOR)
        if (TARGET rpcxx-codegen)
            set(_if_codegen_target rpcxx-codegen)
            set(RPCXX_CODE_GENERATOR "$<TARGET_FILE:rpcxx-codegen>")
        else()
            find_program(RPCXX_CODE_GENERATOR NAMES rpcxx-codegen REQUIRED)
        endif()
    endif()
endmacro()

function(_codegen_scan_includes spec out visited)
    macro(append_uniq list item)
        list(FIND ${list} ${item} _found)
        if (_found EQUAL -1)
            list(APPEND ${list} ${item})
        endif()
    endmacro()

    get_filename_component(base_dir ${spec} DIRECTORY)
    set(${out} "" PARENT_SCOPE)
    if (NOT EXISTS ${spec})
        return()
    endif()
    file(READ ${spec} body)
    string(REGEX MATCHALL "include\\w*\(\\w*[^)\\W]+\\w*\)" includes "${body}")
    foreach(inc ${includes})
        string(REGEX MATCH "\"(.*)\"" path ${inc})
        set(path ${base_dir}/${CMAKE_MATCH_1})
        list(FIND visited ${path} found)
        if (NOT found EQUAL -1)
            continue()
        endif()
        _codegen_scan_includes(${path} _nested "${visited}")
        list(APPEND visited ${path})
        foreach (nest ${_nested})
            append_uniq(visited ${nest})
        endforeach()
    endforeach()
    set(${out} "${visited}" PARENT_SCOPE)
endfunction()

function(rpcxx_codegen_go ARG_SPEC)
    _codegen_find_exec()
    set(options)
    set(oneValueArgs PREFIX DIR TARGET)
    set(multiValueArgs)
    cmake_parse_arguments(ARG
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN})

    if (NOT ARG_SPEC)
        message(FATAL_ERROR "SPEC (path to <spec>.json) argument required")
    endif()
    if (NOT ARG_DIR)
        message(FATAL_ERROR "DIR argument required")
    endif()
    if (NOT ARG_PREFIX)
        message(FATAL_ERROR "PREFIX argument required")
    endif()
    if (NOT ARG_TARGET)
        message(FATAL_ERROR "TARGET argument required")
    endif()

    _codegen_scan_includes(${ARG_SPEC} _scanned_deps "")
    message(STATUS "Codegen for ${ARG_TARGET}: depends: [${ARG_SPEC};${_scanned_deps}]")

    set(marker ${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.marker)
    add_custom_command(
        OUTPUT ${marker}
        COMMAND ${RPCXX_CODE_GENERATOR}
        ARGS ${ARG_SPEC}
            --output-dir ${ARG_DIR}
            --lang go
            --marker ${marker}
            -o pkg_prefix=${ARG_PREFIX}
            ${kwargs}
        DEPENDS ${ARG_SPEC} ${_if_codegen_target} ${_scanned_deps}
        COMMENT "Rpcxx Codegen (GO): ${ARG_SPEC} => ${ARG_DIR}"
    )
    add_custom_target(${ARG_TARGET} DEPENDS ${marker})
endfunction()

function(rpcxx_codegen ARG_SPEC)
    _codegen_find_exec()
    set(options NO_SERVER NO_CLIENT DESCRIBE_SERVER)
    set(oneValueArgs PREFIX TARGET)
    set(multiValueArgs)
    cmake_parse_arguments(ARG
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN})
    if (NOT ARG_SPEC)
        message(FATAL_ERROR "SPEC (path to <spec>.json) argument required")
    endif()
    if (NOT IS_ABSOLUTE ARG_SPEC)
        set(ARG_SPEC ${CMAKE_CURRENT_LIST_DIR}/${ARG_SPEC})
    endif()
    if (NOT EXISTS ${ARG_SPEC})
        message(FATAL_ERROR "Spec file ('${ARG_SPEC}') does not exist")
    endif()
    if (NOT ARG_TARGET)
        message(FATAL_ERROR "TARGET argument is required.")
    endif()
    if(ARG_DESCRIBE_SERVER)
        list(APPEND kwargs --describe-server)
    endif()
    if(ARG_NO_SERVER)
        list(APPEND kwargs --no-server)
    endif()
    if(ARG_NO_CLIENT)
        list(APPEND kwargs --no-client)
    endif()

    _codegen_scan_includes(${ARG_SPEC} _scanned_deps "")
    message(STATUS "Codegen for ${ARG_TARGET}: depends: [${ARG_SPEC};${_scanned_deps}]")

    get_filename_component(output_stem ${ARG_SPEC} NAME_WLE)
    set(prefix ${CMAKE_CURRENT_BINARY_DIR}/${ARG_PREFIX})

    if (NOT EXISTS ${prefix})
      file(MAKE_DIRECTORY ${prefix})
    endif()

    set(output_stem ${prefix}/${output_stem})
    set(output ${output_stem}.hpp)
    set_property(SOURCE ${output} PROPERTY GENERATED 1)

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${RPCXX_CODE_GENERATOR}
        ARGS ${ARG_SPEC} --output-dir ${prefix} ${kwargs}
        DEPENDS ${ARG_SPEC} ${_if_codegen_target} ${_scanned_deps}
        COMMENT "Rpcxx Codegen: ${ARG_SPEC} => ${output}"
    )
    add_custom_target(${ARG_TARGET}_gen DEPENDS ${output})

    add_library(${ARG_TARGET} INTERFACE)
    target_link_libraries(${ARG_TARGET} INTERFACE rpcxx::rpcxx)
    target_include_directories(${ARG_TARGET} INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_gen)
endfunction()
