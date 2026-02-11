//
// Created by bucka on 2/9/2026.
//

#ifndef CMINUSMINUS_COMMON_H
#define CMINUSMINUS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

typedef enum {
    STAGE_LEXER,
    STAGE_PARSER,
    STAGE_ANALYZER,
    STAGE_CODEGEN,
    STAGE_INTERNAL
} ErrorStage;

static inline const char* stage_name(ErrorStage s) {
    switch (s) {
        case STAGE_LEXER: return "lexer";
        case STAGE_PARSER: return "parser";
        case STAGE_ANALYZER: return "analyzer";
        case STAGE_CODEGEN: return "codegen";
        default: return "internal";
    }
}

// Forward declarations for error system
typedef struct ErrorCollector ErrorCollector;
typedef struct {
    int line;
    int column;
    const char* filename;
} SourceLocation;

// Global state
extern ErrorCollector* g_error_collector;
extern bool g_trace_mode;

// Forward declarations from error.h
void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void print_messages(ErrorCollector* ec);
bool has_errors(ErrorCollector* ec);

// Helper to create a dummy location (temporary until we add AST locations)
#define NO_LOC ((SourceLocation){.line = 0, .column = 0, .filename = "unknown"})

// Semantic errors (collect, don't exit immediately)
// Supports both old 3-arg format and new 4-arg format
#define stage_error(stage, ...) \
    _stage_error_helper(g_error_collector, stage, ##__VA_ARGS__)

static inline void _stage_error_helper(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    add_error(ec, stage, loc, "%s", buffer);
}

// Overload for old-style calls without location (temporary)
#define stage_error_no_loc(stage, fmt, ...) do { \
    add_error(g_error_collector, stage, NO_LOC, fmt, ##__VA_ARGS__); \
} while(0)

// Fatal errors (syntax errors - exit immediately)
#define stage_fatal(stage, loc, fmt, ...) do { \
    add_error(g_error_collector, stage, loc, fmt, ##__VA_ARGS__); \
    print_messages(g_error_collector); \
    exit(1); \
} while(0)

// Warnings (never exit)
#define stage_warning(stage, loc, fmt, ...) \
    add_warning(g_error_collector, stage, loc, fmt, ##__VA_ARGS__)

// Trace logging (runtime controlled via -trace flag)
#define stage_trace(stage, fmt, ...) do { \
    if (g_trace_mode) { \
        printf("[%s:trace] ", stage_name(stage)); \
        printf(fmt, ##__VA_ARGS__); \
        printf("\n"); \
    } \
} while(0)

// Legacy stage_debug for backwards compatibility during transition
#define stage_debug(stage, fmt, ...) stage_trace(stage, fmt, ##__VA_ARGS__)

#endif //CMINUSMINUS_COMMON_H
