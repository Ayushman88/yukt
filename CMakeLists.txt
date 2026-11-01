cmake_minimum_required(VERSION 3.20)
project(yukt VERSION 0.14.1 LANGUAGES C CXX)

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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_library(yukt_runtime STATIC runtime/yukt_runtime.c runtime/yukt_013.c runtime/yukt_sync.c runtime/yukt_net.c
            runtime/yukt_http.c)
target_include_directories(yukt_runtime PUBLIC ${CMAKE_SOURCE_DIR}/runtime)
target_link_libraries(yukt_runtime PUBLIC m Threads::Threads)

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

add_executable(yukt_io_tests tests/io/io_tests.cpp)
target_link_libraries(yukt_io_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_io_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_io_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_io_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_io_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_io_tests)
add_test(NAME yukt_io_tests COMMAND yukt_io_tests)
add_dependencies(yukt_io_tests yukt)

add_executable(yukt_sync_tests tests/sync/sync_tests.cpp)
target_link_libraries(yukt_sync_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_sync_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_sync_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_sync_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_sync_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_sync_tests)
add_test(NAME yukt_sync_tests COMMAND yukt_sync_tests)
add_dependencies(yukt_sync_tests yukt)

add_executable(yukt_net_tests tests/net/net_tests.cpp)
target_link_libraries(yukt_net_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_net_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_net_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_net_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_net_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_net_tests)
add_test(NAME yukt_net_tests COMMAND yukt_net_tests)
add_dependencies(yukt_net_tests yukt)

add_executable(yukt_http_tests tests/http/http_tests.cpp)
target_link_libraries(yukt_http_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_http_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_http_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_http_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_http_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_http_tests)
add_test(NAME yukt_http_tests COMMAND yukt_http_tests)
add_dependencies(yukt_http_tests yukt)

add_executable(yukt_http_malformed tests/http/runtime_malformed.c)
target_link_libraries(yukt_http_malformed PRIVATE yukt_runtime)
add_test(NAME yukt_http_malformed COMMAND yukt_http_malformed)

