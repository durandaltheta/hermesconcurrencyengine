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

#include "loguru.hpp"

//------------------------------------------------------------------------------
// method macros callable by inheritors of hce::printable

#ifndef HCE_LOGGING_MACROS
#define HCE_LOGGING_MACROS

/**
 User compile time macro determining compiled log code. Keeping this low ensures 
 that not only will code beneath the specified log level not print, the logging 
 code won't even be compiled because the statements will resolve to an empty 
 statement.

 What's interesting about this is that by setting the HCELOGLIMIT to 0, maximum 
 library performance will be achieved no matter how high the library's default 
 loglevel is compiled with. 

 This should be taken to heart, a lot of the slightly odd design of the the 
 logging mechanics is to enforce "no compile unless required" behavior, along 
 with standardizing output to a high degree. This enables consistent printing of 
 identifying information, such as object addresses and optional object internal 
 state for debugging purposes.

 The reason most of these functions concatenate arguments rather than pass them 
 to a formatting string is because it allows the log functions to ingest 
 arguments dynamically into an internal stream and therefore be used in 
 parameter pack template code which can have an undefined number of arguments.

 The `CONSTRUCTOR`, `DESTRUCTOR`, and `METHOD` logging macros can *only* be 
 called by implementors of `hce::printable`.  However, `FUNCTION` and `LOG` 
 macros can be called anywhere.

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
#define HCELOGLIMIT 0
#endif 

// HCELOGLIMIT min value is 0
#if HCELOGLIMIT < 0 
#define HCELOGLIMIT 0
#endif

// HCELOGLIMIT max value is 9
#if HCELOGLIMIT > 9
#define HCELOGLIMIT 9
#endif

/* 
 The following log macros are always compiled and printed when called
 */

#define HCE_FATAL_CONSTRUCTOR(...) log_constructor__(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_DESTRUCTOR() log_destructor__(loguru::Verbosity_FATAL)
#define HCE_FATAL_METHOD_ENTER(...) log_method_enter__(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_METHOD_BODY(...) log_method_body__(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_ENTER(...) printable::log_function_enter__(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_FUNCTION_BODY(...) printable::log_function_body__(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)
#define HCE_FATAL_LOG(...) VLOG_F(loguru::Verbosity_FATAL __VA_OPT__(,) __VA_ARGS__)


#define HCE_ERROR_CONSTRUCTOR(...) log_constructor__(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_DESTRUCTOR() log_destructor__(loguru::Verbosity_ERROR)
#define HCE_ERROR_METHOD_ENTER(...) log_method_enter__(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_METHOD_BODY(...) log_method_body__(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_ENTER(...) printable::log_function_enter__(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_FUNCTION_BODY(...) printable::log_function_body__(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)
#define HCE_ERROR_LOG(...) VLOG_F(loguru::Verbosity_ERROR __VA_OPT__(,) __VA_ARGS__)


#define HCE_WARNING_CONSTRUCTOR(...) log_constructor__(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_DESTRUCTOR() log_destructor__(loguru::Verbosity_WARNING)
#define HCE_WARNING_METHOD_ENTER(...) log_method_enter__(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_METHOD_BODY(...) log_method_body__(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_ENTER(...) printable::log_function_enter__(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_FUNCTION_BODY(...) printable::log_function_body__(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)
#define HCE_WARNING_LOG(...) VLOG_F(loguru::Verbosity_WARNING __VA_OPT__(,) __VA_ARGS__)


#define HCE_INFO_CONSTRUCTOR(...) log_constructor__(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_DESTRUCTOR() log_destructor__(loguru::Verbosity_INFO)
#define HCE_INFO_METHOD_ENTER(...) log_method_enter__(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_METHOD_BODY(...) log_method_body__(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_ENTER(...) printable::log_function_enter__(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_FUNCTION_BODY(...) printable::log_function_body__(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)
#define HCE_INFO_LOG(...) VLOG_F(loguru::Verbosity_INFO __VA_OPT__(,) __VA_ARGS__)


// high criticality lifecycle
#if HCELOGLIMIT >= 1
#define HCE_HIGH_CONSTRUCTOR(...) log_constructor__(1 __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_DESTRUCTOR() log_destructor__(1)
#else
#define HCE_HIGH_CONSTRUCTOR(...) (void)0
#define HCE_HIGH_DESTRUCTOR() (void)0
#endif 

// high criticality functions and methods
#if HCELOGLIMIT >= 2
#define HCE_HIGH_METHOD_ENTER(...) log_method_enter__(2 __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_METHOD_BODY(...) log_method_body__(2 __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_ENTER(...) printable::log_function_enter__(2 __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_FUNCTION_BODY(...) printable::log_function_body__(2 __VA_OPT__(,) __VA_ARGS__)
#define HCE_HIGH_LOG(...) VLOG_F(2 __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_HIGH_METHOD_ENTER(...) (void)0
#define HCE_HIGH_METHOD_BODY(...) (void)0
#define HCE_HIGH_FUNCTION_ENTER(...) (void)0
#define HCE_HIGH_FUNCTION_BODY(...) (void)0
#define HCE_HIGH_LOG(...) (void)0
#endif

