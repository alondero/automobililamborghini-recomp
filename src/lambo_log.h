// First-party debug-logging gate (--lambo-debug). C/C++ portable; routed from
// C TUs via plain C-linkage symbols (no namespace gymnastics).
//
// C-linkage chosen so C TUs (lambo_savestate.c, lambo_warp.c, libultra_stubs.c)
// can use the same gate as C++ without a wrapper. lambo_crash.cpp and the
// opt-in env-var features (LAMBO_PAK_TRACE, LAMBO_DL_INSPECT, ...) are NOT
// routed through this header.

#ifndef LAMBO_LOG_H
#define LAMBO_LOG_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool lambo_log_enabled;
void lambo_log_parse_flag(int argc, char** argv);

#ifdef __cplusplus
} // extern "C"
#endif

// `tag` MUST be a string literal -- concatenation happens here, not via printf
// format. Args are NOT evaluated when the gate is closed (they live inside an
// untaken if branch).
#define LAMBO_LOG(tag, ...) \
    do { if (lambo_log_enabled) fprintf(stderr, "[" tag "] " __VA_ARGS__); } while (0)

// Crash dumps and fatal early-init errors only -- never gate a line behind
// this for convenience.
#define LAMBO_LOG_ALWAYS(tag, ...) \
    fprintf(stderr, "[" tag "] " __VA_ARGS__)

#endif // LAMBO_LOG_H