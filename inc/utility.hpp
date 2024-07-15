//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#ifndef __HCE_COROUTINE_ENGINE_UTILITY__
#define __HCE_COROUTINE_ENGINE_UTILITY__

#include <utility>
#include <functional>
#include <type_traits>
#include <deque>
#include <string>
#include <sstream>
#include <ostream>
#include <coroutine>
#include <memory>

#include "loguru.hpp"

#ifndef HCE_LOGGING_MACROS
#define HCE_LOGGING_MACROS

/**
 User compile time macro determining compiled log code. Keeping this low ensures 
 that not only will code beneath the specified log level not print, the logging 
 code won't even be compiled because the statements will resolve to an empty 
 statement.

 What's interesting about this is that by setting the HCELOGLIMIT to -9, maximum 
 library performance will be achieved no matter how high the library's default 
 loglevel is compiled with. However, it should realistically never be necessary 
 to set HCELOGLIMIT lower than -1, even in most production code, because any 
 error messages may be very important to have logged.

 A lot of the slightly odd design of the logging mechanics is to enforce "no 
 compile unless required" behavior, along with standardizing output to a high 
 degree. This enables consistent printing of identifying information, such as 
 object addresses and optional object internal state for debugging purposes.

 The reason most of these functions concatenate arguments rather than pass them 
 to a formatting string is because it allows the log functions to ingest 
 arguments dynamically into an internal stream and therefore be used in 
 parameter pack template code which can have an undefined number of arguments. 
 Parameter packs have fewer limitations than old school `c` `va_args`.

 The `CONSTRUCTOR`, `DESTRUCTOR`, and `METHOD` logging macros can *only* be 
 called by implementors of `hce::printable`.  However, `FUNCTION` and `LOG` 
 macros can be called anywhere. `CONSTRUCTOR`, `DESTRUCTOR`, and `METHOD` 
 logging macros will print details about their object, such as its `this` 
 pointer and function namespaces.

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
#if HCELOGLIMIT < -10
#define HCELOGLIMIT -9
#endif

// HCELOGLIMIT max value is 9
#if HCELOGLIMIT > 9
#define HCELOGLIMIT 9
#endif

#if HCELOGLIMIT >= -3
#define HCE_FATAL_CONSTRUCTOR(...) this->log_constructor__(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_DESTRUCTOR() this->log_destructor__(loguru::Verbosity_FATAL, __FILE__, __LINE__)
#define HCE_FATAL_METHOD_ENTER(...) this->log_method_enter__(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_METHOD_BODY(...) this->log_method_body__(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_ENTER(...) hce::printable::log_function_enter__(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_BODY(...) hce::printable::log_function_body__(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_LOG(...) loguru::log(loguru::Verbosity_FATAL, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else 
#define HCE_FATAL_CONSTRUCTOR(...) (void)0
#define HCE_FATAL_DESTRUCTOR() (void)0
#define HCE_FATAL_METHOD_ENTER(...) (void)0
#define HCE_FATAL_METHOD_BODY(...) (void)0
#define HCE_FATAL_FUNCTION_ENTER(...) (void)0
#define HCE_FATAL_FUNCTION_BODY(...) (void)0
#define HCE_FATAL_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= -2
#define HCE_ERROR_CONSTRUCTOR(...) this->log_constructor__(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_DESTRUCTOR() this->log_destructor__(loguru::Verbosity_ERROR, __FILE__, __LINE__)
#define HCE_ERROR_METHOD_ENTER(...) this->log_method_enter__(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_METHOD_BODY(...) this->log_method_body__(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_ENTER(...) hce::printable::log_function_enter__(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_BODY(...) hce::printable::log_function_body__(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_LOG(...) loguru::log(loguru::Verbosity_ERROR, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else 
#define HCE_ERROR_CONSTRUCTOR(...) (void)0
#define HCE_ERROR_DESTRUCTOR() (void)0
#define HCE_ERROR_METHOD_ENTER(...) (void)0
#define HCE_ERROR_METHOD_BODY(...) (void)0
#define HCE_ERROR_FUNCTION_ENTER(...) (void)0
#define HCE_ERROR_FUNCTION_BODY(...) (void)0
#define HCE_ERROR_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= -1
#define HCE_WARNING_CONSTRUCTOR(...) this->log_constructor__(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_DESTRUCTOR() this->log_destructor__(loguru::Verbosity_WARNING, __FILE__, __LINE__)
#define HCE_WARNING_METHOD_ENTER(...) this->log_method_enter__(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_METHOD_BODY(...) this->log_method_body__(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_ENTER(...) hce::printable::log_function_enter__(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_BODY(...) hce::printable::log_function_body__(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_LOG(...) loguru::log(loguru::Verbosity_WARNING, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_WARNING_CONSTRUCTOR(...) (void)0
#define HCE_WARNING_DESTRUCTOR() (void)0
#define HCE_WARNING_METHOD_ENTER(...) (void)0
#define HCE_WARNING_METHOD_BODY(...) (void)0
#define HCE_WARNING_FUNCTION_ENTER(...) (void)0
#define HCE_WARNING_FUNCTION_BODY(...) (void)0
#define HCE_WARNING_LOG(...) (void)0
#endif

#if HCELOGLIMIT >= 0
#define HCE_INFO_CONSTRUCTOR(...) this->log_constructor__(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_DESTRUCTOR() this->log_destructor__(loguru::Verbosity_INFO, __FILE__, __LINE__)
#define HCE_INFO_METHOD_ENTER(...) this->log_method_enter__(loguru::Verbosity_INF, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_METHOD_BODY(...) this->log_method_body__(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_ENTER(...) hce::printable::log_function_enter__(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_BODY(...) hce::printable::log_function_body__(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_LOG(...) loguru::log(loguru::Verbosity_INFO, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_INFO_CONSTRUCTOR(...) (void)0
#define HCE_INFO_DESTRUCTOR() (void)0
#define HCE_INFO_METHOD_ENTER(...) (void)0
#define HCE_INFO_METHOD_BODY(...) (void)0
#define HCE_INFO_FUNCTION_ENTER(...) (void)0
#define HCE_INFO_FUNCTION_BODY(...) (void)0
#define HCE_INFO_LOG(...) (void)0
#endif

// high criticality lifecycle
#if HCELOGLIMIT >= 1
#define HCE_HIGH_CONSTRUCTOR(...) this->log_constructor__(1, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_DESTRUCTOR() this->log_destructor__(1, __FILE__, __LINE__)
#else
#define HCE_HIGH_CONSTRUCTOR(...) (void)0
#define HCE_HIGH_DESTRUCTOR() (void)0
#endif 

// high criticality functions and methods
#if HCELOGLIMIT >= 2
#define HCE_HIGH_METHOD_ENTER(...) this->log_method_enter__(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_METHOD_BODY(...) this->log_method_body__(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_ENTER(...) hce::printable::log_function_enter__(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_BODY(...) hce::printable::log_function_body__(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_LOG(...) loguru::log(2, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_HIGH_METHOD_ENTER(...) (void)0
#define HCE_HIGH_METHOD_BODY(...) (void)0
#define HCE_HIGH_FUNCTION_ENTER(...) (void)0
#define HCE_HIGH_FUNCTION_BODY(...) (void)0
#define HCE_HIGH_LOG(...) (void)0
#endif

// medium criticality lifecycle
#if HCELOGLIMIT >= 3
#define HCE_MED_CONSTRUCTOR(...) this->log_constructor__(3, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_DESTRUCTOR() this->log_destructor__(3, __FILE__, __LINE__)
#else
#define HCE_MED_CONSTRUCTOR(...) (void)0
#define HCE_MED_DESTRUCTOR() (void)0
#endif 

// medium criticality functions and methods
#if HCELOGLIMIT >= 4
#define HCE_MED_METHOD_ENTER(...) this->log_method_enter__(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_METHOD_BODY(...) this->log_method_body__(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_ENTER(...) hce::printable::log_function_enter__(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_BODY(...) hce::printable::log_function_body__(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_LOG(...) loguru::log(4, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MED_METHOD_ENTER(...) (void)0
#define HCE_MED_METHOD_BODY(...) (void)0
#define HCE_MED_FUNCTION_ENTER(...) (void)0
#define HCE_MED_FUNCTION_BODY(...) (void)0
#define HCE_MED_LOG(...) (void)0
#endif

// low criticality lifecycle
#if HCELOGLIMIT >= 5
#define HCE_LOW_CONSTRUCTOR(...) this->log_constructor__(5, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_DESTRUCTOR() this->log_destructor__(5, __FILE__, __LINE__)
#else
#define HCE_LOW_CONSTRUCTOR(...) (void)0
#define HCE_LOW_DESTRUCTOR() (void)0
#endif 

// low criticality functions and methods
#if HCELOGLIMIT >= 6
#define HCE_LOW_METHOD_ENTER(...) this->log_method_enter__(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_METHOD_BODY(...) this->log_method_body__(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_ENTER(...) hce::printable::log_function_enter__(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_BODY(...) hce::printable::log_function_body__(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_LOG(...) loguru::log(6, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_LOW_METHOD_ENTER(...) (void)0
#define HCE_LOW_METHOD_BODY(...) (void)0
#define HCE_LOW_FUNCTION_ENTER(...) (void)0
#define HCE_LOW_FUNCTION_BODY(...) (void)0
#define HCE_LOW_LOG(...) (void)0
#endif

// minimal criticality lifecycle
#if HCELOGLIMIT >= 7
#define HCE_MIN_CONSTRUCTOR(...) this->log_constructor__(7, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_DESTRUCTOR() this->log_destructor__(7, __FILE__, __LINE__)
#else
#define HCE_MIN_CONSTRUCTOR(...) (void)0
#define HCE_MIN_DESTRUCTOR() (void)0
#endif 

// minimal criticality functions and methods
#if HCELOGLIMIT >= 8
#define HCE_MIN_METHOD_ENTER(...) this->log_method_enter__(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_METHOD_BODY(...) this->log_method_body__(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_ENTER(...) hce::printable::log_function_enter__(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_BODY(...) hce::printable::log_function_body__(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_LOG(...) loguru::log(8, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MIN_METHOD_ENTER(...) (void)0
#define HCE_MIN_METHOD_BODY(...) (void)0
#define HCE_MIN_FUNCTION_ENTER(...) (void)0
#define HCE_MIN_FUNCTION_BODY(...) (void)0
#define HCE_MIN_LOG(...) (void)0 
#endif

// trace logs are of such low importance, you only want to print them when
// trying to actively debug code when stepping through with a debugger would be 
// painful or otherwise not useful
#if HCELOGLIMIT >= 9
#define HCE_TRACE_CONSTRUCTOR(...) this->log_constructor__(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_DESTRUCTOR() this->log_destructor__(9, __FILE__, __LINE__)
#define HCE_TRACE_METHOD_ENTER(...) this->log_method_enter__(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_METHOD_BODY(...) this->log_method_body__(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_ENTER(...) hce::printable::log_function_enter__(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_BODY(...) hce::printable::log_function_body__(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_LOG(...) loguru::log(9, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_TRACE_CONSTRUCTOR(...) (void)0
#define HCE_TRACE_DESTRUCTOR() (void)0
#define HCE_TRACE_METHOD_ENTER(...) (void)0
#define HCE_TRACE_METHOD_BODY(...) (void)0
#define HCE_TRACE_FUNCTION_ENTER(...) (void)0
#define HCE_TRACE_FUNCTION_BODY(...) (void)0
#define HCE_TRACE_LOG(...) (void)0
#endif

#endif

namespace hce {
namespace detail {

template <typename T>
using unqualified = typename std::decay<T>::type;

template <typename F, typename... Ts>
using function_return_type = typename std::invoke_result<unqualified<F>,Ts...>::type;

template<typename T, typename _ = void>
struct is_container : std::false_type {};

template<typename... Ts>
struct is_container_helper {};

template< bool B, class T, class F >
using conditional_t = typename std::conditional<B,T,F>::type;

template<typename T>
struct is_container<
        T,
        conditional_t<
            false,
            is_container_helper<
                typename T::value_type,
                typename T::iterator,
                decltype(std::declval<T>().begin()),
                decltype(std::declval<T>().end())
                >,
            void
            >
        > : public std::true_type {};

}

/// Callable accepting and returning no arguments
typedef std::function<void()> thunk;

/*
 @brief interface for allowing an object to be printable

 Objects which implement printable can passed to streams (IE, 
 `std::stringstream`, `std::cout`) and also converted to `std::string` 
 representation.
 */
