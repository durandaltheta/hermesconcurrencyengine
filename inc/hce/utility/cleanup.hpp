#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <stddef.h>

namespace hce {

/// low-level interface for implementing cleanup handlers
struct cleanup {
    struct data {
        void* install; // pointer passed to install()
        void* self; // `this` pointer of implementation
    };

    /// cleanup operation 
    using operation = void (*)(data&);

    cleanup();
    virtual ~cleanup();
    virtual void* cleanup_alloc(size_t) = 0; 
    virtual void cleanup_dealloc(void*) = 0; 

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
        void* install; /// data pointer provied to install()
    };

    node* list_;
};

}
