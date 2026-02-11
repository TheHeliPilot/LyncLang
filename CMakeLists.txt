cmake_minimum_required(VERSION 3.29)
project(Lync C)

set(CMAKE_C_STANDARD 23)

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
)
