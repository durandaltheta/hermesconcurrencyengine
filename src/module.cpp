#include <exception>

#include <dlfcn.h>

#include "logging.hpp"
#include "module.hpp"

using hce_module_create_f = void* (*)();
using hce_module_destroy_f = void (*)(void*);

struct hce::module::lifecycle : public hce::printable {
    lifecycle() = delete;

    virtual ~lifecycle() {
        HCE_HIGH_DESTRUCTOR();
        module_destroy_(module_handle_);
        hce::module::lifecycle::close_dynamic_library_handle(dynamic_library_handle_);
    }

    static inline std::string info_name() { return "hce::module::lifecycle"; }
    inline std::string name() const { return lifecycle::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << (void*)module_destroy_
           << ", " << module_handle_
           << ", " << dynamic_library_handle_;
        return ss.str();
    }

    static void close_dynamic_library_handle(void* handle) {
        int ret = dlclose(handle);

        if(ret) {
            char* error = dlerror();
            HCE_FATAL_FUNCTION_BODY("hce::module::lifecycle::close_dynamic_library_handle", "dlclose(",handle,") failed: ", error);
            std::terminate();
        }
    }

    static inline hce::co<int> start(hce_module_destroy_f mod_des, 
                                     void* mod_hdl, 
                                     void* dl_hdl,
                                     void* ctx) 
    {
        std::unique_ptr<lifecycle> lf(new lifecycle(mod_des, mod_hdl, dl_hdl));
        hce::awt<int> awt = hce::schedule(((hce::module*)lf->module_handle_)->start(ctx));
        HCE_INFO_FUNCTION_BODY("hce::module::lifecycle::start","hce::awt<int>::valid():", awt.valid() ? "true" : "false");
        co_return co_await std::move(awt);
    }

private:
    lifecycle(hce_module_destroy_f mod_des, 
              void* mod_hdl, 
              void* dl_hdl) :
        module_destroy_(mod_des),
        module_handle_(mod_hdl),
        dynamic_library_handle_(dl_hdl)
    { 
        HCE_HIGH_CONSTRUCTOR(mod_des, mod_hdl, dl_hdl);
    }

    const hce_module_destroy_f module_destroy_;
    void* module_handle_;
    void* dynamic_library_handle_;
    hce::awt<int> awaitable_;
};

hce::awt<int> hce::module::import(std::filesystem::path path, 
                                  void* context, 
                                  hce::scheduler& sch) 
{
    char* error = 0;
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if(!handle) {
        error = dlerror();
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "dlopen(\"", path, "\") returned nullptr handle, error:", error);
        std::terminate();
    }

    dlerror(); // reset old errors

    auto create = (hce_module_create_f)dlsym(handle, "hce_module_create");
    error = dlerror();

    if(error) {
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "dlsym(",handle,", \"hce_module_create\") failed: ", error);
        hce::module::lifecycle::close_dynamic_library_handle(handle);
        std::terminate();
    } else if (!create) {
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "dlsym(",handle,", \"hce_module_create\") returned nullptr");
        hce::module::lifecycle::close_dynamic_library_handle(handle);
        std::terminate();
    }

    auto module_destroy = (hce_module_destroy_f)dlsym(handle, "hce_module_destroy");
    error = dlerror();

    if(error) {
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "dlsym(",handle,", \"hce_module_destroy\") failed: ", error);
        hce::module::lifecycle::close_dynamic_library_handle(handle);
        std::terminate();
    } else if(!module_destroy) {
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "dlsym(",handle,", \"hce_module_destroy\") returned nullptr");
        hce::module::lifecycle::close_dynamic_library_handle(handle);
        std::terminate();
    }

    void* module = create();

    if (!module) {
        HCE_FATAL_FUNCTION_BODY("hce::module::import", "hce_module_create() returned nullptr");
        hce::module::lifecycle::close_dynamic_library_handle(handle);
        std::terminate();
    }

    // launch the module
    return sch.schedule(
        hce::module::lifecycle::start(
            module_destroy, module, handle, context));
}

hce::awt<int> hce::module::import(std::filesystem::path path, void* context) {
    return hce::module::import(path, context, hce::scheduler::get());
}
