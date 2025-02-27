#include <exception>

#include <dlfcn.h>

#include "module.hpp"
#include "environment.hpp"

using hce_close_handle_f = void (*)(void*);
using hce_install_environment_f = void (*)(void*);
using hce_module_create_f = void* (*)(void*);
using hce_module_destroy_f = void (*)(void*);

void hce_close_handle_(void* handle) {
    int ret = dlclose(handle);

    if(ret) {
        char* error = dlerror();
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlclose(<handle>\") failed: ", error);
        std::terminate();
    }
}

// install the parent environment into the module by using the dlsym()
// extracted version of this function
extern "C"
void hce_install_environment_(void* env) {
    ((hce::environment*)env)->install();
}

struct hce_install_environment_force_link_ {
    // ensure that hce_install_environment_ gets compiled and linked by 
    // implementors of hce::module
    constexpr static auto force_link_ = &hce_install_environment_;
};

// coroutine holding handle and module memory which awaits the module start() API
hce::co<int> hce_start_module_(
    void* context,
    std::unique_ptr<void, hce_close_handle_f> uhandle,
    std::unique_ptr<void, hce_module_destroy_f> umodule)
{
    // schedule modules' start() coroutine
    co_return co_await hce::schedule(((hce::module*)(umodule.get()))->start(context));
}

hce::awt<int> hce_module_import_(const std::filesystem::path& path, 
                                 void* context,
                                 hce::scheduler& sch) 
{
    char* error = 0;
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if(!handle) {
        error = dlerror();
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlopen(\"", path, "\") returned nullptr, error:", error);
        std::terminate();
    }

    dlerror(); // reset old errors
    auto install = (hce_install_environment_f)dlsym(handle, "hce_install_environment_");
    error = dlerror();

    if(error) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_install_environment_\") failed: ", error);
        hce_close_handle_(handle);
        std::terminate();
    } else if (!install) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_install_environment_\") returned nullptr");
        hce_close_handle_(handle);
        std::terminate();
    }

    auto create = (hce_module_create_f)dlsym(handle, "hce_module_create");
    error = dlerror();

    if(error) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_module_create\") failed: ", error);
        hce_close_handle_(handle);
        std::terminate();
    } else if (!create) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_module_create\") returned nullptr");
        hce_close_handle_(handle);
        std::terminate();
    }

    auto destroy = (hce_module_destroy_f)dlsym(handle, "hce_module_destroy");
    error = dlerror();

    if(error) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_module_destroy\") failed: ", error);
        hce_close_handle_(handle);
        std::terminate();
    } else if(!destroy) {
        HCE_FATAL_FUNCTION_BODY("hce_module_import_", "dlsym(<handle>, \"hce_module_destroy\") returned nullptr");
        hce_close_handle_(handle);
        std::terminate();
    }

    void* module = create(context);

    if (!module) {
        HCE_FATAL_FUNCTION_BODY("hce_start_module_", "hce_module_create() returned nullptr");
        hce_close_handle_(handle);
        std::terminate();
    }

    // acquire the parent environment
    auto env = hce::environment::clone();

    // configure the module's environment
    install(&env);

    // store handles in scoped pointers that will cleanup when the coroutine completes
    std::unique_ptr<void, hce_close_handle_f> uhandle(handle, hce_close_handle_);
    std::unique_ptr<void, hce_module_destroy_f> umodule(module, destroy);

    // launch the module
    return sch.schedule(hce_start_module_(context, std::move(uhandle), std::move(umodule)));
}

hce::module::module() {
    hce_install_environment_force_link_ fl;
    (void)fl;
}

hce::awt<int> hce::module::import(std::filesystem::path path, void* context, hce::scheduler& sch) {
    return hce_module_import_(path, context, sch);
}

hce::awt<int> hce::module::import(std::filesystem::path path, void* context) {
    return hce_module_import_(path, context, hce::scheduler::get());
}
