//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_TEST_HELPERS__
#define __HCE_COROUTINE_ENGINE_TEST_HELPERS__

namespace test {

/*
 Init is a special type created for the sole purpose of standardizing 
 initialization in templates from a number to type `T`. 

 With primitives normal initialization is no problem, but when using the same 
 template with `std::string`, for instance, it does cause problems (because
 normally a conversion function like `std::to_string` would be required).

 `init<T>`, and its specializations, provide a place for templates to get the 
 necessary conversions implicitly.
 */

// Provides a standard initialization API that enables template specialization
template <typename T>
struct init {
    // initialize value 
    template <typename... As>
    init(As&&... as) : t_(std::forward<As>(as)...) { }

    // trivial conversion
    inline operator T() { return std::move(t_); }

private:
    T t_;
};

// void* initialization specialization
template <>
struct init<void*> {
    template <typename... As>
    init(As&&... as) : t_(((void*)(size_t)as)...) { }

    inline operator void*() { return std::move(t_); }

private:
    void* t_;
};

// std::string initialization specialization
template <>
struct init<std::string> {
    template <typename... As>
    init(As&&... as) : t_(std::to_string(std::forward<As>(as)...)) { }

    inline operator std::string() { return std::move(t_); }

private:
    std::string t_;
};

struct CustomObject {
    CustomObject() : i_(0) {}
    CustomObject(const CustomObject&) = default;
    CustomObject(CustomObject&&) = default;

    CustomObject(int i) : i_(i) {}

    CustomObject& operator=(const CustomObject&) = default;
    CustomObject& operator=(CustomObject&&) = default;

    inline bool operator==(const CustomObject& rhs) const { 
        return i_ == rhs.i_; 
    }

    inline bool operator!=(const CustomObject& rhs) const { 
        return !(*this == rhs); 
    }

private:
    int i_;
};

}

#endif
