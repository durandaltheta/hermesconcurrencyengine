//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_OPAQUE_POINTER__
#define __HERMES_COROUTINE_ENGINE_OPAQUE_POINTER__

#include "logging.hpp"
#include "memory.hpp"

namespace hce {

/**
 @brief a type erased unique pointer 

 This opaque_pointer will properly destruct and delete its pointer. 
 */
struct opaque_pointer : public printable {
    opaque_pointer() { HCE_MIN_CONSTRUCTOR(); }
    opaque_pointer(const opaque_pointer&) = delete;

    opaque_pointer(opaque_pointer&& rhs) {
        HCE_MIN_CONSTRUCTOR(rhs);
        data_ = std::move(rhs.data_);
        deleter_ = std::move(rhs.deleter_);
    }

    template <typename T>
    opaque_pointer(T* t) {
        HCE_MIN_CONSTRUCTOR((void*)t);
        reset(t);
    }

    ~opaque_pointer() {
        HCE_MIN_DESTRUCTOR();
        destroy_();
    }

    opaque_pointer& operator=(const opaque_pointer&) = delete;

    opaque_pointer& operator=(opaque_pointer&& rhs) {
        HCE_MIN_METHOD_ENTER("operator=",rhs);
        data_ = std::move(rhs.data_);
        deleter_ = std::move(rhs.deleter_);
        return *this;
    }

    static inline std::string info_name() { return "hce::opaque_pointer"; }
    inline std::string name() const { return opaque_pointer::info_name(); }

    inline std::string content() const {
        std::stringstream ss;
        ss << data_;
        return ss.str();
    }

    inline operator bool() const { 
        HCE_MIN_METHOD_ENTER("operator bool",(bool)data_);
        return (bool)data_; 
    }

    inline void* get() const { 
        HCE_MIN_METHOD_ENTER("get");
        return data_; 
    }

    inline void* release() {
        HCE_MIN_METHOD_ENTER("release");
        void* data = data_;
        data_ = nullptr;
        return data;
    }
   
    template <typename T>
    inline void reset(T* t) {
        HCE_MIN_METHOD_ENTER("reset",(void*)t);
        destroy_();
        data_ = (void*)t;
        deleter_ = &opaque_pointer::deleter<T>;
    }

private:
    template <typename T>
    static inline void deleter(void* d) {
        reinterpret_cast<T*>(d)->~T();
        hce::deallocate(d);
    }

    inline void destroy_() {
        if(data_) { 
            deleter_(data_); 
            data_ = nullptr;
        }
    }

    void* data_ = nullptr;
    void (*deleter_)(void*);
};

}

#endif
