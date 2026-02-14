//
// Created by bucka on 2/14/2026.
//

#ifndef LYNC_FILE_LOADER_H
#define LYNC_FILE_LOADER_H

#include "parser.h"

// Maximum include depth to prevent circular includes
#define MAX_INCLUDE_DEPTH 32

// Resolve a module name (e.g. "utils.arrays") to a file path relative to source_dir
// Returns malloc'd string like "C:/path/to/utils/arrays.lync"
char* resolve_module_path(const char* module_name, const char* source_dir);

// Load, lex, and parse a .lync file. Returns the parsed Program.
// source_dir is the directory of the file that contains the include statement.
// depth tracks recursion to prevent circular includes.
Program* load_and_parse_file(const char* file_path, int depth);

// Process all non-std imports in a program.
// Loads included files, filters functions by import type, and appends them to the program.
// source_file is the path of the main .lync file (used to resolve relative paths).
void process_file_includes(Program* prog, const char* source_file);

// Get the directory portion of a file path
char* get_directory(const char* file_path);

#endif //LYNC_FILE_LOADER_H
