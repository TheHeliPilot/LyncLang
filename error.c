//
// Created by bucka on 2/10/2026.
//

#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// ANSI color codes
#define ANSI_RED     "\033[1;31m"
#define ANSI_YELLOW  "\033[1;33m"
#define ANSI_CYAN    "\033[1;36m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RESET   "\033[0m"

static bool detect_color_support(void) {
    if (!isatty(fileno(stderr))) return false;
#ifdef _WIN32
    // Enable ANSI escape processing on Windows 10+
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode)) {
            SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
    return true;
}

ErrorCollector* init_error_collector() {
    ErrorCollector* ec = malloc(sizeof(ErrorCollector));
    ec->capacity = 16;
    ec->count = 0;
    ec->error_count = 0;
    ec->warning_count = 0;
    ec->note_count = 0;
    ec->max_errors = 20;
    ec->use_color = detect_color_support();
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
    char buffer[2048];
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
    } else if (severity == MSG_WARNING) {
        ec->warning_count++;
    } else {
        ec->note_count++;
    }
}

void add_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    add_message(ec, MSG_ERROR, stage, loc, fmt, args);
    va_end(args);
}

void vadd_error(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, va_list args) {
    add_message(ec, MSG_ERROR, stage, loc, fmt, args);
}

void add_warning(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    add_message(ec, MSG_WARNING, stage, loc, fmt, args);
    va_end(args);
}

void add_note(ErrorCollector* ec, ErrorStage stage, SourceLocation loc, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    add_message(ec, MSG_NOTE, stage, loc, fmt, args);
    va_end(args);
}

bool has_errors(ErrorCollector* ec) {
    return ec != NULL && ec->error_count > 0;
}

bool has_warnings(ErrorCollector* ec) {
    return ec != NULL && ec->warning_count > 0;
}

void print_messages(ErrorCollector* ec) {
    if (ec == NULL || ec->count == 0) return;

    int errors_printed = 0;
    for (int i = 0; i < ec->count; i++) {
        CompilerMessage* msg = &ec->messages[i];

        // Apply error limit (still print all warnings and notes)
        if (msg->severity == MSG_ERROR) {
            if (ec->max_errors > 0 && errors_printed >= ec->max_errors) {
                continue;
            }
            errors_printed++;
        }

        const char* sev_label = msg->severity == MSG_ERROR ? "error" :
                                (msg->severity == MSG_WARNING ? "warning" : "note");
        const char* sev_color = "";
        const char* bold = "";
        const char* reset = "";
        if (ec->use_color) {
            reset = ANSI_RESET;
            bold = ANSI_BOLD;
            if (msg->severity == MSG_ERROR) sev_color = ANSI_RED;
            else if (msg->severity == MSG_WARNING) sev_color = ANSI_YELLOW;
            else sev_color = ANSI_CYAN;
        }

        if (msg->loc.line > 0) {
            // Format: [file:line:col] stage:severity: message
            fprintf(stderr, "%s[%s:%d:%d]%s %s:%s%s%s: %s\n",
                    bold,
                    msg->loc.filename ? msg->loc.filename : "unknown",
                    msg->loc.line,
                    msg->loc.column,
                    reset,
                    stage_name(msg->stage),
                    sev_color, sev_label, reset,
                    msg->message);
        } else {
            // No location — omit the prefix
            fprintf(stderr, "%s:%s%s%s: %s\n",
                    stage_name(msg->stage),
                    sev_color, sev_label, reset,
                    msg->message);
        }
    }

    // Show truncation message if errors were suppressed
    if (ec->max_errors > 0 && ec->error_count > ec->max_errors) {
        int remaining = ec->error_count - ec->max_errors;
        fprintf(stderr, "...and %d more error%s.\n",
                remaining, remaining == 1 ? "" : "s");
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
