//
// Created by bucka on 2/10/2026.
//

#ifndef CMINUSMINUS_ERROR_H
#define CMINUSMINUS_ERROR_H

#include <stdbool.h>
#include "common.h"

// SourceLocation is already defined in common.h

typedef enum {
    MSG_ERROR,
    MSG_WARNING
} MessageSeverity;

typedef struct {
    MessageSeverity severity;
    ErrorStage stage;
    SourceLocation loc;
    char* message;  // Dynamically allocated
} CompilerMessage;

struct ErrorCollector {
    CompilerMessage* messages;
    int count;
    int capacity;
    int error_count;
    int warning_count;
};

// Error collector lifecycle
ErrorCollector* init_error_collector();
void free_error_collector(ErrorCollector* ec);

// Add messages
void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);
void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...);

// Query and output
bool has_errors(ErrorCollector* ec);
void print_messages(ErrorCollector* ec);

#endif //CMINUSMINUS_ERROR_H
