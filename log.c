#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

bool logger_allow_verbose;

void log_debug(const char* message, ...);
void log_info(const char* message, ...);
void log_error(const char* message, ...);

void log_format(FILE *f, const char* tag, const char* message, va_list args) {
    time_t now;
    time(&now);
    char *date =ctime(&now);
    date[strlen(date) - 1] = '\0';
    fprintf(f, "%s [%s] ", date, tag);
    vfprintf(f, message, args);
    fprintf(f, "\n");
}

void log_error(const char* message, ...) {
    va_list args;
    va_start(args, message);
    log_format(stderr, "error", message, args);
    va_end(args);
}

void log_info(const char* message, ...) {
    va_list args;
    va_start(args, message);
    log_format(stdout, "info", message, args);
    va_end(args);
}

void log_debug(const char* message, ...) {
    if (!logger_allow_verbose) {
        return;
    }
    va_list args;
    va_start(args, message);
    log_format(stdout, "debug", message, args);
    va_end(args);
}