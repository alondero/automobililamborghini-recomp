// First-party logging and Windows console policy. C-linkage keeps this API
// usable from the small C translation units as well as C++.

#ifndef LAMBO_LOG_H
#define LAMBO_LOG_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LamboLogLevel {
    LAMBO_LEVEL_ERROR = 0,
    LAMBO_LEVEL_WARN  = 1,
    LAMBO_LEVEL_INFO  = 2,
    LAMBO_LEVEL_DEBUG = 3,
    LAMBO_LEVEL_TRACE = 4
} LamboLogLevel;

// Source-compatible fast path for existing hot-loop probes. It is true when
// the selected threshold includes debug messages.
extern bool lambo_log_enabled;
bool lambo_log_parse_args(int argc, char** argv);
void lambo_log_parse_flag(int argc, char** argv); // compatibility entry point
const char* lambo_log_last_error(void);
bool lambo_log_initialize(void);
void lambo_log_shutdown(void);
const char* lambo_log_path(void);
bool lambo_log_console_requested(void);
LamboLogLevel lambo_log_level(void);
bool lambo_log_would_emit(LamboLogLevel level);
void lambo_log_write(LamboLogLevel level, const char* tag, const char* format, ...);

#ifdef __cplusplus
} // extern "C"
#endif

// `tag` MUST be a string literal. Arguments remain inside the untaken branch,
// so expensive formatting expressions are not evaluated at a disabled level.
#define LAMBO_LOG_AT(level, tag, ...) \
    do { if (lambo_log_would_emit(level)) lambo_log_write(level, tag, __VA_ARGS__); } while (0)

// Existing diagnostics are debug-level by design.
#define LAMBO_LOG(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LAMBO_LOG_ERROR(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_ERROR, tag, __VA_ARGS__)
#define LAMBO_LOG_WARN(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_WARN, tag, __VA_ARGS__)
#define LAMBO_LOG_INFO(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_INFO, tag, __VA_ARGS__)
#define LAMBO_LOG_DEBUG(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LAMBO_LOG_TRACE(tag, ...) LAMBO_LOG_AT(LAMBO_LEVEL_TRACE, tag, __VA_ARGS__)

// Crash dumps and fatal early-init errors only -- never gate a line behind
// this for convenience.
#define LAMBO_LOG_ALWAYS(tag, ...) \
    fprintf(stderr, "[" tag "] " __VA_ARGS__)

#endif // LAMBO_LOG_H
