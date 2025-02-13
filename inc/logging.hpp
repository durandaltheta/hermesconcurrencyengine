//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef HERMES_COROUTINE_ENGINE_LOGGING
#define HERMES_COROUTINE_ENGINE_LOGGING

#include <utility>
#include <string>
#include <sstream>
#include <ostream>
#include <iostream>
#include <coroutine>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <any>
#include <mutex>
#include <condition_variable>

#include "loguru.hpp"
#include "utility.hpp"
#include "chrono.hpp"

/**
 User source code compile time macro determining compiled log code. Keeping this 
 low ensures that not only will code beneath the specified log level not print, 
 the logging code won't even be compiled because the statements will resolve to 
 an empty statement.

 What's interesting about this is that by setting the HCELOGLIMIT to -9, maximum 
 library performance will be achieved no matter how high the loglevel is . 
 However, it should realistically never be necessary to set HCELOGLIMIT lower 
 than -1, even in most production code, because any error messages may be very 
 important to have logged.

 A lot of the slightly odd design of the logging mechanics is to enforce "no 
 compile unless required" behavior, along with standardizing output to a high 
 degree. This enables consistent printing of identifying information, such as 
 object and thread addresses and optional object internal state for debugging 
 purposes.

 The reason most of these functions concatenate arguments rather than pass them 
 to a formatting string is because it allows the log functions to ingest 
 arguments dynamically into an internal stream and therefore be used in 
 parameter pack template code (which can have an undefined number of arguments). 
 Parameter packs have fewer limitations than old school `c` `va_args`.

 The `CONSTRUCTOR`, `DESTRUCTOR`, and `METHOD` logging macros can *only* be 
 called by implementions of `hce::printable`.  However, `FUNCTION` and `LOG` 
 macros can be called anywhere. `CONSTRUCTOR`, `DESTRUCTOR`, and `METHOD` 
 logging macros will print details about their object, such as its `this` 
 pointer address, namespace and (optional) object content/state.

 `ENTER` and `CONSTRUCTOR` (for object constructors specifically) macros are for 
 describing functions as they are being entered, all arguments to the macro are 
 interpretted as if they are arguments to a function.

 Example logline:
 HCE_FATAL_FUNCTION_ENTER("my_function", "string", "int");

 Would print something like:
 my_function(string, int)

 In comparision `BODY` macros are for writing arbitrary function loglines, and 
 will be interpretted not as arguments, but concatenated into a single logline:

 Whereas:
 HCE_FATAL_FUNCTION_BODY("my_function", "hello ", "world ", 3);

 Would print something like:
 my_function():hello world 3

 The `LOG` functions are more basic loggers provided as a fallback for when the 
 higher order functions won't suffice because precision output is required. They 
 accept a `printf()` style format string and arguments:

 This:
 HCE_FATAL_LOG("%s:%s:%d", "hello", "world", 3)

 would print something like:
 hello:world:3
 */
#ifndef HCELOGLIMIT
#define HCELOGLIMIT -1
#endif 

// HCELOGLIMIT min value is -9
#if HCELOGLIMIT < -9
#define HCELOGLIMIT -9
#endif

// HCELOGLIMIT max value is 9
#if HCELOGLIMIT > 9
#define HCELOGLIMIT 9
#endif

