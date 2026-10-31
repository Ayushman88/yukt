cmake_minimum_required(VERSION 3.20)
project(yukt VERSION 0.13.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

function(yukt_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

set(LLVM_HINTS
    /opt/homebrew/opt/llvm
    /usr/local/opt/llvm
)
find_package(LLVM REQUIRED CONFIG
    HINTS ${LLVM_HINTS}
    PATH_SUFFIXES lib/cmake/llvm
)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

if(DEFINED LLVM_TOOLS_BINARY_DIR)
    find_program(YUKT_HOST_CLANG NAMES clang clang-18 clang-19 clang-20 clang-21 clang-22 clang-23 clang-24
        HINTS ${LLVM_TOOLS_BINARY_DIR}
        NO_DEFAULT_PATH)
endif()
if(NOT YUKT_HOST_CLANG)
    find_program(YUKT_HOST_CLANG NAMES clang)
endif()
if(NOT YUKT_HOST_CLANG)
    set(YUKT_HOST_CLANG "")
endif()
message(STATUS "YUKT host clang for linking: ${YUKT_HOST_CLANG}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(YUKT_LLVM_LIBS
    Core
    Support
    native
    nativecodegen
    MC
    MCParser
    BitWriter
    Target
    TargetParser
    AsmPrinter
    AsmParser
    Passes
    Analysis
    TransformUtils
    InstCombine
    ScalarOpts
    Vectorize
    IPO
)

add_library(yukt_compiler
    compiler/diagnostics/diagnostics.cpp
    compiler/lexer/lexer.cpp
    compiler/ast/ast.cpp
    compiler/parser/parser.cpp
    compiler/semantic/type.cpp
    compiler/semantic/ownership.cpp
    compiler/semantic/analyzer.cpp
    compiler/ir/ir.cpp
    compiler/ir/lower.cpp
    compiler/driver/driver.cpp
    compiler/driver/module.cpp
)
target_include_directories(yukt_compiler PUBLIC ${CMAKE_SOURCE_DIR}/compiler)
target_compile_definitions(yukt_compiler PUBLIC YUKT_STDLIB_DIR="${CMAKE_SOURCE_DIR}/std")
yukt_set_warnings(yukt_compiler)

add_library(yukt_runtime STATIC runtime/yukt_runtime.c)
target_include_directories(yukt_runtime PUBLIC ${CMAKE_SOURCE_DIR}/runtime)
target_link_libraries(yukt_runtime PUBLIC m)

add_library(yukt_backend
    compiler/backend/llvm_backend.cpp
    compiler/backend/explain.cpp
    compiler/backend/pipeline.cpp
)
target_include_directories(yukt_backend PUBLIC ${CMAKE_SOURCE_DIR}/compiler)
target_include_directories(yukt_backend SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(yukt_backend PRIVATE
    ${LLVM_DEFINITIONS_LIST}
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
    YUKT_LLVM_CLANG="${YUKT_HOST_CLANG}"
)
target_link_libraries(yukt_backend PUBLIC yukt_compiler PRIVATE ${YUKT_LLVM_LIBS})
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_backend PRIVATE -fno-rtti)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(yukt_backend PRIVATE -Wno-unused-parameter)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_backend PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()

add_executable(yukt compiler/main.cpp)
target_link_libraries(yukt PRIVATE yukt_backend yukt_runtime)
target_compile_definitions(yukt PRIVATE YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>")
yukt_set_warnings(yukt)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()

enable_testing()

function(yukt_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE yukt_compiler)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    yukt_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

yukt_add_test(yukt_lexer_tests tests/lexer/lexer_tests.cpp)
yukt_add_test(yukt_parser_tests tests/parser/parser_tests.cpp)
yukt_add_test(yukt_program_tests tests/programs/program_tests.cpp)
yukt_add_test(yukt_semantic_tests tests/semantic/semantic_tests.cpp)
yukt_add_test(yukt_module_tests tests/modules/module_tests.cpp)

add_executable(yukt_ir_tests tests/ir/ir_tests.cpp)
target_link_libraries(yukt_ir_tests PRIVATE yukt_compiler)
target_include_directories(yukt_ir_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_ir_tests PRIVATE YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
yukt_set_warnings(yukt_ir_tests)
add_test(NAME yukt_ir_tests COMMAND yukt_ir_tests)

add_executable(yukt_native_tests tests/backend/native_tests.cpp)
target_link_libraries(yukt_native_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_native_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_native_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_native_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_native_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_native_tests COMMAND yukt_native_tests)
add_dependencies(yukt_native_tests yukt)

add_executable(yukt_record_tests tests/records/record_tests.cpp)
target_link_libraries(yukt_record_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_record_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_record_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_record_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_record_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_record_tests COMMAND yukt_record_tests)
add_dependencies(yukt_record_tests yukt)

add_executable(yukt_stdlib_tests tests/stdlib/stdlib_tests.cpp)
target_link_libraries(yukt_stdlib_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_stdlib_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_stdlib_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_stdlib_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_stdlib_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_stdlib_tests COMMAND yukt_stdlib_tests)
add_dependencies(yukt_stdlib_tests yukt)

yukt_add_test(yukt_ownership_tests tests/ownership/ownership_tests.cpp)

add_executable(yukt_ownership_runtime_tests tests/ownership/runtime_release_tests.c)
target_link_libraries(yukt_ownership_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_ownership_runtime_tests COMMAND yukt_ownership_runtime_tests)
add_test(NAME yukt_ownership_double_release COMMAND yukt_ownership_runtime_tests double-release)
set_tests_properties(yukt_ownership_double_release PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: double release or untracked pointer")

add_test(NAME hello_tokens COMMAND yukt --tokens ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_ast COMMAND yukt --ast ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_types COMMAND yukt --types ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_check COMMAND yukt ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_ir COMMAND yukt --emit-ir ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_explain COMMAND yukt --explain ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME index_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/examples/index.yukt)
add_test(NAME module_ok_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/modules/ok/app.yukt)
add_test(NAME module_nested_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/modules/nested/a.yukt)
add_test(NAME stdlib_str_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/stdlib/str_basic.yukt)
add_test(NAME stdlib_all_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/stdlib/all_modules.yukt)
add_test(NAME records_basic_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/records/basic.yukt)
add_test(NAME records_import_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/records/import/app.yukt)

add_executable(yukt_ref_tests tests/refs/ref_tests.cpp)
target_link_libraries(yukt_ref_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_ref_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_ref_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_ref_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_ref_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_ref_tests COMMAND yukt_ref_tests)
add_dependencies(yukt_ref_tests yukt)
add_test(NAME refs_suite_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/refs/suite.yukt)
