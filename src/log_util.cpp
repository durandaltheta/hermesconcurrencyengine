#include "loguru.hpp"

struct log_initializer {
    log_initializer() {
        // Put every log message in "everything.log":
        //loguru::add_file("hermesconcurrencyengine_all.log", loguru::Append, loguru::Verbosity_MAX);

        // Only log INFO, WARNING, ERROR and FATAL to "latest_readable.log":
        //loguru::add_file("hermesconcurrencyengine.log", loguru::Truncate, loguru::Verbosity_INFO);

        // Only show most relevant things on stderr:
        loguru::g_stderr_verbosity = 1;
    }
} g_log_initializer;