#if HCELOGLIMIT >= -3
#define HCE_FATAL_CONSTRUCTOR(...) hce::logger::constructor(this, loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_DESTRUCTOR() hce::logger::destructor(this, loguru::Verbosity_FATAL, __FILE__, __LINE__)
#define HCE_FATAL_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_FATAL_METHOD_ENTER(...) hce::logger::method_enter(this, loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_METHOD_BODY(...) hce::logger::method_body(this, loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_ENTER(...) hce::logger::function_enter(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_BODY(...) hce::logger::function_body(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_LOG(...) loguru::log(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else 
#define HCE_FATAL_CONSTRUCTOR(...) (void)0
#define HCE_FATAL_DESTRUCTOR() (void)0
#define HCE_FATAL_GUARD(...) (void)0
#define HCE_FATAL_METHOD_ENTER(...) (void)0
#define HCE_FATAL_METHOD_BODY(...) (void)0
#define HCE_FATAL_FUNCTION_ENTER(...) (void)0
#define HCE_FATAL_FUNCTION_BODY(...) (void)0
#define HCE_FATAL_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= -2
#define HCE_ERROR_CONSTRUCTOR(...) hce::logger::constructor(this, loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_DESTRUCTOR() hce::logger::destructor(this, loguru::Verbosity_ERROR, __FILE__, __LINE__)
#define HCE_ERROR_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_ERROR_METHOD_ENTER(...) hce::logger::method_enter(this, loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_METHOD_BODY(...) hce::logger::method_body(this, loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_ENTER(...) hce::logger::function_enter(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_BODY(...) hce::logger::function_body(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_LOG(...) loguru::log(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else 
#define HCE_ERROR_CONSTRUCTOR(...) (void)0
#define HCE_ERROR_DESTRUCTOR() (void)0
#define HCE_ERROR_GUARD(...) (void)0
#define HCE_ERROR_METHOD_ENTER(...) (void)0
#define HCE_ERROR_METHOD_BODY(...) (void)0
#define HCE_ERROR_FUNCTION_ENTER(...) (void)0
#define HCE_ERROR_FUNCTION_BODY(...) (void)0
#define HCE_ERROR_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= -1
#define HCE_WARNING_CONSTRUCTOR(...) hce::logger::constructor(this, loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_DESTRUCTOR() hce::logger::destructor(this, loguru::Verbosity_WARNING, __FILE__, __LINE__)
#define HCE_WARNING_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_WARNING_METHOD_ENTER(...) hce::logger::method_enter(this, loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_METHOD_BODY(...) hce::logger::method_body(this, loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_ENTER(...) hce::logger::function_enter(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_BODY(...) hce::logger::function_body(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_LOG(...) loguru::log(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_WARNING_CONSTRUCTOR(...) (void)0
#define HCE_WARNING_DESTRUCTOR() (void)0
#define HCE_WARNING_GUARD(...) (void)0
#define HCE_WARNING_METHOD_ENTER(...) (void)0
#define HCE_WARNING_METHOD_BODY(...) (void)0
#define HCE_WARNING_FUNCTION_ENTER(...) (void)0
#define HCE_WARNING_FUNCTION_BODY(...) (void)0
#define HCE_WARNING_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= 0
#define HCE_INFO_CONSTRUCTOR(...) hce::logger::constructor(this, loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_DESTRUCTOR() hce::logger::destructor(this, loguru::Verbosity_INFO, __FILE__, __LINE__)
#define HCE_INFO_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_INFO_METHOD_ENTER(...) hce::logger::method_enter(this, loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_METHOD_BODY(...) hce::logger::method_body(this, loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_ENTER(...) hce::logger::function_enter(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_BODY(...) hce::logger::function_body(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_LOG(...) loguru::log(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_INFO_CONSTRUCTOR(...) (void)0
#define HCE_INFO_DESTRUCTOR() (void)0
#define HCE_INFO_GUARD(...) (void)0
#define HCE_INFO_METHOD_ENTER(...) (void)0
#define HCE_INFO_METHOD_BODY(...) (void)0
#define HCE_INFO_FUNCTION_ENTER(...) (void)0
#define HCE_INFO_FUNCTION_BODY(...) (void)0
#define HCE_INFO_LOG(...) (void)0
#endif

// high criticality lifecycle
#if HCELOGLIMIT >= 1
#define HCE_HIGH_CONSTRUCTOR(...) hce::logger::constructor(this, 1, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_DESTRUCTOR() hce::logger::destructor(this, 1, __FILE__, __LINE__)
#define HCE_HIGH_GUARD(test, ...) if(test) { __VA_ARGS__; }
#else
#define HCE_HIGH_CONSTRUCTOR(...) (void)0
#define HCE_HIGH_DESTRUCTOR() (void)0
#define HCE_HIGH_GUARD(...) (void)0
#endif 

