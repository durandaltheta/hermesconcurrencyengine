#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <utility>
#include <string>
#include <sstream>
#include <ostream>

#include "loguru.hpp"
#include "hce/utility/type.hpp"
#include "hce/printable.hpp"

/**
 User source code compile time macro determining compiled log code. Keeping this 
 low ensures that not only will code beneath the specified log level not print, 
 the logging code won't even be compiled because the macro will resolve to an
 empty statement.

 What's interesting about this is that by setting the HCELOGLIMIT to -9, maximum 
 library performance will be achieved no matter how high the loglevel is . 
 However, it should realistically never be necessary to set HCELOGLIMIT lower 
 than -1, even in most production code, because any error messages may be very 
 important to have logged. Additionally, calls to log error messages are rare,
 so performance shouldn't take a hit.

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
 pointer address, namespace+name and (optional) object content/state.

 `ENTER` and `CONSTRUCTOR` (for object constructors specifically) macros are for 
 describing functions as they are being entered, all arguments to the macro are 
 interpretted as if they are arguments to a function.

 Example ENTER logline:
 HCE_FATAL_FUNCTION_ENTER("my_function", "string", "int");

 Would print something like:
 my_function(string, int)

 In comparision `BODY` macros are for writing arbitrary function loglines, and 
 will be interpretted not as arguments, but concatenated into a single logline.

 Example BODY logline:
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
namespace config {
namespace logging {

/**
 @brief the process wide default_log_level

 Set to compiler define HCELOGLEVEL. Threads inherit this log level.

 @return the thread local log level
 */
int default_log_level();

} // logging
} // config

namespace detail {
namespace logger {

// ingest a single item
template <typename A>
static inline void ingest_item_(std::stringstream& ss, A&& a) {
    ss << std::forward<A>(a);
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

} // logger 
} // detail

/**
 @brief namespace object for underlying logging functions

 This object's namespace also contains a variety of static loglevel 
 introspection and modification methods.

 `logger` is rarely utilized by the user directly, but through macros which 
 determine at compile time if logging statements need to be written.
 */
struct logger {
    /// convenience Callable->std::string conversion
    template <typename Callable>
    static inline std::string callable_to_string(Callable& f) {
        std::stringstream ss;
        ss << "callable@" << (void*)&f;
        return ss.str();
    }

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
            detail::logger::ingest_parameters_(ss, std::forward<As>(as)...);
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
            detail::logger::ingest_parameters_(ss, std::forward<As>(as)...);
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
            detail::logger::ingest_(ss, std::forward<As>(as)...);
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
            detail::logger::ingest_parameters_(ss, std::forward<As>(as)...);
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
            detail::logger::ingest_(ss, std::forward<As>(as)...);
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
};

} // hce