struct printable {
    /// return the namespace of the object
    virtual const char* nspace() const = 0;

    /// return the object name
    virtual const char* name() const = 0;

    /**
     @brief string with optional content of this object 

     This method can be overwritten by the inheritor, useful for describing 
     internal elements of a given object, such as allocated pointers.
     */
    virtual std::string content() const { return {}; }

    /// string conversion
    inline std::string to_string() const { 
        std::stringstream ss;
        ss << this->nspace()
           << "::"
           << this->name()
           << "@" 
           << (void*)this;

        std::string c = this->content();

        if(!(c.empty())) { 
            // put content in brackets to print like a container
            ss << "[" << c << "]";
        }

        return ss.str();
    }

    /// string cast conversion
    virtual inline operator std::string() const final { return to_string(); }

    /**
     @brief the process wide default_log_level
     
     Set to compiler define HCELOGLEVEL. Threads inherit this log level.

     @return the thread local log level
     */
    static int default_log_level();

    /**
     @brief threads inherit the default_log_level()
     @return the current thread local log level
     */
    static int thread_log_level();

    /**
     @brief set the thread local log level 
     @param the new log level for the calling thread
     */
    static void thread_log_level(int level);

    //--------------------------------------------------------------------------
    // The following public methods should be called by macros *ONLY*
    
