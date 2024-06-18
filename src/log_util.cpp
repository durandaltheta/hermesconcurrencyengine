#include <string>
#include <string.h>

#include "loguru.hpp"
#include "utility.hpp"

struct log_initializer {
    log_initializer() {
        char* argv[1][4];
        std::string verbosity = std::string("-v") + std::to_string(HCELOGLEVEL);
        strncpy(argv[0],verbosity.c_str(),verbosity.length());
        argv[0][3]=0;
        loguru::init(1,argv);
    }
} g_log_initializer;
