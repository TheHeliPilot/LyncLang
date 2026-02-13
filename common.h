//
// Created by bucka on 2/9/2026.
//

#ifndef LYNC_COMMON_H
#define LYNC_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

//for compatibility with MSVC and older C standards
#ifndef nullptr
#define nullptr NULL
#endif

typedef enum {
    STAGE_LEXER,
    STAGE_PARSER,
    STAGE_ANALYZER,
    STAGE_OPTIMIZER,
    STAGE_CODEGEN,
    STAGE_INTERNAL
} ErrorStage;

static inline const char* stage_name(ErrorStage s) {
    switch (s) {
        case STAGE_LEXER: return "lexer";
        case STAGE_PARSER: return "parser";
        case STAGE_ANALYZER: return "analyzer";
        case STAGE_OPTIMIZER: return "optimizer";
        case STAGE_CODEGEN: return "codegen";
        default: return "internal";
    }
}

typedef struct ErrorCollector ErrorCollector;
typedef struct {
    int line;
    int column;
    const char* filename;
} SourceLocation;

extern ErrorCollector* g_error_collector;
extern bool g_trace_mode;
extern int g_trace_depth;

void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void vadd_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, va_list args);
void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void add_note(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void print_messages(ErrorCollector* ec);
bool has_errors(ErrorCollector* ec);
bool has_warnings(ErrorCollector* ec);

#define NO_LOC ((SourceLocation){.line = 0, .column = 0, .filename = "unknown"})

#define stage_error(stage, ...) \
    _stage_error_helper(g_error_collector, stage, ##__VA_ARGS__)

static inline void _stage_error_helper(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vadd_error(ec, stage, loc, fmt, args);
    va_end(args);
}

#define stage_fatal(stage, loc, fmt, ...) do { \
    add_error(g_error_collector, stage, loc, fmt, ##__VA_ARGS__); \
    print_messages(g_error_collector); \
    exit(1); \
} while(0)

#define stage_warning(stage, loc, fmt, ...) \
    add_warning(g_error_collector, stage, loc, fmt, ##__VA_ARGS__)

#define stage_note(stage, loc, fmt, ...) \
    add_note(g_error_collector, stage, loc, fmt, ##__VA_ARGS__)

#define stage_trace(stage, fmt, ...) do { \
    if (g_trace_mode) { \
        for (int _i = 0; _i < g_trace_depth; _i++) fprintf(stderr, "  "); \
        fprintf(stderr, "[%s:trace] ", stage_name(stage)); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while(0)

#define stage_trace_enter(stage, fmt, ...) do { \
    stage_trace(stage, fmt, ##__VA_ARGS__); \
    g_trace_depth++; \
} while(0)

#define stage_trace_exit(stage, fmt, ...) do { \
    if (g_trace_depth > 0) g_trace_depth--; \
    stage_trace(stage, fmt, ##__VA_ARGS__); \
} while(0)

#endif //LYNC_COMMON_H