// medium criticality lifecycle
#if HCELOGLIMIT >= 3
#define HCE_MED_CONSTRUCTOR(...) log_constructor__(3 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_DESTRUCTOR() log_destructor__(3)
#else
#define HCE_MED_CONSTRUCTOR(...) (void)0
#define HCE_MED_DESTRUCTOR() (void)0
#endif 

// medium criticality functions and methods
#if HCELOGLIMIT >= 4
#define HCE_MED_METHOD_ENTER(...) log_method_enter__(4 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_METHOD_BODY(...) log_method_body__(4 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_ENTER(...) printable::log_function_enter__(4 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_FUNCTION_BODY(...) printable::log_function_body__(4 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MED_LOG(...) VLOG_F(4 __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MED_METHOD_ENTER(...) (void)0
#define HCE_MED_METHOD_BODY(...) (void)0
#define HCE_MED_FUNCTION_ENTER(...) (void)0
#define HCE_MED_FUNCTION_BODY(...) (void)0
#define HCE_MED_LOG(...) (void)0
#endif

// low criticality lifecycle
#if HCELOGLIMIT >= 5
#define HCE_LOW_CONSTRUCTOR(...) log_constructor__(5 __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_DESTRUCTOR() log_destructor__(5)
#else
#define HCE_LOW_CONSTRUCTOR(...) (void)0
#define HCE_LOW_DESTRUCTOR() (void)0
#endif 

// low criticality functions and methods
#if HCELOGLIMIT >= 6
#define HCE_LOW_METHOD_ENTER(...) log_method_enter__(6 __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_METHOD_BODY(...) log_method_body__(6 __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_ENTER(...) printable::log_function_enter__(6 __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_FUNCTION_BODY(...) printable::log_function_body__(6 __VA_OPT__(,) __VA_ARGS__)
#define HCE_LOW_LOG(...) VLOG_F(6 __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_LOW_METHOD_ENTER(...) (void)0
#define HCE_LOW_METHOD_BODY(...) (void)0
#define HCE_LOW_FUNCTION_ENTER(...) (void)0
#define HCE_LOW_FUNCTION_BODY(...) (void)0
#define HCE_LOW_LOG(...) (void)0
#endif

// minimal criticality lifecycle
#if HCELOGLIMIT >= 7
#define HCE_MIN_CONSTRUCTOR(...) log_constructor__(7 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_DESTRUCTOR() log_destructor__(7)
#else
#define HCE_MIN_CONSTRUCTOR(...) (void)0
#define HCE_MIN_DESTRUCTOR() (void)0
#endif 

// minimal criticality functions and methods
#if HCELOGLIMIT >= 8
#define HCE_MIN_METHOD_ENTER(...) log_method_enter__(8 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_METHOD_BODY(...) log_method_body__(8 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_ENTER(...) printable::log_function_enter__(8 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_FUNCTION_BODY(...) printable::log_function_body__(8 __VA_OPT__(,) __VA_ARGS__)
#define HCE_MIN_LOG(...) VLOG_F(8 __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_MIN_METHOD_ENTER(...) (void)0
#define HCE_MIN_METHOD_BODY(...) (void)0
#define HCE_MIN_FUNCTION_ENTER(...) (void)0
#define HCE_MIN_FUNCTION_BODY(...) (void)0
#define HCE_MIN_LOG(...) (void)0 
#endif

// trace methods are of such low importance, you only want to print them when
// trying to actively debug code that stepping through with a debugger would be 
// painful or otherwise not useful
#if HCELOGLIMIT >= 9
#define HCE_TRACE_METHOD_ENTER(...) log_method_enter__(9 __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_METHOD_BODY(...) log_method_body__(9 __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_ENTER(...) printable::log_function_enter__(9 __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_FUNCTION_BODY(...) printable::log_function_body__(9 __VA_OPT__(,) __VA_ARGS__)
#define HCE_TRACE_LOG(...) VLOG_F(9 __VA_OPT__(,) __VA_ARGS__)
#else
#define HCE_TRACE_METHOD_ENTER(...) (void)0
#define HCE_TRACE_METHOD_BODY(...) (void)0
#define HCE_TRACE_FUNCTION_ENTER(...) (void)0
#define HCE_TRACE_FUNCTION_BODY(...) (void)0
#define HCE_TRACE_LOG(...) (void)0
#endif

#endif

namespace hce {
namespace detail {
namespace coroutine {

}

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
    virtual inline std::string to_string() const final { 
        std::stringstream ss;
        ss << this->nspace()
           << "::"
           << this->name()
           << "@" 
           << (void*)this;


        std::string c = this->content();

        if(!(c.empty())) { 
            ss << "["
               << c
               << "]";
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
    void log_constructor__(int verbosity, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_args_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        this->name(), 
                        ss.str().c_str());
        }
    }

