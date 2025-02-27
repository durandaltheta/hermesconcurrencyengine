#include <iostream>
#include <filesystem>
#include "hce.hpp"

#if defined(_WIN32)
#include <windows.h>
std::filesystem::path get_executable_path(){
    char path[MAX_PATH];
    if (GetModuleFileName(nullptr, path, MAX_PATH)) {
        return std::filesystem::canonical(path);
    } else {
        throw std::runtime_error("Failed to get executable path");
    }
}
#elif defined(__linux__)
#include <unistd.h>
std::filesystem::path get_executable_path() {
    return std::filesystem::canonical("/proc/self/exe");
}
#else
    #error Unsupported platform
#endif

int main() {
    std::cout << "initializing..." << std::endl;
    auto lf = hce::initialize();
    int i = 1234;
    const std::string so_lib_name = "libexample_002_shared.so";

    std::cout << "importing " << so_lib_name << std::endl;
    std::filesystem::path so_lib_path = get_executable_path().parent_path() / so_lib_name;

    // import the shared library as an hce module and then block on the awaitable
    int code = hce::module::import(so_lib_path.c_str(), (void*)&i);

    if(code == 0) {
        std::cout << so_lib_name << " ran successfully" << std::endl;
    } else {
        std::cout << so_lib_name << " failed with code: " << code << std::endl;
    }

    return 0;
}