// high criticality functions and methods
#if HCELOGLIMIT >= 2
#define HCE_HIGH_METHOD_ENTER(...) hce::logger::method_enter(this, 2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_METHOD_BODY(...) hce::logger::method_body(this, 2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_ENTER(...) hce::logger::function_enter(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_BODY(...) hce::logger::function_body(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_LOG_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_HIGH_LOG(...) loguru::log(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_HIGH_METHOD_ENTER(...) (void)0
#define HCE_HIGH_METHOD_BODY(...) (void)0
#define HCE_HIGH_FUNCTION_ENTER(...) (void)0
#define HCE_HIGH_FUNCTION_BODY(...) (void)0
#define HCE_HIGH_LOG_GUARD(...) (void)0
#define HCE_HIGH_LOG(...) (void)0
#endif

// medium criticality lifecycle
#if HCELOGLIMIT >= 3
#define HCE_MED_CONSTRUCTOR(...) hce::logger::constructor(this, 3, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_DESTRUCTOR() hce::logger::destructor(this, 3, __FILE__, __LINE__)
#define HCE_MED_GUARD(test, ...) if(test) { __VA_ARGS__; }
#else
#define HCE_MED_CONSTRUCTOR(...) (void)0
#define HCE_MED_DESTRUCTOR() (void)0
#define HCE_MED_GUARD(...) (void)0
#endif 

// medium criticality functions and methods
#if HCELOGLIMIT >= 4
#define HCE_MED_METHOD_ENTER(...) hce::logger::method_enter(this, 4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_METHOD_BODY(...) hce::logger::method_body(this, 4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_ENTER(...) hce::logger::function_enter(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_BODY(...) hce::logger::function_body(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_LOG_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_MED_LOG(...) loguru::log(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MED_METHOD_ENTER(...) (void)0
#define HCE_MED_METHOD_BODY(...) (void)0
#define HCE_MED_FUNCTION_ENTER(...) (void)0
#define HCE_MED_FUNCTION_BODY(...) (void)0
#define HCE_MED_LOG_GUARD(...) (void)0
#define HCE_MED_LOG(...) (void)0
#endif

// low criticality lifecycle
#if HCELOGLIMIT >= 5
#define HCE_LOW_CONSTRUCTOR(...) hce::logger::constructor(this, 5, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_DESTRUCTOR() hce::logger::destructor(this, 5, __FILE__, __LINE__)
#define HCE_LOW_GUARD(test, ...) if(test) { __VA_ARGS__; }
#else
#define HCE_LOW_CONSTRUCTOR(...) (void)0
#define HCE_LOW_DESTRUCTOR() (void)0
#define HCE_LOW_GUARD(...) (void)0
#endif 

// low criticality functions and methods
#if HCELOGLIMIT >= 6
#define HCE_LOW_METHOD_ENTER(...) hce::logger::method_enter(this, 6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_METHOD_BODY(...) hce::logger::method_body(this, 6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_ENTER(...) hce::logger::function_enter(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_BODY(...) hce::logger::function_body(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_LOG_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_LOW_LOG(...) loguru::log(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_LOW_METHOD_ENTER(...) (void)0
#define HCE_LOW_METHOD_BODY(...) (void)0
#define HCE_LOW_FUNCTION_ENTER(...) (void)0
#define HCE_LOW_FUNCTION_BODY(...) (void)0
#define HCE_LOW_LOG_GUARD(...) (void)0
#define HCE_LOW_LOG(...) (void)0
#endif

// minimal criticality lifecycle
#if HCELOGLIMIT >= 7
#define HCE_MIN_CONSTRUCTOR(...) hce::logger::constructor(this, 7, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_DESTRUCTOR() hce::logger::destructor(this, 7, __FILE__, __LINE__)
#define HCE_MIN_GUARD(test, ...) if(test) { __VA_ARGS__; }
#else
#define HCE_MIN_CONSTRUCTOR(...) (void)0
#define HCE_MIN_DESTRUCTOR() (void)0
#define HCE_MIN_GUARD(...) (void)0
#endif 

// minimal criticality functions and methods
#if HCELOGLIMIT >= 8
#define HCE_MIN_METHOD_ENTER(...) hce::logger::method_enter(this, 8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_METHOD_BODY(...) hce::logger::method_body(this, 8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_ENTER(...) hce::logger::function_enter(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_BODY(...) hce::logger::function_body(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_LOG_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_MIN_LOG(...) loguru::log(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MIN_METHOD_ENTER(...) (void)0
#define HCE_MIN_METHOD_BODY(...) (void)0
#define HCE_MIN_FUNCTION_ENTER(...) (void)0
#define HCE_MIN_FUNCTION_BODY(...) (void)0
#define HCE_MIN_LOG_GUARD(...) (void)0
#define HCE_MIN_LOG(...) (void)0 
#endif