    inline void log_destructor__(int verbosity) {
        if(verbosity <= printable::thread_log_level()) {
            std::string self(*this);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
                        "%s::~%s()", 
                        self.c_str(), 
                        this->name());
        }
    }

    template <typename... As>
    void log_method_enter__(int verbosity, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_args_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
                        "%s::%s(%s)", 
                        self.c_str(), 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    void log_method_body__(int verbosity, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);
            std::string self(*this);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
                        "%s::%s():%s", 
                        self.c_str(), 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    static void log_function_enter__(int verbosity, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
                        "%s(%s)", 
                        method_name, 
                        ss.str().c_str());
        }
    }

    template <typename... As>
    static void log_function_body__(int verbosity, const char* method_name, As&&... as) {
        if(verbosity <= printable::thread_log_level()) {
            std::stringstream ss;
            printable::ingest_(ss, std::forward<As>(as)...);

            loguru::log(verbosity, 
                        __FILE__, 
                        __LINE__, 
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
    static void ingest_item_(std::true_type, std::stringstream& ss, A&& container) {
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
    static void ingest_item_(std::false_type, std::stringstream& ss, A&& a) {
        ss << std::forward<A>(a);
    }

    static inline void ingest_continue_(std::stringstream& ss) { }

    template <typename A, typename... As>
    static void ingest_continue_(std::stringstream& ss, A&& a, As&&... as) {
        ss << ", "; 
        ingest_item_(
            detail::is_container<typename std::decay<A>::type>(),
            ss,
            std::forward<A>(a));
        ingest_continue_(ss, std::forward<As>(as)...);
    }
    
    static inline void ingest_args_(std::stringstream& ss) { }

    // for ingesting a list of arguments
    template <typename A, typename... As>
    static void ingest_args_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(
            detail::is_container<typename std::decay<A>::type>(),
            ss,
            std::forward<A>(a));
        ingest_continue_(ss, std::forward<As>(as)...);
    }
    
    static inline void ingest_(std::stringstream& ss) { }

    // for ingesting arbitrary data
    template <typename A, typename... As>
    static void ingest_(std::stringstream& ss, A&& a, As&&... as) {
        ingest_item_(
            detail::is_container<typename std::decay<A>::type>(),
            ss,
            std::forward<A>(a));
        ingest_(ss, std::forward<As>(as)...);
    }
};

namespace detail {
/// convenience std::function->std::string conversion
template <typename Callable>
inline std::string callable_to_string(Callable& f) {
    std::stringstream ss;
    ss << "function@"
       << (void*)&f;
    return ss.str();
}

/// less efficient double conversion
template <typename R, typename... As>
inline std::string to_string(std::function<R(As...)> f) {
    return to_string(f);
}

}

/**
 @brief call handlers on object destructor

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
    ~cleanup() { 
        HCE_MED_DESTRUCTOR();

        for(auto& hdl : handlers_) { 
            HCE_MED_METHOD_BODY(4,"~cleanup",detail::to_string(hdl));
            hdl(t_); 
        } 
    }

    inline const char* nspace() const { return "hce"; }
    inline const char* name() const { return "cleanup"; }

    /// install handler taking no arguments
    inline void install(thunk th) { 
        handlers_.push_back(adaptor{ std::move(th) });
        HCE_MED_METHOD_ENTER(4,"install",detail::to_string(handlers_.back()));
    }

    /// install handler taking T as an argument
    inline void install(handler h) { 
        handlers_.push_back(std::move(h)); 
        HCE_MED_METHOD_ENTER(4,"install",detail::to_string(handlers_.back()));
    }

private:
    struct adaptor {
        inline void operator()(T) { t(); }
        thunk t;
    };

    T t_;
    std::deque<handler> handlers_; 
};

}

namespace std {

/// std:: printable conversion
inline std::string to_string(const hce::printable& p) { 
    HCE_TRACE_FUNCTION_ENTER("std::to_string","const hce::printable&");
    HCE_TRACE_FUNCTION_BODY("std::to_string",p);
    return p.to_string(); 
}

}

/// :: ostream writing
inline std::ostream& operator<<(std::ostream& out, const hce::printable& p) {
    HCE_TRACE_FUNCTION_ENTER("std::ostream& std::operator<<","std::ostream&","const hce::printable& p)",(void*)out,p);
    HCE_TRACE_FUNCTION_BODY("std::ostream& std::operator<<",(void*)out,p);
    out << p.to_string();
    return out;
}

/// :: ostream writing for generic coroutine_handle
inline std::ostream& operator<<(std::ostream& out, const std::coroutine_handle<>& h) {
    out << "std::coroutine_handle@" << h.address();
    return out;
}

#endif
