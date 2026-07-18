#include "lambo_log.h"

#include <cstring>

bool lambo_log_enabled = false;

void lambo_log_parse_flag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::strcmp(argv[i], "--lambo-debug") == 0) {
            lambo_log_enabled = true;
            return;
        }
    }
}