// trace logs are of such low importance, you only want to print them when
// trying to actively debug code when stepping through with a debugger would be 
// painful or otherwise not useful
#if HCELOGLIMIT >= 9
#define HCE_TRACE_CONSTRUCTOR(...) hce::logger::constructor(this, 9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_DESTRUCTOR() hce::logger::destructor(this, 9, __FILE__, __LINE__)
#define HCE_TRACE_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_TRACE_METHOD_ENTER(...) hce::logger::method_enter(this, 9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_METHOD_BODY(...) hce::logger::method_body(this, 9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_ENTER(...) hce::logger::function_enter(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_BODY(...) hce::logger::function_body(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_LOG_GUARD(test, ...) if(test) { __VA_ARGS__; }
#define HCE_TRACE_LOG(...) loguru::log(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_TRACE_CONSTRUCTOR(...) (void)0
#define HCE_TRACE_DESTRUCTOR() (void)0
#define HCE_TRACE_GUARD(...) (void)0
#define HCE_TRACE_METHOD_ENTER(...) (void)0
#define HCE_TRACE_METHOD_BODY(...) (void)0
#define HCE_TRACE_FUNCTION_ENTER(...) (void)0
#define HCE_TRACE_FUNCTION_BODY(...) (void)0
#define HCE_TRACE_LOG_GUARD(...) (void)0
#define HCE_TRACE_LOG(...) (void)0
#endif

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

template <>
struct info<std::chrono::steady_clock,void> {
    static inline std::string name(){ return "std::chrono::steady_clock"; }
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

template <typename Rep, typename Period>
struct info<std::chrono::duration<Rep,Period>,void> {
    static inline std::string name(){ 
        return templatize<Rep,Period>("std::chrono::duration");
    }
};

template <typename Clock, typename Duration>
struct info<std::chrono::time_point<Clock,Duration>,void> {
    static inline std::string name(){ 
        return templatize<Clock,Duration>("std::chrono::time_point"); 
    }
};

}

// time-to-string conversions
namespace chrono {
namespace detail {

template <typename Rep, typename Period>
inline void ingest_duration_ticks(
        std::stringstream& ss,
        const std::chrono::duration<Rep, Period>& d)
{
    ss << d.count() << " ";

    if (std::ratio_equal<Period, std::nano>::value) {
        ss << "ns";
    } else if (std::ratio_equal<Period, std::micro>::value) {
        ss << "µs";
    } else if (std::ratio_equal<Period, std::milli>::value) {
        ss << "ms";
    } else if (std::ratio_equal<Period, std::ratio<1>>::value) {
        ss << "s";
    } else if (std::ratio_equal<Period, std::ratio<60>>::value) {
        ss << "min";
    } else if (std::ratio_equal<Period, std::ratio<3600>>::value) {
        ss << "h";
    } else {
        ss << "unknown_period";
    }
}

}

template <typename Rep, typename Period>
inline std::string to_string(const std::chrono::duration<Rep, Period>& d) {
    std::stringstream ss;
    ss << hce::type::name<const std::chrono::duration<Rep, Period>&>() << "[";
    detail::ingest_duration_ticks(ss, d);
    ss << "]";
    return ss.str();
}

template <typename Clock, typename Duration>
inline std::string to_string(const std::chrono::time_point<Clock, Duration>& t) 
{
    std::stringstream ss;
    ss << hce::type::name<const std::chrono::time_point<Clock, Duration>&>() << "[";
    detail::ingest_duration_ticks(ss, t.time_since_epoch());
    ss << "]";
    return ss.str();
}

}

/*
 @brief interface for allowing an object instance to be printable

 Objects which implement printable can passed to streams (IE, 
 `std::stringstream`, `std::cout`) and also converted to `std::string` 
 representation.
 */
struct printable {
    /**
     It is expected that the returned string properly include namespaces and 
     be properly templatized as necessary.

     @return the object name 
     */
    virtual std::string name() const = 0;

