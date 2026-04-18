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
)

target_include_directories(pavicc PRIVATE src)
