//
// Created by bucka on 2/10/2026.
//

#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

ErrorCollector* init_error_collector() {
    ErrorCollector* ec = malloc(sizeof(ErrorCollector));
    ec->capacity = 16;
    ec->count = 0;
    ec->error_count = 0;
    ec->warning_count = 0;
    ec->messages = malloc(sizeof(CompilerMessage) * ec->capacity);
    return ec;
}

void free_error_collector(ErrorCollector* ec) {
    if (ec == NULL) return;

    for (int i = 0; i < ec->count; i++) {
        free(ec->messages[i].message);
    }
    free(ec->messages);
    free(ec);
}

static void add_message(ErrorCollector* ec, MessageSeverity severity, ErrorStage stage,
                       SourceLocation loc, const char* fmt, va_list args) {
    if (ec == NULL) return;

    // Resize if needed
    if (ec->count >= ec->capacity) {
        ec->capacity *= 2;
        ec->messages = realloc(ec->messages, sizeof(CompilerMessage) * ec->capacity);
    }

    // Format message
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    // Create message
    CompilerMessage* msg = &ec->messages[ec->count++];
    msg->severity = severity;
    msg->stage = stage;
    msg->loc = loc;
    msg->message = strdup(buffer);

    // Update counters
    if (severity == MSG_ERROR) {
        ec->error_count++;
    } else {
        ec->warning_count++;
    }
}

void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    add_message(ec, MSG_ERROR, stage, loc, fmt, args);
    va_end(args);
}

void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    add_message(ec, MSG_WARNING, stage, loc, fmt, args);
    va_end(args);
}

bool has_errors(ErrorCollector* ec) {
    return ec != NULL && ec->error_count > 0;
}

void print_messages(ErrorCollector* ec) {
    if (ec == NULL || ec->count == 0) return;

    for (int i = 0; i < ec->count; i++) {
        CompilerMessage* msg = &ec->messages[i];

        // Format: [file:line:col] stage:severity: message
        fprintf(stderr, "[%s:%d:%d] %s:%s: %s\n",
                msg->loc.filename ? msg->loc.filename : "unknown",
                msg->loc.line,
                msg->loc.column,
                stage_name(msg->stage),
                msg->severity == MSG_ERROR ? "error" : "warning",
                msg->message);
    }

    // Summary
    if (ec->error_count > 0 || ec->warning_count > 0) {
        fprintf(stderr, "\n");
        if (ec->error_count > 0) {
            fprintf(stderr, "%d error%s generated.\n",
                    ec->error_count, ec->error_count == 1 ? "" : "s");
        }
        if (ec->warning_count > 0) {
            fprintf(stderr, "%d warning%s generated.\n",
                    ec->warning_count, ec->warning_count == 1 ? "" : "s");
        }
    }
}
