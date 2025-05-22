//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_MODULE
#define HERMES_COROUTINE_ENGINE_MODULE

#include <string>
#include <filesystem>

#include "coroutine.hpp"
#include "scheduler.hpp"

namespace hce {

/**
 @brief an interface to be implementated by shared library code which can be imported and run in the host hce environment
 */
struct module {
    module();
    virtual ~module(){}

    /**
     The interpretation of the context pointer and the meaning of result of the 
     coroutine is implementation specific.

     @param context a pointer to be interpretted and used by the implementation
     @return a coroutine which will start and run the module implementation, returning a code
     */
    virtual hce::co<int> start(void* context) = 0;

    /**
     @brief import a shared library as an hce module and start it

     The shared library will be opened, a module constructed and and its 
     `start()` implementation called and the resulting coroutine scheduled. The 
     module will be destroyed and the library closed before the awaitable 
     completes.

     In addition to the `start()` API, module code must provide the following 
     functions:
     ```
     // Return a pointer to the hce::module implementation. IE, the returned 
     // void* will be cast to an hce::module* and module->start() called.
     void* hce_module_create(); 

     // destroy the pointer returned from hce_module_create()
     void hce_module_destroy(void*);
     ```

     @param path to a .so or .dll 
     @param context a pointer to pass to hce::module::start()
     @param the scheduler to start the imported module on 
     @return an awaitable which blocks until the the result of start() completes
     */
    static hce::awt<int> import(std::filesystem::path path, void* context, hce::scheduler&);

    /// variant which supplies scheduler from hce::scheduler::get()
    static hce::awt<int> import(std::filesystem::path path, void* context);
};

}

#endif