    /**
     @brief string with optional content of this object 

     This method can be overwritten by the inheritor, useful for describing 
     internal elements of a given object, such as allocated pointers, or other 
     printable objects it contains.
     */
    virtual inline std::string content() const { return {}; }

    /// string conversion
    inline std::string to_string() const { 
        std::stringstream ss;

        // assemble the namespaced name and memory address of the object
        ss << this->name() << "@" << (void*)this;

        // check for object content
        std::string c = this->content();

        if(!(c.empty())) { 
            // put content in brackets to print like a container
            ss << "[" << c << "]";
        }

        return ss.str();
    }

    /// std::string conversion
    inline operator std::string() const { return to_string(); }
};

/// convenience Callable->std::string conversion
template <typename Callable>
inline std::string callable_to_string(Callable& f) {
    std::stringstream ss;
    ss << "callable@" << (void*)&f;
    return ss.str();
}

}

namespace std {

/// std:: printable string conversion
inline std::string to_string(const hce::printable& p) { return p.to_string(); }

}

/**
 :: ostream writing of a printable reference. An object which implements 
 printable can be passed by reference here without typecasting.
 */
inline std::ostream& operator<<(std::ostream& out, const hce::printable& p) {
    out << p.to_string();
    return out;
}

/**
 :: ostream writing of a printable pointer. An object which implements 
 printable can be passed by pointer here without typecasting.
 */
inline std::ostream& operator<<(std::ostream& out, const hce::printable* p) {
    if(p) { out << *p; } 
    else { out << "hce::printable@nullptr"; }
    return out;
}

/// :: ostream writing for generic coroutine_handle
template <typename PROMISE>
inline std::ostream& operator<<(std::ostream& out, const std::coroutine_handle<PROMISE>& h) {
    out << hce::type::name<const std::coroutine_handle<PROMISE>&>() 
        << "@"
        << h.address();
    return out;
}

namespace hce {
namespace config {
namespace logging {

/**
 @brief the process wide default_log_level

 Set to compiler define HCELOGLEVEL. Threads inherit this log level.

 @return the thread local log level
 */
int default_log_level();

/**
 Declaration of user replacable log initialization function 

 This method is called when logging is being initialized, and is responsible for
 calling any necessary `loguru::` namespace functions such as `loguru::init()`..
 */
void initialize();

}
}

/**
 @brief namespace object for underlying logging functions

 This object's namespace also contains a variety of static loglevel 
 introspection and modification methods.

 `logger` is rarely utilized by the user directly, but through macros which 
 determine at compile time if logging statements need to be written.
 */
struct logger {
    /**
     @brief threads inherit the default_log_level()
     @return the current thread local log level
     */
    static int thread_log_level();

    /**
     @brief set the thread local log level 

     Maximum value: 9
     Minimum value: -9

     @param the new log level for the calling thread
     */
    static void thread_log_level(int level);

    template <typename... As>
    static inline void constructor(const printable* p, 
                                   int verbosity, 
                                   const char* file, 
                                   int line, 
                                   As&&... as) {
        if(verbosity <= logger::thread_log_level()) {
            std::stringstream ss;
            logger::ingest_parameters_(ss, std::forward<As>(as)...);
            std::string self(*p);
            std::string ingested(ss.str());
            std::string name_str(type::basename(p->name()));

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        name_str.c_str(), 
                        ingested.c_str());
        }
    }