add_executable(yukt_net_runtime_tests tests/net/runtime_net_tests.c)
target_link_libraries(yukt_net_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_net_runtime_tests COMMAND yukt_net_runtime_tests)
add_test(NAME yukt_net_bad_max COMMAND yukt_net_runtime_tests bad-max)
set_tests_properties(yukt_net_bad_max PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: invalid read size")

yukt_add_test(yukt_ownership_tests tests/ownership/ownership_tests.cpp)

add_executable(yukt_ownership_runtime_tests tests/ownership/runtime_release_tests.c)
target_link_libraries(yukt_ownership_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_ownership_runtime_tests COMMAND yukt_ownership_runtime_tests)
add_test(NAME yukt_ownership_double_release COMMAND yukt_ownership_runtime_tests double-release)
set_tests_properties(yukt_ownership_double_release PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: double release or untracked pointer")

add_executable(yukt_grow_oom_keeps_list tests/runtime/grow_oom_keeps_list.c)
target_link_libraries(yukt_grow_oom_keeps_list PRIVATE yukt_runtime)
add_test(NAME yukt_grow_oom_keeps_list COMMAND yukt_grow_oom_keeps_list)

add_executable(yukt_generation_guard_tests tests/runtime/generation_guard_tests.c)
target_link_libraries(yukt_generation_guard_tests PRIVATE yukt_runtime)
add_test(NAME yukt_generation_guard_tests COMMAND yukt_generation_guard_tests)
add_test(NAME yukt_generation_guard_trap COMMAND yukt_generation_guard_tests generation-guard)
set_tests_properties(yukt_generation_guard_trap PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: list changed during iteration")

add_test(NAME hello_tokens COMMAND yukt --tokens ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_ast COMMAND yukt --ast ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_types COMMAND yukt --types ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_check COMMAND yukt ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_ir COMMAND yukt --emit-ir ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_explain COMMAND yukt --explain ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME hello_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/examples/hello.yukt)
add_test(NAME tasks_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/examples/tasks.yukt)
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

add_executable(yukt_foreign_tests tests/foreign/foreign_tests.cpp)
target_link_libraries(yukt_foreign_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_foreign_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_foreign_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_foreign_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_foreign_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_foreign_tests COMMAND yukt_foreign_tests)
add_dependencies(yukt_foreign_tests yukt)
add_test(NAME foreign_strlen_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/foreign/strlen.yukt)

add_executable(yukt_error_tests tests/errors/error_tests.cpp)
target_link_libraries(yukt_error_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_error_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_error_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_error_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_error_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
add_test(NAME yukt_error_tests COMMAND yukt_error_tests)
add_dependencies(yukt_error_tests yukt)
add_test(NAME errors_ok_fail_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/errors/ok_fail.yukt)
add_test(NAME stdlib_map_set_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/stdlib/map_set.yukt)
add_test(NAME stdlib_file_json_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/stdlib/file_json.yukt)
add_test(NAME suite013_run COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/suite013/all.yukt)

add_executable(yukt_task_tests tests/tasks/task_tests.cpp)
target_link_libraries(yukt_task_tests PRIVATE yukt_backend yukt_runtime)
target_include_directories(yukt_task_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(yukt_task_tests PRIVATE
    YUKT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    YUKT_EXECUTABLE="$<TARGET_FILE:yukt>"
    YUKT_RUNTIME_LIB="$<TARGET_FILE:yukt_runtime>"
)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(yukt_task_tests PRIVATE -fno-rtti)
endif()
if(APPLE AND DEFINED LLVM_LIBRARY_DIR)
    target_link_options(yukt_task_tests PRIVATE "-Wl,-rpath,${LLVM_LIBRARY_DIR}")
endif()
yukt_set_warnings(yukt_task_tests)
add_test(NAME yukt_task_tests COMMAND yukt_task_tests)
add_dependencies(yukt_task_tests yukt)

add_executable(yukt_task_runtime_tests tests/tasks/runtime_task_tests.c)
target_link_libraries(yukt_task_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_task_runtime_tests COMMAND yukt_task_runtime_tests)
add_test(NAME yukt_task_double_wait COMMAND yukt_task_runtime_tests double-wait)
set_tests_properties(yukt_task_double_wait PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: double wait or untracked task")
add_test(NAME yukt_task_worker_trap COMMAND yukt run ${CMAKE_SOURCE_DIR}/tests/tasks/worker_trap.yukt)
set_tests_properties(yukt_task_worker_trap PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error:")

add_executable(yukt_dio_runtime_tests tests/runtime/dio_runtime_tests.c)
target_link_libraries(yukt_dio_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_dio_runtime_tests COMMAND yukt_dio_runtime_tests)
add_test(NAME yukt_dio_worker_stdin COMMAND yukt_dio_runtime_tests worker-stdin)
set_tests_properties(yukt_dio_worker_stdin PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: stdin from worker")

add_executable(yukt_sync_runtime_tests tests/sync/runtime_sync_tests.c)
target_link_libraries(yukt_sync_runtime_tests PRIVATE yukt_runtime)
add_test(NAME yukt_sync_runtime_tests COMMAND yukt_sync_runtime_tests)
add_test(NAME yukt_sync_double_receiver COMMAND yukt_sync_runtime_tests double-receiver)
set_tests_properties(yukt_sync_double_receiver PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: receiver already taken")
add_test(NAME yukt_sync_send_closed COMMAND yukt_sync_runtime_tests send-closed)
set_tests_properties(yukt_sync_send_closed PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: send on closed channel")
add_test(NAME yukt_sync_send_recv_closed COMMAND yukt_sync_runtime_tests send-recv-closed)
set_tests_properties(yukt_sync_send_recv_closed PROPERTIES
    PASS_REGULAR_EXPRESSION "YUKT runtime error: send on closed channel")
