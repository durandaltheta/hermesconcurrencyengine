//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_CLEANUP
#define HERMES_COROUTINE_ENGINE_CLEANUP 

namespace hce {

/// low-level interface for implementing cleanup handlers
struct cleanup {
    struct data {
        void* install; // pointer passed to install()
        void* self; // `this` pointer of implementation
    };

    /// cleanup operation 
    using operation = void (*)(data&);

    cleanup() : list_(nullptr) { }
    virtual ~cleanup(){}
    virtual void* cleanup_alloc(size_t) = 0; 
    virtual void cleanup_dealloc(void*) = 0; 

    /**
     @brief install a cleanup operation 

     Cleanup handlers are installed as a list. Installed handlers are 
     executed FILO (first in, last out).

     @param op a cleanup operation function pointer 
     @param arg some arbitrary data to be passed to `co` in the `cleanup_data` struct
     */
    inline void install(operation op, void* arg) {
        node* next = (node*)(this->cleanup_alloc(sizeof(node)));
        new(next) node(list_, op, arg);
        list_ = next;
    }

    /**
     @brief execute any installed callback operations

     Cleanup often needs to happen at a topmost destructor while all members are 
     valid, so it must be explicitly called.
     */
    inline void clean() {
        // trigger the callback if it is set, then unset it
        if(list_) [[likely]] {
            do {
                data d{list_->install, this};
                list_->op(d);
                node* old = list_;
                list_ = list_->next;
                this->cleanup_dealloc(old);
            } while(list_);
        }
    }

private:
    struct node {
        node(node* n, operation o, void* i) :
            next(n),
            op(o),
            install(i)
        { }

        node* next; /// the next node in the cleanup handler list
        operation op; /// the cleanup operation provided to install()
        void* install; /// data pointer provied to install()
    };

    node* list_;
};

}

#endif
