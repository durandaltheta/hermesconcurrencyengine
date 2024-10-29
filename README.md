# Hermes Concurrency Engine
C++20 Stackless Coroutine Concurrency Engine

## Rationale
`c++20` coroutines are an extremely powerful and efficient mechanism for writing concurrent code. They are also one of the most difficult parts of the language to use correctly.

This framework is designed to make using and scheduling `c++20` coroutines easy and to make integration into existing codebases simple.

### Example
A simple example where a coroutine is constructed, scheduled, and communicated with asynchronously. 

[example_001 source](ex/src/example_001.cpp)
```
#include <iostream>
#include <hce.hpp>

hce::co<void> my_coroutine(hce::channel<int> ch) {
    int i;

    while(co_await ch.recv(i)) {
        std::cout << "received: " << i << std::endl;
    }
}

int main() {
    auto ch = hce::channel<int>::make();
    auto awt = hce::schedule(my_coroutine(ch));
    ch.send(1);
    ch.send(2);
    ch.send(3);
    ch.close();
    // awt destructs and blocks until my_coroutine returns
    return 0;
}
```

Usage and output (after building code examples with `make hce_ex`):
```
$ ./ex/example_001
received: 1
received: 2
received: 3
$
```

## License 
This project utilizes the MIT license.

## Building the Library
### Prerequisites 
- `c++20` compiler toolchain (eg, `gcc`, `clang`, etc.) 
- `cmake`

Because usage of this library depends only on the `c++` language, in theory, it should be usable on different operating systems that support a `c++20` toolchain. Actual testing on the target platform with `script/validate` is recommended to verify behavior in a particular environment.

### Build 
A default build from within this repository can be built with:
```
cmake .
make hce
```

`libhce.a` can then be statically linked against by user software. The project directories `inc/` and `loguru/` will need to be added somehow as user software include directories.

### Default Configuration Defines
This project has various `cmake` `-D` defines which determine compiled library runtime behavior:
- `HCEGLOBALCOROUTINEPOOLLIMIT`; Count of coroutines the global `hce::scheduler` will cache reusable resources for, lowering the cost of repeated allocation/deallocation
- `HCEGLOBALREUSEBLOCKCOUNT`: Count of persistent threads for running `hce::block()` calls managed by the process-wide default global `hce::scheduler`.
- `HCETHREADPOOLCOROUTINEPOOLLIMIT`; Count of coroutines that `hce::threadpool` `hce::scheduler`s will cache reusable resources for, lowering the cost of repeated allocation/deallocation
- `HCETHREADPOOLREUSEBLOCKCOUNT`: Count of persistent threads for running `hce::block()` calls for *each* `hce::schduler` managed by the process-wide `hce::threadpool`
- `HCETHREADPOOLSCHEDULERCOUNT`: Count of process-wide `hce::threadpool` worker threads running coroutine `hce::scheduler`s
- `HCELOGLEVEL`: The default `hce` loglevel of threads. See [logging documentation](logging.md)

Additonally, the following compiler define should be considered when building user code:
- `HCELOGLIMIT`: A user code compile time option which limits what log statements are actually compiled, see [logging documentation](logging.md)
- `HCEPOOLALLOCATORDEFAULTSIZELIMIT`: A user code compile option which sets the default cap for reusable allocated memory used by pool allocators in this library. With many operations, increasing this value can potentially increase throughput, with potentially more memory held at a time.
- `HCESCHEDULERPOOLALLOCATORDEFAULTSIZELIMIT`: A user code compile option for pool allocations in `hce::schedulers` specifically, primarily affecting allocation of coroutine scheduling resources. This can often be defaulted much higher than `HCEPOOLALLOCATORDEFAULTSIZELIMIT`.

### Custom Configuration
Various default `hce::config::` namespace `extern` functions are implemented in [config.cpp](src/config.cpp). If this project has been configured with `cmake -DHCECUSTOMCONFIG` then this file will not be compiled with `libhce`. This allows the user to provide and link their own implementations at runtime. This is especially useful if the user wants to utilize the various callbacks and exception handling available in `hce::scheduler::config`.

