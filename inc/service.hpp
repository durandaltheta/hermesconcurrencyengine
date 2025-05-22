//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_SERVICE
#define HERMES_COROUTINE_ENGINE_SERVICE 

#include "base.hpp"
#include "logging.hpp"

namespace hce {

/**
 @brief services are singleton objects 

 Implementations of this CRTP (curiously recurring template pattern) are 
 singleton objects with a consistent accessor pattern:
 ```
 hce::service<IMPLEMENTATION>::get(); // return the singleton
 ```
 */
template <typename IMPLEMENTATION>
struct service {
    service() {
        service<IMPLEMENTATION>::ptr_ref() = static_cast<IMPLEMENTATION*>(this);
    }

    virtual ~service() { 
        service<IMPLEMENTATION>::ptr_ref() = nullptr;
    }

    /**
     @return true if the service exists, else false
     */
    static inline bool ready() { return ptr_ref() != nullptr; }

    /**
     @return the implementation 
     */
    static inline IMPLEMENTATION& get() { return *(ptr_ref()); }

private:
    // get a reference to the instanced pointer
    static IMPLEMENTATION*& ptr_ref();
    friend struct environment;
};

}

#endif