    template <typename... As>
    void log_constructor__(int verbosity, const char* file, int line, As&&... as) const {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_args_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        this->name(), 
                        ss.str().c_str());
        }
    }

    inline void log_destructor__(int verbosity, const char* file, int line) const {
        if(verbosity <= printable::thread_log_level()) {
            std::string self(*this);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::~%s()", 
                        self.c_str(), 
                        this->name());
        }
    }

    template <typename... As>
    void log_method_enter__(int verbosity, const char* file, int line, const char* method_name, As&&... as) const {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_args_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    void log_method_body__(int verbosity, const char* file, int line, const char* method_name, As&&... as) const {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s::%s():%s", 
                        self.c_str(), 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    static void log_function_enter__(int verbosity, const char* file, int line, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s(%s)", 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    static void log_function_body__(int verbosity, const char* file, int line, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);

            loguru::log(verbosity, 
                        file, 
                        line, 
                        "%s():%s", 
                        method_name, 
                        ss.str().c_str());
        }
    }

private:
    // thread_local loglevel
    static int& tl_loglevel();

    // ingest a container of items, prints like: [elem1, elem2, elem3]
    template <typename A>
    static void ingest_item_disambiguation_(std::true_type, std::stringstream& ss, A&& container) {
        auto it = container.begin();
        auto end = container.end();
        
        ss << "[";

        if(it!=end) {
            ss << *it;
            ++it;

            while(it!=end) {
                ss << ", ";
                ss << *it;
                ++it;
            }
        }

        ss << "]";
    }

    // ingest a single item
    template <typename A>
    static void ingest_item_disambiguation_(std::false_type, std::stringstream& ss, A&& a) {
        ss << std::forward<A>(a);
    }
   
    // catch any variant of `std::basic_string<>` (IE, `std::string`)
    template <typename CharT, typename Traits, typename Alloc>
    static void ingest_item_(
            std::stringstream& ss, 
            const std::basic_string<CharT, Traits, Alloc>& s) {
        ss << s;
    }
    
    // catch any variant of `std::basic_string<>` (IE, `std::string`)
    template <typename CharT, typename Traits, typename Alloc>
    static void ingest_item_(
            std::stringstream& ss, 
            std::basic_string<CharT, Traits, Alloc>&& s) {
        ss << s;
    }

    // disambiguate between containers and other printable items
    template <typename A>
    static void ingest_item_(std::stringstream& ss, A&& a) {
        ingest_item_disambiguation_(
            detail::is_container<typename std::decay<A>::type>(),
            ss,
            std::forward<A>(a));
    }

    static inline void ingest_rest_of_args_(std::stringstream& ss) { }

    // in argument lists, begin inserting "," between arguments
    template <typename A, typename... As>
    static void ingest_rest_of_args_(std::stringstream& ss, A&& a, As&&... as) {
        ss << ", "; 
        ingest_item_(ss,std::forward<A>(a));
        ingest_rest_of_args_(ss, std::forward<As>(as)...);
    }
   
    // final ingest
    static inline void ingest_args_(std::stringstream& ss) { }

    // for ingesting a list of function or method arguments
    template <typename A, typename... As>
    static void ingest_args_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(ss,std::forward<A>(a));
        ingest_rest_of_args_(ss, std::forward<As>(as)...);
    }
    
    // final ingest
    static inline void ingest_(std::stringstream& ss) { }

    // for ingesting arbitrary data into a logline
    template <typename A, typename... As>
    static void ingest_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(ss,std::forward<A>(a));
        ingest_(ss, std::forward<As>(as)...);
    }
};

