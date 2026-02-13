//
// Created by bucka on 2/10/2026.
//

#ifndef LYNC_ERROR_H
#define LYNC_ERROR_H

#include <stdbool.h>
#include "common.h"

typedef enum {
    MSG_ERROR,
    MSG_WARNING,
    MSG_NOTE
} MessageSeverity;

typedef struct {
    MessageSeverity severity;
    ErrorStage stage;
    SourceLocation loc;
    char* message;
} CompilerMessage;

struct ErrorCollector {
    CompilerMessage* messages;
    int count;
    int capacity;
    int error_count;
    int warning_count;
    int note_count;
    int max_errors;
    bool use_color;
};

ErrorCollector* init_error_collector();
void free_error_collector(ErrorCollector* ec);

void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void vadd_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, va_list args);
void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void add_note(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);

bool has_errors(ErrorCollector* ec);
bool has_warnings(ErrorCollector* ec);
void print_messages(ErrorCollector* ec);

#endif //LYNC_ERROR_H