If implementing custom `hce::config::` functionality, be aware that project configuration defines are used by the project default implementations (with the exception of `HCELOGLIMIT`). Therefore the user is responsible for how those values are utilized when writing their own implementations.

## Documentation
### Prerequisites 
- `doxygen`
- `graphviz`

### Generate
`doxygen` documentation can be generated locally for this library. Install `doxygen` and `graphviz` packages then run `doxygen` in the project root:
```
doxygen
```
The generated `doc/` directory should contain both `html` and `rtf` generated documentation. For `html`, open `index.html` by a browser to view the documentation. The generated documentation will contain a *lot* more information than this readme, and is recommended for non-trivial usecases.

## Unit Tests
### Prerequisites
- `python3`
- `cmake`
- `c++20` compiler toolchain
- `valgrind` (specifically `memcheck`)
- `roff` 
- `tex`

### Build and Run
Execute all tests with `script/validate` from this repository's root directory (example with `gcc`):
```
./script/validate -cc=/path/to/gcc -cxx=/path/to/g++
```

Print test help with:
```
./script/validate --help
```

The `validate` command is a `python3` executable script. Its design goal is to provide a consistent and thorough unit test validation algorithm. It builds and runs the library in many different configurations:
- with the specified `c++` compiler toolchain
- `hce` library compiled with various `HCELOGLEVEL`s
- `hce_ut` unit tests compiled with various `HCELOGLIMIT`s
- produce `valgrind` memory leak reports 

The total times the the `hce_ut` unit tests are built and run is the combinatorial total of all enabled options. IE:
- compiler settings (+1 test run)
- `-1` up to the maximum `HCELOGLEVEL` value of `9` (x10 test runs )
- `-1` up to the maximum `HCELOGLIMIT` value of `9` (x10 test runs )
- valgrind `memcheck` (+1 test run)

Thus the unit tests will be run 101 times.

The goal of this is to introduce timing differences to increase confidence that the feature logic of the project is correct. A successful default `validate` run should produce high confidence of this software's correctness for the runtime hardware with the specified compiler.

### Compiler options
Compilers are set with the `-cc`/`--c-compiler` and `-cxx`/`-cxx-compiler`. They can be any compiler, including cross compilers.

The argument specified `cc`/`cxx` executable paths are provided to `script/validate` by searching the `PATH` (similar to how `linux` `which my-program` will return the path to the first found `my-program`). 

Additionally, if cross compiling, the user will need to specify `-otc`/`--override-test-command` and `-omc`/`--override-memcheck-command` so that the compiled unit tests can run in the necessary virtual environment or on connected target hardware. It is up to the user to implement said scripts/commands in such a way that compiled unit tests get executed in the right environment.

## HCE Coroutines
`c++20` coroutines are specialized, lightweight "function-like" objects generated by the compiler. A `c++20` coroutine behaves as a function that is capable of suspending (pausing execution) at various points, only to resume execution at some future point. The underlying mechanics of how this is accomplished are complex to understand and generally unnecessary to use this library. 

Coroutines are valuable because they are orders of magnitude faster to context switch (that is, change which one is actively executing) than the operating system equivalent of a "system thread" (POSIX `pthread`s and/or `c++` `std::thread`s). This makes concurrent code which needs to handle multiple simultaneous tasks (that may or may not execute on the same CPU core) written with coroutines potentially *MUCH* faster and more resource efficient (`coroutine`s are very lightweight) than the same done with only system threads.

It is enough for using this framework that each `hce` compatible coroutine must:

1. The `hce` coroutine function must specify its return value is the object `hce::co<COROUTINE_RETURN_TYPE>`. `COROUTINE_RETURN_TYPE` is the type of the returned value by the `co_return` statement.

```
hce::co<int> my_coroutine_returning_int() {
    co_return 3;
}
```

2. utilize one or more of the following `c++20` `std::coroutine` keywords:
```
co_return // coroutine specific 'return' statement
co_await // used to safely block a coroutine on a returned awaitable object and get the result (if any) of the operation
``` 

