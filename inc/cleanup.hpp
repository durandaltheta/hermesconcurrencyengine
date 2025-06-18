//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_CLEANUP
#define HERMES_COROUTINE_ENGINE_CLEANUP 

#include "memory.hpp"

namespace hce {

/**
 @brief low-level interface for implementing cleanup handlers

 Sometimes an operation that would ideally be handled by a virtual destructor is 
 not known at object creation time. This mechanism is a low-level, simple 
 mechanism for dynamically appending destructor-like logic to the lifecycle of 
 an implementation.

 The the most distant descendent implementator of cleanup must call `clean()` 
 before it goes out of scope (typically within the descendent destructor, 
 ensuring memory is valid).
 */
struct cleanup {
    struct data {
        void* install; // pointer passed to install()
        void* self; // `this` pointer of implementation
    };

    /// cleanup operation 
    using operation = void (*)(data&);

    cleanup();
    virtual ~cleanup();

    /**
     @brief install a cleanup operation 

     Cleanup handlers are installed as a list. Installed handlers are 
     executed FILO (first in, last out).

     @param op a cleanup operation function pointer 
     @param arg some arbitrary data to be passed to `co` in the `cleanup_data` struct
     */
    void install(operation op, void* arg);

    /**
     @brief execute any installed callback operations

     Cleanup often needs to happen at a topmost destructor while all members are 
     valid, so it must be explicitly called.
     */
    void clean();

private:
    struct node {
        node(node* n, operation o, void* i);

        node* next; /// the next node in the cleanup handler list
        operation op; /// the cleanup operation provided to install()
        void* install; /// data pointer provided to install()
    };

    // Provide the first entry as an optimization, assuming if an object 
    // implements cleanup, it is very likely to need at least one node. In this 
    // default case, avoids an unnecessary allocation/deallocation.
    node head_; 
    node* list_;
};

}

#endif
