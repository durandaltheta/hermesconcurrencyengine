#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <coroutine>
#include <memory>
#include <cstdlib>
#include <any>
#include <mutex>
#include <condition_variable>

#include "hce/utility.hpp"

namespace hce {

/**
 @brief namespace for type information deduction 

 There are two types of type name string generation used in this library:
 - hce::type::info disambiguation
 - hce::printable object interface

 The `hce::type::info` name string generation uses *only* type information. This 
 is used for printing the types of templates. For instance, if an object is a 
 template for some typename `T`, this form of name generation is utilized for 
 printing a readable name for `T` without requiring an actual instance of a 
 given object or variable of type `T`. This is necessary because the c++ 
 standard does not guarantee a readable name when using `typeid(T).name()`. 
 However, this library will fall back to `typeid(T).name()` as necessary.

 Logging of object instances is enabled by that object implementing the 
 `hce::printable` interface. This enables introspections of the object's name,
 as well as printing information about that object instance's address and 
 state. However, implementations of `hce::printable` generally rely on 
 `hce::type::info` specializations to acquire type name strings.
 */
namespace type {

/// return a string representing the cv type qualifier
template <typename T>
std::string cv_name() {
    if constexpr (std::is_const_v<T>) {
        if constexpr (std::is_volatile_v<T>) {
            return "const volatile";
        } else {
            return "const";
        }
    } else if constexpr (std::is_volatile_v<T>) {
        return "volatile";
    } else {
        return ""; // no qualifiers
    }
}

/// return a string representing the reference type qualifier
template <typename T>
std::string reference_name() {
    if constexpr (std::is_pointer_v<T>) {
        return "*";
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        return "&";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "&&";
    } else {
        return ""; // no qualifiers
    }
}

/// acquire the base name of an object without namespace or template text
inline std::string basename(std::string name) {
    // find the last occurrence of "::"
    size_t pos = name.rfind("::");

    if (pos != std::string::npos) [[likely]] {
        // modify n, removing the namespace text 
        name = name.substr(pos + 2);
    } 

    // find the last '<'
    size_t open_pos = name.rfind('<');
    // find the last '>' that comes after the last '<'
    size_t close_pos = name.rfind('>');

    if (open_pos != std::string::npos && 
        close_pos != std::string::npos && 
        open_pos < close_pos) 
    {
        // remove everything between and including the '<' and '>'
        name = name.substr(0, open_pos);
    }

    return name;
}

/**
 @brief a general template for acquiring a type `T`'s stringified name 

 This object can be specialized (specializations of primitives and some standard 
 std:: objects are provided). If a specialization is not provided and a method 
 `static std::string T::info_name()` is available, then this library will select 
 an `info` template to call that method. Otherwise the compiler's naming 
 conventions (`typeid(T).name()`) will be used as a final fallback. However, 
 compiler naming is not guaranteed to be very readable. 

 User code can choose to enhance their logging by either implementing an 
 `hce::type::info<USER_T>` specialization OR providing a static method
 `static std::string USER_T::info_name()` for the given type `USER_T`.

 This object and its specializations are intended to be used internally by other 
 mechanisms, namely `hce::type::name<T>()` and `hce::type::templatize<Ts...>()`, 
 and generally don't need to be used directly.

 `struct info` specializations should always be for a base, unqualified `T`. 
 That is, type `T` should not be a reference, pointer, const, volatile, or 
 mutable. The `hce::type::name<T>()` mechanism will take care of those details.
 */
template <typename T, typename = void>
struct info {
    // fallback name string
    static inline std::string name(){ return typeid(T).name(); }
};

/**
 Specialization for types with method `static std::string info_name()`

 The compiler sometimes has a really hard time with partial specializations of 
 template dependent types (IE, hce::co<T>::promise_type). This is an alternative 
 route to allow types to get `hce::type::info` support (and thus printability) 
 by defining `static inline std::string info_name()`. For example, in
 `hce::co<T>::promise_type` the following is defined:
 ```
 static inline std::string info_name() { 
     return type::name<co<T>>() + "::promise_type"; 
 }
 ```

 The above implementation allows this `hce::type::info` template to be selected 
 when `hce::type::info<hce::co<T>::promise_type>::name()` is called.
 */
template <typename T>
struct info<T, std::void_t<decltype(T::info_name())>> {
    static inline std::string name() { 
        return T::info_name(); 
    }
};

// void 

template <>
struct info<void,void> {
    static inline std::string name(){ return "void"; }
};

// int 

template <>
struct info<int,void> {
    static inline std::string name(){ return "int"; }
};

template <>
struct info<unsigned int,void> {
    static inline std::string name(){ return "unsigned int"; }
};

template <>
struct info<short int,void> {
    static inline std::string name(){ return "short int"; }
};

template <>
struct info<unsigned short int,void> {
    static inline std::string name(){ return "unsigned short int"; }
};

template <>
struct info<long int,void> {
    static inline std::string name(){ return "long int"; }
};

template <>
struct info<unsigned long int,void> {
    static inline std::string name(){ return "unsigned long int"; }
};

template <>
struct info<long long int,void> {
    static inline std::string name(){ return "long long int"; }
};

template <>
struct info<unsigned long long int,void> {
    static inline std::string name(){ return "unsigned long long int"; }
};

// bool

template <>
struct info<bool,void> {
    static inline std::string name(){ return "bool"; }
};

// float 

template <>
struct info<float,void> {
    static inline std::string name(){ return "float"; }
};

// double 

template <>
struct info<double,void> {
    static inline std::string name(){ return "double"; }
};

template <>
struct info<long double,void> {
    static inline std::string name(){ return "long double"; }
};

// char 

template <>
struct info<char,void> {
    static inline std::string name(){ return "char"; }
};

template <>
struct info<signed char,void> {
    static inline std::string name(){ return "signed char"; }
};

template <>
struct info<unsigned char,void> {
    static inline std::string name(){ return "unsigned char"; }
};

template <>
struct info<wchar_t,void> {
    static inline std::string name(){ return "wchar_t"; }
};

template <>
struct info<char16_t,void> {
    static inline std::string name(){ return "char16_t"; }
};

template <>
struct info<char32_t,void> {
    static inline std::string name(){ return "char32_t"; }
};

// additional info name specializations

template <>
struct info<std::byte,void> {
    static inline std::string name(){ return "std::byte"; }
};

template <>
struct info<std::string,void> {
    static inline std::string name(){ return "std::string"; }
};

template <>
struct info<std::any,void> {
    static inline std::string name(){ return "std::any"; }
};

template <>
struct info<std::mutex,void> {
    static inline std::string name(){ return "std::mutex"; }
};

template <>
struct info<std::condition_variable,void> {
    static inline std::string name(){ return "std::condition_variable"; }
};

template <>
struct info<std::condition_variable_any,void> {
    static inline std::string name(){ return "std::condition_variable_any"; }
};

template <std::intmax_t Num, std::intmax_t Denom>
struct info<std::ratio<Num,Denom>,void> {
    static inline std::string name(){ 
        std::stringstream ss;
        ss << "std::ratio<" << Num << "," << Denom << ">";
        return ss.str();
    }
};

/**
 @brief acquire a runtime accessible name string of a type T

 This is the framework's source for getting name strings for arbitrary types 
 for when there is no instance of said type. That is, when a name for a given 
 type `T` is required but no object or variable of `T` is present (and therefore 
 `hce::printable` interface cannot be implemented). 

 @return a name string
 */
template <typename T>
inline std::string name() { 
    std::stringstream ss;
    ss << cv_name<T>() << info<unqualified<T>>::name() << reference_name<T>(); 
    return ss.str();
};

template <>
inline std::string name<void>() { 
    std::stringstream ss;
    ss << cv_name<void>() << info<void,void>::name() << reference_name<void>(); 
    return ss.str();
};

template <>
inline std::string name<void*>() { 
    std::stringstream ss;
    ss << cv_name<void*>() << info<void,void>::name() << reference_name<void*>(); 
    return ss.str();
};

template <>
inline std::string name<const void*>() { 
    std::stringstream ss;
    ss << cv_name<const void*>() << info<void,void>::name() << reference_name<void*>(); 
    return ss.str();
};

namespace detail {

template <typename T>
inline void templatize_ingest(std::stringstream& ss) {
    ss << name<T>();
}

template <typename T>
inline void templatize_rest(std::stringstream& ss) { 
    ss << ",";
    templatize_ingest<T>(ss);
}

template <typename T, typename T2, typename... Ts>
inline void templatize_rest(std::stringstream& ss) {
    ss << ",";
    templatize_ingest<T>(ss);
    templatize_rest<T2,Ts...>(ss);
}

template <typename T>
inline void templatize(std::stringstream& ss) { 
    templatize_ingest<T>(ss);
    ss << ">";
}

template <typename T, typename T2, typename... Ts>
inline void templatize(std::stringstream& ss) {
    templatize_ingest<T>(ss);

    // get the names of any further types in the template list
    templatize_rest<T2,Ts...>(ss);
   
    // append the final tag
    ss << ">";
}

}

/**
 @brief transform a string by appendng template tags and template types' names

 The final string returned by `templatize` will use the real type names instead 
 of `Ts...` or any other template typename. For example, the resulting string 
 when calling `hce::type::name<my_type<int,std::string>>()` will be 
 "my_type<int,std::string>".
 */
template <typename T, typename... Ts>
inline std::string templatize(const std::string& s) {
    std::stringstream ss;

    // get the template base name
    ss << s << "<";

    // get the names of any further types in the template list
    detail::templatize<T,Ts...>(ss);
    return ss.str();
}

// templatize dependent infos

template <typename T>
struct info<std::coroutine_handle<T>,void> {
    static inline std::string name(){ 
        return templatize<T>("std::coroutine_handle"); 
    }
};

template <typename T>
struct info<std::unique_ptr<T>,void> {
    static inline std::string name(){ return templatize<T>("std::unique_ptr"); }
};

template <typename T>
struct info<std::shared_ptr<T>,void> {
    static inline std::string name(){ return templatize<T>("std::shared_ptr"); }
};

template <typename T>
struct info<std::weak_ptr<T>,void> {
    static inline std::string name(){ return templatize<T>("std::weak_ptr"); }
};

} // type
} // hce 

namespace std {

/// :: ostream writing for generic coroutine_handle
template <typename PROMISE>
inline std::ostream& operator<<(std::ostream& out, const std::coroutine_handle<PROMISE>& h) {
    out << hce::type::name<const std::coroutine_handle<PROMISE>&>() 
        << "@"
        << h.address();
    return out;
}
 
} // std