namespace detail {
namespace utility {

/// convenience std::function->std::string conversion
template <typename Callable>
inline std::string callable_to_string(Callable& f) {
    std::stringstream ss;
    ss << "callable@" << (void*)&f;
    return ss.str();
}

}
}

/**
 @brief call runtime handlers on object destructor

 It is worth pointing out that `T` can be a reference, like `int&`, or a pointer 
 `int*`.
 */
template <typename T>
struct cleanup : public printable {
    typedef std::function<void(T)> handler;

    /// construct T
    template <typename... As>
    cleanup(As&&... as) : t_(std::forward<As>(as)...) {
        HCE_MED_CONSTRUCTOR();
    }

    /// destructor calls all handlers with T
    virtual ~cleanup() { 
        HCE_MED_DESTRUCTOR();

        for(auto& hdl : handlers_) { 
            HCE_MED_METHOD_BODY("~cleanup",detail::utility::callable_to_string(hdl));
            hdl(t_); 
        } 
    }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "cleanup"; }

    /// install handler taking no arguments
    inline void install(thunk th) { 
        handlers_.push_back(adaptor{ std::move(th) });
        HCE_MED_METHOD_ENTER("install",detail::utility::callable_to_string(handlers_.back()));
    }

    /// install handler taking T as an argument
    inline void install(handler h) { 
        handlers_.push_back(std::move(h)); 
        HCE_MED_METHOD_ENTER("install",detail::utility::callable_to_string(handlers_.back()));
    }