NOTE: `awaitable` objects are returned from various functions in this framework with the type `hce::awt<AWAITABLE_RETURN_TYPE>`. They are used to suspend execution (block) coroutines until an operation completes, upon which the coroutine will resume execution. `AWAITABLE_RETURN_TYPE` is the type returned from the `co_await` statement:
```
// given this function
hce::awt<int> my_function_returning_an_awaitable();

// this coroutine co_awaits the result of function_returning_awt_int()
hce::co<int> my_coroutine() {
    int my_result = co_await my_function_returning_an_awaitable();
    co_return my_result;
}
```

An `hce::awt<T>` can be directly converted to type `T` (or go out of scope) in a non-coroutine (IE, regular code) to block and get the result of an awaitable operation:
```
// given this function
hce::awt<int> my_function_returning_an_awaitable();

// this non-coroutine awaits the result of function_returning_awt_int()
int my_regular_code() {
    int my_result = my_function_returning_an_awaitable();
    return my_result;
}
```

3. The coroutine function is exactly one stack frame in size, all `co_return`, `co_await`, and `co_yield` statements associated with a single coroutine happen in the topmost function of the coroutine.

Bad:
```
int inner_frame() {
    co_return 3; // BAD, inner is its own stack frame and is not a coroutine
}

hce::co<int> outer_frame() {
    inner_frame(); 
    // BAD co_return required at toplevel
}
```

Good:
```
hce::co<int> valid() {
    co_return 3; 
}
```

Good:
```
int inner_frame() {
    return 3; 
}

hce::co<int> outer_frame() {
    co_return inner_frame(); // valid, co_returning the result of some function
}
```

### Further Examples and Explanations
```
#include <string> 
#include <hce.hpp>

hce::co<void> void_return_coroutine() {
    co_return;
}

hce::co<int> int_return_coroutine() {
    co_return 5;
}

hce::co<std::string> string_return_coroutine_with_arguments(int arg1, std::string arg2) {
    co_return std::to_string(arg1) + arg2;
}
```

When an `hce` coroutine is first invoked, it does not immediately run. Instead it constructs the `hce::co<T>` return object. This object can be passed to a `hce` scheduling operation, such as `hce::schedule()` so that it will actually execute.

