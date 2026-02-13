cmake_minimum_required(VERSION 3.29)
project(Lync C)

set(CMAKE_C_STANDARD 23)

# Set output directory to build root instead of Debug/Release subdirectories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

add_executable(lync main.c
        lexer.c
        lexer.h
        parser.c
        parser.h
        analyzer.c
        analyzer.h
        codegen.c
        codegen.h
        error.c
        error.h
        common.h
        optimizer.c
        optimizer.h
        codegen_asm.c
)