private:
    struct adaptor {
        inline void operator()(T) { t(); }
        thunk t;
    };

    T t_;
    std::deque<handler> handlers_; 
};

// Arbitrary word sized allocated memory. The unique address of this 
// memory is used as a unique value
struct id : public std::shared_ptr<bool>, public printable {
    template <typename... As>
    id(As&&... as) : std::shared_ptr<bool>(std::forward<As>(as)...) {
        HCE_TRACE_CONSTRUCTOR();
    }

    ~id() { HCE_TRACE_DESTRUCTOR(); }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "id"; }

    inline std::string content() const {
        std::stringstream ss;
        ss << "unique@" << get();
        return ss.str();
    }
};

}

namespace std {

/// std:: printable conversion
inline std::string to_string(const hce::printable& p) { return p.to_string(); }

}

/// :: ostream writing
inline std::ostream& operator<<(std::ostream& out, const hce::printable& p) {
    out << p.to_string();
    return out;
}

inline std::ostream& operator<<(std::ostream& out, const hce::printable* p) {
    if(p) { out << *p; } 
    else { out << "hce::printable@nullptr"; }
    return out;
}

/// :: ostream writing for generic coroutine_handle
template <typename PROMISE>
std::ostream& operator<<(std::ostream& out, const std::coroutine_handle<PROMISE>& h) {
    out << "std::coroutine_handle@" << h.address();
    return out;
}

#endif
