# Hermes Concurrency Engine
C++20 Stackless Coroutine Concurrency Engine 

## Rationale

## Building the Library
### Prerequisites 
- `c++20` compiler toolchain (eg, `gcc`, `clang`, etc.) 
- `cmake`

Because usage of this library depends only on the `c++` language, in theory, it should be usable on different operating systems.

### Build 
Set your environment variables (IE, `HCELOGLEVEL`, etc.) and execute:
```
cmake .
make hce
```

`hce.a` can then be statically linked against by user software. The directories `inc/` and `loguru/` will need to be added to user software include directories.

## Documentation
### Prerequisites 
- `doxygen`
- `graphviz`

### Generate
`doxygen` documentation can be generated locally for this library. Install `doxygen` and `graphviz` packages then run `doxygen`:
```
cd /path/to/your/mce/repo/checkout
doxygen
```
The generated `doc/` directory should contain both `html` and `rtf` generated documentation. For `html`, open `index.html` by a browser to view the documentation. 

## Building and Running Unit Tests
### Prerequisites
- `python3`
- `cmake`
- `gcc` compiler toolchain (if not not disabled)
- `clang` compiler toolchain (if not not disabled)
- custom compiler toolchain (if specified)
- `valgrind` (specifically `memcheck`)
- `roff` 
- `tex`

### Build and Run
Execute all tests with:
```
./script/validate
```

Print test help with:
```
./script/validate --help
```

The `validate` command is a `python3` executable script. It's goal is to provide a consistent unit test validation algorithm. It builds and runs the library in many different configurations:
- the specified `c` and `c++` compiler
- produce `valgrind` memory leak reports 
- `hce` library compiled with various `HCELOGLEVEL`s
- `hce_ut` unit tests compiled with various `HCELOGLIMIT`s

The total times the the `hce_ut` unit tests are built and run is the combinatorial total of all enabled options. IE:
- the maximum `HCELOGLEVEL`: 11x
- the maximum `HCELOGLIMIT`: 10x
- valgrind `memcheck`: 1

Then the unit tests will be run "111" times.

The goal of all this is to introduce timing differences and general processing jitter, to increase confidence that the feature logic of the project is correct. A successful default `validate` run should produce high confidence of this software's correctness for the runtime hardware with the specified compiler.

### Compiler options
The `gcc`/`g++`/`clang`/`clang++` executables are provided to `script/validate` by searching the `PATH` (similar to how `linux` `which my-program` will return the path to the first found `my-program`). 

`gcc` compilation and testing can be disabled with `-ng`/`--no-gcc`. Likewise, `clang` compilation can be disabled with `-nc`/`--no-clang`.

`-ccc`/`--custom-c-compiler` and `-ccxx`/`-custom-cxx-compiler` can be provided if a specific compiler is needed (example: a cross compiler). 

Additionally, if cross compiling, the user will need to specify `-otc`/`--override-test-command` and `-omc`/`--override-memcheck-command` so that the compiled unit tests can run in the necessary virtual environment or on connected target hardware.

## Documentation

## Scheduling

## Communication 

## Blocking Calls

## Integration with Existing Code

