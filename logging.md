# Debug Logging
This project utilizes the [emilk/loguru](https://github.com/emilk/loguru) project for debug logging, writing to stdout and stderr by default. Logging features are provided primarily for the development of this library, but work has been done to make it fairly robust and may be applicable for user development and production code debugging purposes.

The `loguru` source code is included in this code repository and does not need to be downloaded. `loguru` is distributed with the permissive "unlicense" license.

Most user accessible objects provided by this library are printable (writable to an `std::ostream` with `<<` and convertable to an `std::string` with `std::to_string()`). That is, any object which implements the `hce::printable` interface can be printed to a stream. 

Logging occurs on standard output.

## loglevel definitions
`HCELOGLIMIT`, `HCELOGLEVEL` and individual threads with `hce::printable::thread_log_level(int)`/`hce::scheduler::log_level(int)` can be set to the following values:
```
-3: FATAL will be logged
-2: ERROR will be logged
-1: WARNING will be logged (the framework default loglevel)
0: INFO will be logged
1: high criticality construction/destruction of objects
2: high criticality method and function calls
3: medium criticality construction/destruction of objects
4: medium criticality method and function calls
5: low criticality construction/destruction of objects
6: low criticality method and function calls 
7: minimal criticality construction/destruction of objects
8: minimal criticality method and function calls
9: trace criticality logging enabled
```

All logs for an object of a given criticality are not guaranteed to print at the specified loglevels, they should be taken as a general debugging guide. View the source code for what *exact* log macros are called in specific functions.

### High criticality
- `hce::scheduler`
- `hce::scheduler::lifecycle`
- `hce::scheduler::lifecycle::manager`
- `hce::scheduler::config`
- `hce::scheduler::config::handlers`
- `hce::scheduler::install`
- `hce::schedule()` and `hce::scheduler::schedule()`
- `hce::join()` and `hce::scheduler::join()`
- `hce::scope()` and `hce::scheduler::scope()`

This category contains framework management and scheduling utilities.

### Medium criticality
- `hce::coroutine`
- `hce::timer`
- `hce::start()` and `hce::scheduler::start()`
- `hce::running()` and `hce::scheduler::running()`
- `hce::sleep()` and `hce::scheduler::sleep()`
- `hce::cancel()` and `hce::scheduler::cancel()`
- `hce::block()` and `hce::scheduler::block()`

This category includes individual coroutine lifecycle and execution.

### Low criticality 
- `hce::yield`
- `hce::awaitable::interface` (the implementation of an `hce::awaitable`)
- `hce::channel::interface` (the implementation of an `hce::channel`)
- `hce::block()`

This category includes "communication" operations.

### Minimal criticality
- `hce::circular_buffer`
- `hce::spinlock`
- `hce::barrier`
- `hce::lockfree` 
- `hce::mutex`
- `hce::unique_lock<Lock>`
- `hce::condition_variable`
- `hce::condition_variable_any` 

This category contains synchronization objects implemented by this library.

### Trace criticality
All remaining API log statements (that has does not log with any lower loglevel) will print at this level. Output may be *extremely* verbose. It is expected this is only necessary in a few situations:
- debugging of specific issues, potentially in combination with thread local log level management
- introducing maximum processing jitter during calls to `script/validate` to increase the likelyhood of detecting race conditions in the unit tests

## Library Compile Time Definitions 
### HCELOGLEVEL
`HCELOGLEVEL` is utilized for specifying the *default* loglevel for all threads. Any log with an associated visibility value of higher than `HCELOGLEVEL` will not be printed (unless the loglevel for a particular thread is modified, see [Thread Local Logging](#thread-local-logging) below).

The project-wide default loglevel can be retrieved at runtime with:
```
int hce::printable::default_log_level();
```

If the user used the default log initialization, this returned value will be equal to `HCELOGLEVEL`.


The calling thread's current loglevel can be retrieved at runtime with:
```
int hce::printable::thread_log_level();
```

The calling thread's current loglevel can be *set* at runtime with:
```
void hce::printable::thread_log_level(int level);
```
`HCELOGLEVEL` is a compiler define that is used when building *this library* (*not* user code!). As such, `HCELOGLEVEL` can be defined for the library as environment variables when building or set in their associated `cmake` variables in the toplevel `CMakeLists.txt`. However, these values may have to be additionally passed in as explicit compiler defines to user source compilation.

The default value for `HCELOGLEVEL` is `-1`, generally limiting logging to errors or critical messages. The valid range for `HCELOGLEVEL` is `-9` to `9`.

`HCELOGLEVEL` can be set lower than `HCELOGLIMIT` to prevent logging (though some extra runtime processing may occur if the `HCELOGLIMIT` is set higher than `HCELOGLEVEL` even if nothing is printed).

If debugging becomes necessary, rebuild *this library* (not user code) with `HCELOGLEVEL` defined at the appropriate level as an environment variable or set within the toplevel `CMakeLists.txt`.

### HCECUSTOMCONFIG
If `HCECUSTOMCONFIG` is defined at library compile time, then the library's default `loguru` initialization code will not be defined. 

If `HCECUSTOMCONFIG` is defined the user is responsible for externally linking the following function definition: 
```
namespace hce {
namespace config {
extern void initialize_log();
}
}
```

It is expected that the user implementation of the above function will need to `#include "loguru.hpp"` somehow, link against the both compiled `loguru` and `hce` libraries and call `loguru::init()` with any desired arguments.

## User Code Compile Time Definitions 
### HCELOGLIMIT
Objects in this library integrate compile-time controlled logging with compiler define `HCELOGLIMIT`. `HCELOGLIMIT` is only applicable when compiling *user code*, *not* compiling this library in isolation. Therefore, to control `HCELOGLIMIT`, pass it as a compiler define to the toolchain building your source code (IE, pass `-DHCELOGLIMIT=yourloglevel` to `g++`/`gcc`/`clang` somehow).

The default value for `HCELOGLIMIT` is `-1`, generally limiting logging to errors or critical messages. The valid range for `HCELOGLIMIT` is `-9` to `9`.

Logging visibility is *hard* limited by `HCELOGLIMIT`. That is, if `HCELOGLIMIT` is set too low for something to print, it will *not* be possible for that statement to print (the relevant `C++` logging macro will be defined as an empty statement). Therefore, a simple way to solve any logging bugs or logging performance problems... is to set this `HCELOGLIMIT` to `-9` :-D (disables all logging). Of course, setting the log limit too low may hide critical errors!

Setting `HCELOGLIMIT` value correctly may be important for performance reasons, as no objects are constructed nor log level tests are ever executed for logging that is above the limit. It is generally recommended to keep this value low in production code (a level of <= to `3` may be tolerable in many production cases), though mileage may vary.

If additional debugging becomes necessary, rebuild *user code* with `HCELOGLIMIT` defined at the required level. 

## Runtime Log Control
### Thread Local Logging 
Assuming the `HCELOGLIMIT` is specified high enough when compiled against user code, it is actually possible to modify the log level for a specific thread at runtime. This allows for user code to specify which threads should print more verbosely for debugging. Each thread's default log level is set to the compiled `HCELOGLEVEL` (unless the user has specified `HCECUSTOMLOGINIT` and implemented the default `loguru` loglevel differently).

The following methods are provided for this:
`static int hce::printable::thread_log_level()`: return the log level for the calling thread
`static void hce::printable::thread_log_level(int)`: set the log level on the calling thread
`hce::scheduler` object's loglevel can be configured during calls to `hce::scheduler::make()` by specifying the log level in the `hce::scheduler::config` argument.

An example debuggable non-production design might:
- Compile user application with `HCELOGLIMIT` to the necessary level when compiling user code (for example, `6` to enable up to "low criticality" logging)
- Compile `HCELOGLEVEL` to some low default level when compiling this library (maybe `2`, to only print "high" criticality logs, or `4` to print "medium" criticality logs) 
- On threads where debugging becomes necessary, set the thread local logging level (calling `hce::printable::thread_log_level(6)` or `hce::scheduler::log_level(6)`) to make that thread print "low" criticality logs)

## Developer Logging Notes
Because of the unusual nature of logging in this project (the mix of compile time configuration with `HCELOGLIMIT` and the runtime configuration with `HCELOGLEVEL`) the basic pattern of writing project code with logging is any time logging is required for `HCE` project code, it shall only be written in a header (IE, inline code).

Where pre-compiled code is necessary (a la, `.cpp` files) no logging code is generally included unless there is a very good reason and the code is not in a critical section (called only once or infrequently, not in very time sensitive processing loop). 
