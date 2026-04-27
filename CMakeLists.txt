cmake_minimum_required(VERSION 3.29)
project(Lync C)

set(CMAKE_C_STANDARD 23)

# Set output directory to build root instead of Debug/Release subdirectories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

add_executable(lync src/main.c
        src/lexer.c
        src/lexer.h
        src/parser.c
        src/parser.h
        src/template.c
        src/analyzer.c
        src/analyzer.h
        src/codegen.c
        src/codegen.h
        src/error.c
        src/error.h
        src/common.h
        src/optimizer.c
        src/optimizer.h
        src/codegen_asm.c
        src/file_loader.c
        src/file_loader.h
        src/plugin.c
        src/plugin.h
        src/plugin_dl.c
        src/lync_plugin.h
)

# On POSIX we need libdl for dlopen; on Windows it's in kernel32 (linked by default).
if(UNIX AND NOT APPLE)
    target_link_libraries(lync PRIVATE dl)
endif()
