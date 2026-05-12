cmake_minimum_required(VERSION 3.16)
project(PavicCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(pavicc
    src/main.cpp
    src/diagnostic.cpp
    src/source.cpp
    src/token.cpp
    src/lexer.cpp
    src/cst.cpp
    src/parser.cpp
    src/ast.cpp
    src/ast_lower.cpp
    src/optimizer.cpp
    src/symbol_table.cpp
    src/semantic_scope.cpp
    src/semantic_type.cpp
    src/semantic_usage.cpp
    src/codegen/memory_layout.cpp
    src/codegen/code_buffer.cpp
    src/codegen/codegen.cpp
    src/multi_backend.cpp
)

target_include_directories(pavicc PRIVATE src)

enable_testing()
add_test(
    NAME regression_add34_codegen
    COMMAND sh -c "./pavicc -q --emulator -o regression_add34.bin \"${CMAKE_SOURCE_DIR}/tests/regression/add34.markos\" && cmp regression_add34.bin \"${CMAKE_SOURCE_DIR}/tests/regression/add34.expected.bin\""
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