## Debug Logging
This project utilizes the [emilk/loguru](https://github.com/emilk/loguru) project for debug logging, writing to stdout and stderr by default. Logging features are provided for primarily for the development of this library, but work has been done to make it fairly robust and may be applicable for user development and production code debugging purposes.

By default, no debug logging is enabled or compiled.

The `loguru` source code is included in this code repository and does not need to be provided externally.

Additionally, all user accessible objects provided by this library are printable (writable to an `std::ostream` and convertable to an `std::string` with `std::to_string()`), with or without `loguru` logging enabled. 

### Library Compile Time Definitions 
#### HCELOGLEVEL
`HCELOGLEVEL` is utilized for specifying the *default* loglevel for all threads. Any log with an associated visibility value of higher than `HCELOGLEVEL` will not be printed (unless the loglevel for a particular thread is modified, see [Thread Local Logging](#thread-local-logging) below).

`HCELOGLIMIT` is a compiler define that is used when building *this library* (*not* user code!). As such, `HCELOGLEVEL` can be defined for the library as environment variables when building or set in their associated `cmake` variables in the toplevel `CMakeLists.txt`. However, these values may have to be additionally passed in as explicit compiler defines to user source compilation.

The default value for `HCELOGLEVEL` is `-1`, generally limiting logging to errors or critical messages.

`HCELOGLEVEL` can be set lower than `HCELOGLIMIT` to prevent logging (though some extra runtime processing may occur if the `HCELOGLIMIT` is set higher than `HCELOGLEVEL` even if nothing is printed).

If debugging becomes necessary, rebuild *this library* with `HCELOGLEVEL` defined at the appropriate level as an environment variable or set within the toplevel `CMakeLists.txt`.

#### HCECUSTOMLOGINIT
If `HCECUSTOMLOGINIT` is defined at library compile time, then the library's default `loguru` initialization code will not be defined. 

If `HCECUSTOMLOGINIT` is defined the user is responsible for externally linking the following function definition: 
```
extern void hce_log_initialize(int hceloglevel);
```

The argument `hceloglevel` will be set to the value of the `HCELOGLEVEL` definition compiled with this library.

It is expected that the user implementation of the above function will need to `#include "loguru.hpp"` somehow, link against the both compiled `loguru` and `hce` libraries and call `loguru::init()` with any desired arguments.

### User Code Compile Time Definitions 
#### HCELOGLIMIT
Objects in this library integrate compile-time controlled logging with compiler define `HCELOGLIMIT`, which defaults to `0`. `HCELOGLIMIT` is only applicable when compiling *user code*, *not* compiling this library in isolation. Therefore, to control `HCELOGLIMIT`, pass it as a compiler define to the toolchain building your source code (IE, pass `-DHCELOGLIMIT=yourloglevel` to `g++`/`gcc`/`clang` somehow).

Logging visibility is *hard* limited by `HCELOGLIMIT`. That is, if `HCELOGLIMIT` is set too low for something to print, it will *not* be possible for that statement to print (the relevant `C++` logging macro will be defined as an empty statement). Therefore, a simple way to solve any logging bugs... is to set this `HCELOGLIMIT` to `0` :D. 

Setting `HCELOGLIMIT` value correctly may be important for performance reasons, as no objects are constructed nor log level tests are ever executed for logging that is above the limit. It is generally recommended to keep this value low in production code (a level of <= to `3` may be tolerable in many production cases), though mileage may vary.

If additional debugging becomes necessary, rebuild *user code* with `HCELOGLIMIT` defined at the required level. 

### Runtime Log Control
#### Thread Local Logging 
Assuming the `HCELOGLIMIT` is specified high enough when compiled against user code, it is actually possible to modify the log level for a specific thread at runtime. This allows for user code to specify which threads should print more verbosely for debugging. Each thread's default log level is set to the compiled `HCELOGLEVEL` (unless the user has specified `HCECUSTOMLOGINIT` and implemented the default `loguru` loglevel differently).

The following methods are provided for this:
`static int hce::printable::thread_log_level()`: return the log level for the calling thread
`static void hce::printable::thread_log_level(int)`: set the log level on the calling thread 
`int hce::scheduler::log_level()`: schedule a coroutine to get the log level of the scheduler's thread
`void hce::scheduler::log_level(int)`: schedule a coroutine to set the log level of the scheduler's thread

An example debuggable non-production design might:
- Compile user application with `HCELOGLIMIT` to the necessary level when compiling user code (for example, `6` to enable up to "low criticality" logging)
- Compile `HCELOGLEVEL` to some low default level when compiling this library (maybe `2`, to only print "high" criticality logs, or `4` to print "medium" criticality logs) 
- On threads where debugging becomes necessary, set the thread local logging level (calling `hce::printable::thread_log_level(6)` or `hce::scheduler::log_level(6)`) to make that thread print "low" criticality logs)

### loglevel definitions
`HCELOGLIMIT`, `HCELOGLEVEL` and individual threads with `hce::printable::thread_log_level(int)`/`hce::scheduler::log_level(int)` can be set to the following values:
```
0: the default framework level, only errors and critical information will be logged
1: construction/destruction of high criticality objects
2: method calls high criticality objects and functions
3: construction/destruction of medium criticality objects
4: method calls of medium criticality objects and functions
5: construction/destruction of low criticality objects
6: method calls of low criticality objects and functions 
7: construction/destruction of minimal criticality objects
8: method calls of minimal criticality objects and functions
9: trace criticality logging enabled
```

All logs for an object of a given criticality are not guaranteed to print at the specified loglevels, they should be taken as a general debugging guide. View the source code for what *exact* log macros are called in specific functions.

#### High criticality
- `hce::scheduler`
- `hce::scheduler::lifecycle`
- `hce::scheduler::lifecycle::manager`
- `hce::scheduler::config`
- `hce::scheduler::config::handlers`
- `hce::schedule()`
- `hce::join()`
- `hce::scope()`
- `hce::sleep()`

#### Medium criticality
- `hce::coroutine`
- `hce::timer`

#### Low criticality
- `hce::cleanup`
- `hce::yield`
- `hce::awaitable::interface` (the implementation of an `hce::awaitable`)
- `hce::channel::interface` (the implementation of an `hce::channel`)

#### Minimal criticality
- `hce::circular_buffer`
- `hce::spinlock`
- `hce::lockfree` 
- `hce::mutex`
- `hce::unique_lock<Lock>`
- `hce::condition_variable`
- `hce::condition_variable_any` 

#### Trace criticality
All remaining API (that has does not log with any lower loglevel) will print with at this level. Output may be *extremely* verbose. It is expected this is only necessary in 2 situations:
- debugging of a very specific issue 
- introducing maximum processing jitter during calls to `validate`