    static inline void destructor(const printable* p, 
                                  int verbosity, 
                                  const char* file, 
                                  int line) {
        if(verbosity <= logger::thread_log_level()) {
            std::string self(*p);
            std::string name_str(type::basename(p->name()));

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::~%s()", 
                        self.c_str(), 
                        name_str.c_str());
        }
    }

    template <typename... As>
    static inline void method_enter(const printable* p, 
                                    int verbosity, 
                                    const char* file, 
                                    int line, 
                                    std::string method_name, 
                                    As&&... as) {
        if(verbosity <= logger::thread_log_level()) {
            std::stringstream ss;
            logger::ingest_parameters_(ss, std::forward<As>(as)...);
            std::string self(*p);
            std::string ingested(ss.str());

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        method_name.c_str(), 
                        ingested.c_str());
        }
    }

    template <typename... As>
    static inline void method_body(const printable* p, 
                                   int verbosity, 
                                   const char* file, 
                                   int line, 
                                   std::string method_name, 
                                   As&&... as) {
        if(verbosity <= logger::thread_log_level()) {
            std::stringstream ss;
            logger::ingest_(ss, std::forward<As>(as)...);
            std::string self(*p);
            std::string ingested(ss.str());

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s():%s", 
                        self.c_str(), 
                        method_name.c_str(), 
                        ingested.c_str());
        }
    }

    template <typename... As>
    static inline void function_enter(int verbosity, 
                                      const char* file, 
                                      int line, 
                                      std::string method_name, 
                                      As&&... as) {
        if(verbosity <= logger::thread_log_level()) {
            std::stringstream ss;
            logger::ingest_parameters_(ss, std::forward<As>(as)...);
            std::string ingested(ss.str());

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s(%s)", 
                        method_name.c_str(), 
                        ingested.c_str());
        }
    }

    template <typename... As>
    static inline void function_body(int verbosity, 
                                     const char* file, 
                                     int line, 
                                     std::string method_name, 
                                     As&&... as) {
        if(verbosity <= logger::thread_log_level()) {
            std::stringstream ss;
            logger::ingest_(ss, std::forward<As>(as)...);
            std::string ingested(ss.str());

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s():%s", 
                        method_name.c_str(), 
                        ingested.c_str());
        }
    }

private:
    logger(){}

    // thread_local loglevel
    static int& tl_loglevel();

    // ingest a single item
    template <typename A>
    static inline void ingest_item_(std::stringstream& ss, A&& a) {
        ss << std::forward<A>(a);
    }

    // template specializations for duration
    template <class Rep, class Period>
    static inline void ingest_item_(
            std::stringstream& ss, 
            const std::chrono::duration<Rep,Period>& d) 
    {
        ss << hce::chrono::to_string(d);
    }

    template <class Rep, class Period>
    static inline void ingest_item_(
            std::stringstream& ss, 
            std::chrono::duration<Rep,Period>& d) 
    {
        ss << hce::chrono::to_string(d);
    }

    template <class Rep, class Period>
    static inline void ingest_item_(
            std::stringstream& ss, 
            std::chrono::duration<Rep,Period>&& d) 
    {
        ss << hce::chrono::to_string(d);
    }

    // template specializations for time_point
    template <class Clock, class Duration>
    static inline void ingest_item_(
            std::stringstream& ss, 
            const std::chrono::time_point<Clock, Duration>& tp) 
    {
        ss << hce::chrono::to_string(tp);
    }

    template <class Clock, class Duration>
    static inline void ingest_item_(
            std::stringstream& ss, 
            std::chrono::time_point<Clock, Duration>& tp) 
    {
        ss << hce::chrono::to_string(tp);
    }

    template <class Clock, class Duration>
    static inline void ingest_item_(
            std::stringstream& ss, 
            std::chrono::time_point<Clock, Duration>&& tp) 
    {
        ss << hce::chrono::to_string(tp);
    }

    // final ingest
    static inline void ingest_rest_of_args_(std::stringstream& ss) { }

    // in argument lists, begin inserting "," between arguments
    template <typename A, typename... As>
    static inline void ingest_rest_of_args_(std::stringstream& ss, A&& a, As&&... as) {
        ss << ", "; 
        ingest_item_(ss,std::forward<A>(a));
        ingest_rest_of_args_(ss, std::forward<As>(as)...);
    }
   
    // final ingest
    static inline void ingest_parameters_(std::stringstream& ss) { }

    // for ingesting a list of function or method arguments
    template <typename A, typename... As>
    static inline void ingest_parameters_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(ss,std::forward<A>(a));
        ingest_rest_of_args_(ss, std::forward<As>(as)...);
    }
    
    // final ingest
    static inline void ingest_(std::stringstream& ss) { }

    // for ingesting arbitrary data into a logline
    template <typename A, typename... As>
    static inline void ingest_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(ss,std::forward<A>(a));
        ingest_(ss, std::forward<As>(as)...);
    }
};

}

#endif