WARNING: `lambda` functions, as well as Functors (objects with implement the Call operator `()` implemented), can be coroutines. HOWEVER, there are very specific rules on what data is available to the coroutine function when it executes. The shorthand is: do not use `lambda` captures or Functor object members within a coroutine body, pass them in as function arguments. For Functors, ensure the coroutine is a `static` function. See [the en.cppreference.com coroutine documentation](https://en.cppreference.com/w/cpp/language/coroutines) for complicated details.

Generate `Doxygen` documentation to see more for `hce` coroutine creation.

## Scheduling
This library includes high level, and very efficient, scheduling operations:
```
hce::awt<CO_RETURN_TYPE> hce::schedule(co): Schedule and join with a coroutine, blocking and returning the coroutine's return value (IE, `hce::co<CO_RETURN_TYPE>`)
hce::awt<void> hce::sleep(... duration args...): Block a coroutine for a period of time
hce::awt<void> hce::start(hce::id&, ... duration args...): Block a coroutine for a period of time on the global scheduler, cancellable with assigned id 
bool hce::cancel(const hce::id&): Cancel running timer on the global scheduler early, return true if successful
hce::awt<RETURN_TYPE> hce::block(Callable, args...): Call a function which may block the calling thread and join the coroutine with the result, returning `Callable`'s return value.
```

A simple example program:
```
#include <iostream>
#include <hce.hpp> 

// c++20 coroutines valid for this framework return `hce::co<COROUTINE_RETURN_TYPE>`
hce::co<int> my_coroutine(int arg) {
    std::cout << "my_coroutine received " << arg << std::endl;
    co_return arg + 1; // return a value to main
}

int main() {
    int my_result = hce::schedule(my_coroutine(14));
    std::cout << "main joined with my_coroutine and received " << my_result << std::endl;
    return 0;
}
```

Example output:
```
$ ./a.out 
my_coroutine received 14
main joined with my_coroutine and received 15
$
```

Generate `Doxygen` documentation to see more for `hce::scheduler` creation, configuration and management. 

## Joining
All scheduled coroutines are joinable with the `hce::awt<T>` returned by `schedule()` type calls. `coroutine`s can `co_await` this object like normal to block until a coroutine completes:
```
#include <iostream>
#include <hce.hpp> 

hce::co<int> co1() {
    co_return 12;
}

hce::co<int> co2() {
    // block on sub-coroutine
    co_return co_await hce::schedule(co1());
}

int main() {
    // conversion of an hce::awt<T> blocks the thread in a non-coroutine
    int result = hce::schedule(co2());
    std::cout <<  "received " << result << std::endl;
    return 0;
}
```

Example output:
```
$ ./a.out 
received 12
$
```

Joining a returned `hce::awt<T>` can be delayed by storing the awaitable in a variable. This can be particularly useful in the `main()` or other code running outside of coroutines that doesn't need to block on the coroutine returning:
```
#include <iostream>
#include <hce.hpp> 

hce::co<void> root_coroutine() {
    // schedule more coroutines
    co_await hce::schedule(/*... other coroutines ...*/);
    // ... do things ...
    co_return;
}

int main() {
    auto root_awaitable = hce::schedule(root_coroutine());
    // ... do things ...
    return 0; // destruction of root_awaitable finally blocks main()
}
```

`coroutine`s launched by other coroutines that keep `hce::awt<T>` as a variable may have to have to `std::move()` the `hce::awt<T>` into a `co_await` statement:
```
#include <hce.hpp> 

hce::co<void> co1() {
    co_return;
}

hce::co<void> co2() {
    auto awt = hce::schedule(co1());
    // ... do other things ...
    co_await std::move(awt); // std::move() awt into the co_await statement
    co_return;
}

int main() {
    hce::schedule(co2());
    return 0;
}
```

### Scopes
Multiple coroutines can be joined simultaneously using an `hce::scope`. `hce::scope`s can be constructed with zero or more `hce::awt<T>`s (of potentially different types `T`!). Additional `hce::awt<T>`s can be added later using `hce::scope::add()`. All added `hce::awt`s can be awaited by awaiting the result of `hce::scope::join()`:
```
#include <iostream>
#include <string>
#include <hce.hpp> 

hce::co<void> co1() {
    co_return;
}

hce::co<std::string> co2() {
    co_return std:string("this string is ignored");
}

hce::co<void> synchronizing_co() {
    co_return co_await hce::scope(hce::schedule(co1()), hce::schedule(co2())).join();
}

int main() {
    // schedules synchronizing_co and blocks the main thread (when the returned 
    // hce::awt<void> destructs) until synchronizing_co returns
    hce::schedule(synchronizing_co());
    return 0;
}
```

If `hce::scope` destructs in a non-`coroutine` (IE, a standard thread) it will `join()` automatically:
```
#include <iostream>
#include <string>
#include <hce.hpp> 

hce::co<void> co1() {
    co_return;
}

hce::co<std::string> co2() {
    co_return std:string("this string is ignored");
}

hce::co<void> synchronizing_co() {
    co_return co_await hce::scope(hce::schedule(co1()), hce::schedule(co2())).join();
}

int main() {
    // automatically join()s on ~scope()
    hce::scope(hce::schedule(co1()), hce::schedule(co2()));
    return 0;
}
```

## Communication 
This library allows communication between coroutines, threads, and any combination there-in using `hce::channel<T>`s. 

`hce::channel<T>` is a specialized communication mechanism which works correctly for both `hce` coroutines (running in an `hce::scheduler`) and system threads. `T` is the type of data sent and received through channel. The default constructed `hce::channel<T>` with `hce::channel<T>::make()` is unbuffered (has no internal storage, data is transferred directly between pointer addresses), which is extremely lightweight and efficient for coroutine to coroutine communication, especially for coroutines running on the same `hce::scheduler` (the same system thread).

A simple example program:
```
#include <iostream>
#include <hce.hpp> 

hce::co<void> my_coroutine(hce::channel<int> in_ch, hce::channel<int> out_ch) {
    int i;
    
    // receive a value from main
    co_await in_ch.recv(i);
    std::cout << "my_coroutine received " << i << std::endl;
    
    // send a value to main
    co_await out_ch.send(i+1);
    co_return;
}

int main() {
    auto in_ch = hce::channel<int>::make();
    auto out_ch = hce::channel<int>::make();
    auto awt = hce::schedule(my_coroutine(in_ch, out_ch));

    // send a value to my_coroutine
    in_ch.send(16); // system threads do not call co_await 

    // receive a value from my_coroutine
    int my_result;
    out_ch.recv(my_result);

    std::cout << "main received " << my_result << std::endl;
    // awt destructs and blocks until my_coroutine returns
    return 0;
}
```

Example output:
```
$ ./a.out 
my_coroutine received 16
main joined with my_coroutine and received 17
$
```

`hce::channel`s with a buffered internal queue can also be constructed by passing a buffer size to `hce::channel<T>::make(unsigned int buffer_size)`. `hce::channel<T>`s with a buffer will not block on `send()` operations until the buffer becomes full. This can be used to optimize send operations from system threads (non-coroutines), where blocking the entire thread is a much more expensive operation.

Generate `Doxygen` documentation to see more, specifically for `hce::channel<T>` unbuffered/buffered channel construction and other API.

## Thread Blocking Calls
Arbitrary functions which may block a calling coroutine (either for a long or indefinite amount of time) are unsafe to use directly by a coroutine because they will block the calling thread. Doing this will pause the processing of coroutines and in the worst case cause system deadlock. 

Instead, those functions can be executed with a call to `hce::block()` to safely block the coroutine without blocking the `hce::scheduler` owned system thread:

A simple example program:
```
#include <iostream>
#include <chrono>
#include <hce.hpp> 

int my_blocking_function(int arg) {
    // sleep for 5 seconds blocking the calling thread
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return arg + 1;
}

hce::co<void> my_coroutine(int arg) {
    std::cout << "my_coroutine received " << arg << std::endl;
    // wrapping my_blocking_function in hce::block() allows other coroutines to run
    int my_result = co_await hce::block(my_blocking_function, arg);
    co_return my_result;
}

int main() {
    int my_result = hce::schedule(my_coroutine(3));
    std::cout << "main joined with my_coroutine and received " << my_result << std::endl;
    return 0;
}
```

Example output:
```
$ ./a.out 
my_coroutine received 3
main joined with my_coroutine and received 4
$
```

`hce::block()` launches and manages system threads as necessary to guarantee operations execute on isolated threads. It is possible to configure an `hce::scheduler` to keep more block worker threads in existence to lower the cost for frequent `hce::block()` operations (see `struct hce::scheduler::config` and `hce::config::global_scheduler_config()` in the `Doxygen` documentation). The count of reusable block workers on the global `scheduler` defaults to 1.

`hce::block()` attempts to be efficient about when it chooses to launch new threads. If it detects that a calling thread is already isolated (either the caller is not a coroutine executing in an `scheduler` OR the `block()` call is executing inside another `block()` call) then it will execute the blocking operation immediately, in-place. Otherwise a thread will be requested from the `scheduler`, which may be reused from an earlier `block()` call.

Generate `Doxygen` documentation to see more for `hce::block()` and `hce::scheduler` configuration options.

## Thread Non-Blocking Calls
Non-blocking calls can be utilized in coroutines by `co_await`ing `hce::yield<RETURN_TYPE>` objects to allow other coroutines to run. The `RETURN_TYPE` is the type returned by the `co_await` statement, and its `co_await`ed value is whatever `hce::yield` is constructed with.

The simplest form of `hce::yield` is `hce::yield<void>`, which returns no value when `co_await`ed. For example, a coroutine can yield execution between attempts to ensure other coroutines can run:
```
// return true on success, else false
bool some_operation_which_may_fail(int& result);

hce::co<int> non_blocking_coroutine() {
    int result = 0;

    while(!some_operation_which_may_fail(result)) { 
        co_await hce::yield<void>();
    }

    co_await hce::yield<void>(); 
    co_return result;
}
```

It is wise to make a habit of yielding even on success in case the entire `some_operation_which_may_fail()` operation needs to be scheduled in a loop allowing other coroutines to run. To illustrate the problem:
```
// return true on success, else false
bool some_operation_which_may_fail(int& result, bool& cont);

hce::co<int> non_blocking_coroutine() {
    int result = 0;
    bool cont = true;

    while(cont) {
        while(!some_operation_which_may_fail(result, cont)) { 
            co_await hce::yield<void>();
        }

        // yield after success to allow other coroutines to run
        co_await hce::yield<void>(); 
    }

    co_return result;
}
```

However, using `hce::yield<RETURN_TYPE>` set to the `RETURN_TYPE` of the non-blocking operation allows for clean inline statements which automatically yields execution after every run:
```
bool some_operation_which_may_fail(int& result);

hce::co<int> non_blocking_coroutine() {
    int result = 0;

    while(!(co_await hce::yield<bool>(some_operation_which_may_fail(result))) { }

    co_return result;
}
```

And a double loop example:
```
bool some_operation_which_may_fail(int& result);

hce::co<int> non_blocking_coroutine() {
    int result = 0;
    bool cont = true;

    while(cont) {
        while(!(co_await hce::yield<bool>(some_operation_which_may_fail(result,cont))) { }
    }

    co_return result;
}
```

## Chrono 
This library utilizes `hce::chrono::time_point` and `hce::chrono::duration` objects for all timing API.

`hce::chrono::time_point` is an inheritor of `std::chrono::steady_clock::time_point` and `hce::chrono::duration` is an inheritor of `std::chrono::steady_clock::duration`. Any arguments passed to their constructors behaves identically to constructing `std::chrono::steady_clock::time_point`/`std::chrono::steady_clock::duration`. This also means that `hce::chrono::` objects can be *constructed* with the equivalent `std::` variant:
```
hce::chrono::duration my_dur(std::chrono::steady_clock::milliseconds(35));
```

The current `hce::chrono::time_point` can be retrieved with `hce::chrono::now()`.

Generate `Doxygen` documentation to see more information and additional conversion API.

## Scheduler Access
Coroutines run in `hce::scheduler` objects, which are themselves running on system threads. Access to the underlying `hce::scheduler` objects can be retrieved with the following static API:
```
hce::scheduler& hce::scheduler::global(): return the process wide, default scheduler
bool hce::scheduler::in(): return true if the current thread is running a scheduler, else false
hce::scheduler& hce::scheduler::local(): return the scheduler running on the current system thread
hce::scheduler& hce::scheduler::get(): return a scheduler, preferring `local()` if available with `in()`, falling back to `global()`
```

Schedulers have various scheduling APIs similar to the static global scheduling API (the global API actually selects a scheduler and calls the equivalent API on a `scheduler`):
```
hce::awt<CO_RETURN_TYPE> hce::scheduler::schedule(co)
hce::awt<void> hce::scheduler::sleep(... duration args...)
hce::awt<void> hce::scheduler::start(hce::id&, ... duration args...)
bool hce::scheduler::cancel(const hce::id&)
hce::awt<RETURN_TYPE> hce::block(Callable, args...)
```

`hce::scheduler` objects are always allocated as `std::shared_ptr<hce::scheduler>`s. The `std::shared_ptr` of an `hce::scheduler` can be acquired by conversion:
```
// acquire the global scheduler shared_ptr from the global scheduler reference
std::shared_ptr<hce::scheduler> my_shared_scheduler = hce::scheduler::global();
```

`scheduler` objects have various API for indicating current workload and state. It is also possible to create and launch user managed schedulers, though this is a task with some complicated considerations. Generate `Doxygen` documentation to see more.

## Threadpool Access
This library provides a global `hce::threadpool` object running one or more `hce::scheduler` objects running on independent system threads, allowing coroutines to be scheduled on separate CPU cores. Coroutines don't need separate CPU cores to run efficiently and the global high level API (`hce::schedule()`) attempt to schedule on the current processor core. However, there may be cases where parallelism (instead of only concurrency) is beneficial to distribute CPU resources. 

`hce::threadpool`'s `static` scheduling API includes:
```
void hce::threadpool::schedule(...): call schedule() on a threadpool scheduler with arguments
```

`hce::threadpool` attempts to schedule on an `hce::scheduler` with a light workload when its scheduling API is called. If the user wishes to acquire a scheduler with a custom algorithm, the user can access the `const vector` of schedulers with the relevant `threadpool` API:
```
/**
 There is only ever one threadpool in existence.

 @return the reference to the process wide threadpool
 */
static threadpool& hce::threadpool::get();

/**
 @return a const reference to the managed vector of threadpool schedulers
 */
const std::vector<std::shared_ptr<hce::scheduler>>& hce::threadpool::schedulers() const;
```

It should be noted that this `vector` of `scheduler`s is only written to during `threadpool` initialization, and is safe to reference without a lock.

The count of workers can be configured at library compile time with environment variable `HCETHREADPOOLSCHEDULERCOUNT`. If this value is undefined or 0 (the library default) then the framework will determine the count of worker threads (an attempt is made to match the count of worker threads with the count of CPU cores).

If `HCETHREADPOOLSCHEDULERCOUNT` is set to 1, no threads beyond the default global scheduler (returned by `hce::scheduler::global()`) will be launched.

If `HCETHREADPOOLSCHEDULERCOUNT` is set greater than 1, the additional count of threads beyond the first will be launched.

Generate `Doxygen` documentation to see additional information and API.

## Integration with Existing Code 
Integrating this library into existing code can be done fairly easily and with few additional system resources consumed. For example, if `HCETHREADPOOLSCHEDULERCOUNT` is set to 1 during library compile time (limiting the workers on the `hce::threadpool` to 1), then this library will typically only maintain 2 additional threads by default (with more potentially launched and destroyed dynamically based on runtime `hce::block()` calls). 

The global scheduling API (like `hce::schedule()`, `hce::threadpool::schedule()`, etc.) can be used in most cases to implement all of the user's coroutine scheduling needs.

### Communication
Because `hce::channel<T>` objects work with both coroutines and non-coroutines, they can be used to easily get data in and out of coroutines running in this framework.

It is possible to construct `hce::channel<T>`s with a buffered implementation
(specify a size in `hce::channel<T>::make(unsigned int buffer_size)`) which can optimize sends from non-coroutines, allowing those system threads to continue processing without waiting for the channel recipient to receive the value.

Additionally, `hce::channel<T>`s can be constructed to use `std::mutex` instead of `hce::spinlock` for its internal atomic synchronization. This is generally not helpful *EXCEPT* in the edgecase where many system threads are competing for access to a single channel. In this scenario `std::mutex` allows the operating system to correctly block competing system threads. In most situations (IE, coroutine to coroutine communication, communication between only one sender and receiver, and particularly communication between coroutines on the same scheduler) the default `hce::spinlock` should provide superior performance. `hce::channel<T>` can be constructed with `std::mutex` by specifying with `hce::channel<T>::make<std::mutex>()` or `hce::channel<T>::make<std::mutex>(unsigned int buffer_size)`.

Similarly it is *also* possible to construct a channel without *any* atomic synchronization using `hce::channel<T>::make<hce::lockfree>()`. This is only safe when communicating between 2 or more coroutines executing on the *SAME* scheduler. Usage of `hce::schedule()` guarantees this behavior, so any coroutines scheduled with that mechanism (or when one `coroutine` schedules additional `coroutine`s using `hce::schedule()`) can make use of an `hce::lockfree` channel. This can slighly improve performance in situations where there is a lot of communication in performance critical sections.

As always, actual testing is best for determining optimizations.

### Replacing std synchronization primitives
`hce::mutex` and `hce::condition_variable` can be carefully used to replace usage of `std::mutex` and `std::condition_variable`, potentially transforming code to be coroutine-safe. This operation needs to be done with care and deeper knowledge of this project. Study of the `Doxygen` documentation is recommended.

### Compiler Define Considerations
Consider what values should be set for your usecase and can be passed to `cmake` with `cmake -DMYVARIABLE=myvalue .` when configuring the library:
- `HCEGLOBALCOROUTINEPOOLLIMIT`
- `HCEGLOBALREUSEBLOCKCOUNT`
- `HCETHREADPOOLCOROUTINEPOOLLIMIT`
- `HCETHREADPOOLREUSEBLOCKCOUNT`
- `HCETHREADPOOLSCHEDULERCOUNT`
- `HCELOGLEVEL`

Additonally, the following compiler define should be considered when building user code:
- `HCELOGLIMIT`

## Library Debug Logging 
Information on `hce` debug logging, with details about `HCELOGLIMIT` and `HCELOGLEVEL`, made by this library can be found in the [logging documentation](logging.md).
