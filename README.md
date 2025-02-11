# Hermes Concurrency Engine
C++20 Stackless Coroutine Concurrency Engine 

## Rationale
`c++20` coroutines are an extremely powerful and efficient mechanism for writing concurrent code. They are also one of the most difficult parts of the language to use correctly.

This framework is designed to make using `c++20` coroutines real code much easier and to make integration into existing codebases simple.

### Example
A simple example where a coroutine is constructed, scheduled, and communicated with asynchronously. 

[example_001 source](ex/src/example_001.cpp)
```
#include <iostream>
#include <hce.hpp>

hce::co<void> my_coroutine(hce::chan<int> ch) {
    int i;

    while(co_await ch.recv(i)) {
        std::cout << "received: " << i << std::endl;
    }
}

int main() {
    auto ch = hce::chan<int>::make();
    hce::schedule(my_coroutine(ch));
    ch.send(1);
    ch.send(2);
    ch.send(3);
    ch.close();
    return 0;
}
```

Usage and output (after building with `make hce_ex`):
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

Because usage of this library depends only on the `c++` language, in theory, it should be usable on different operating systems. Actual testing on the target platform with `script/validate` is highly recommended to verify behavior.

### Build 
Set your environment variables (IE, `HCELOGLEVEL`, etc.) and execute:
```
cmake .
make hce
```

`hce.a` can then be statically linked against by user software. The directories `inc/` and `loguru/` will need to be added as user code include directories.

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

## Unit Tests
### Prerequisites
- `python3`
- `cmake`
- `gcc` or `clang` compiler toolchain

### Build and Run
Build and execute tests with `validate` script from this repository's root directory (example with `gcc`):
```
./validate gcc basic
```

The `cmake` unit test target is `hce_ut`, and this can be built separately from the `validate` frontend with 
```
cmake .; make -j hce_ut
```

However, `validate` is very useful for quickly configuring the project in various ways to test different things.

`validate` can be configured with either `gcc` or `clang`:
```
./validate clang basic
```

`validate` has a number of configurations it can build and execute tests:
- `basic`: all unit tests (without compiler optimizations)
- `log`: all unit tests with maximum debug logging enabled (without compiler optimizations)
- `mem`: all unit tests with address sanitization enabled (without compiler optimizations) 
- `jitter`: non-timing unit tests compiled and executed with many variations of loglevels to check for random timing errors (without compiler optimizations)
- `release`: all unit tests with maximum compiler optimizations
- `ALL`: build an execute each configuration sequentially

`ALL` is useful for doing a broad sanity test to ensure everything is working before a public release.

`validate` output is stored in a log in the project root named `validate.log`. 

## HCE Coroutines
`c++20` coroutines are specialized, lightweight "function-like" objects (Functors) generated by the compiler. A `c++20` coroutine behaves as a function that is capable of suspending (pausing execution) at various points, only to resume execution at some future point. The underlying mechanics of how this is accomplished are complex to understand and generally unnecessary to learn in order to use this library. 

Coroutines are valuable because they are orders of magnitude faster to context switch (that is, change which one is actively executing) than the operating system equivalent of a "system thread" (POSIX `pthread`s and/or `c++` `std::thread`s). This makes executing code which needs to handle multiple simultaneous tasks (that may or may not execute on the same CPU core) written with coroutines potentially *MUCH* faster and more resource efficient (coroutines are very lightweight) than the same done with only system threads.

It is enough for using this framework that each `hce` compatible coroutine must:
### Utilize coroutine keywords
Utilize one or more of the following `c++20` coroutine keywords:
```
co_return // coroutine specific 'return' statement
co_await // used to safely block a coroutine on a returned awaitable object
```

`co_yield` keyword is not utilized by this framework.

NOTE: `awaitable` objects are returned from various functions in this framework with the type `hce::awt<AWAITABLE_RETURN_TYPE>`. `AWAITABLE_RETURN_TYPE` is the type returned from the awaitable when `co_await`ed:
```
// given this function
hce::awt<int> awaitable_function();

// this coroutine co_awaits the result of function_returning_awt_int()
hce::co<int> my_coroutine() {
    int my_result = co_await function_returning_awt_int();
    co_return my_result;
}
```

### Coroutines return a coroutine type
The `hce` coroutine function must specify its return type as `hce::co<COROUTINE_RETURN_TYPE>`. `COROUTINE_RETURN_TYPE` is the type of the returned value by the `co_return` statement.

```
hce::co<int> my_coroutine_returning_int() {
    co_return 3;
}
```

### Coroutines are 1 stack frame
The coroutine function must be exactly one stack frame in size, all `co_return`, `co_await`, and `co_yield` statements associated with a single coroutine happen directly in the coroutine body.

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

hce::co<std::string> coroutine_with_arguments(int arg1, std::string arg2) {
    co_return std::to_string(arg1) + arg2;
}
```

When an `hce` coroutine is first invoked, it does not immediately run. Instead it constructs the `hce::co<T>` return object. This object can be passed to a `hce` scheduling operation, such as `hce::schedule()` so that it will actually execute.

WARNING: `lambda` functions, as well as Functors (objects with implement the Call operator `()` implemented), can be coroutines. HOWEVER, there are very specific rules on what data is available to the coroutine function when it executes. The shorthand is: do not use `lambda` captures or Functor object members within a coroutine body, pass them in as function arguments. For Functors, ensure the coroutine is a `static` function. See [the en.cppreference.com coroutine documentation](https://en.cppreference.com/w/cpp/language/coroutines) for the complicated details.

Generate `Doxygen` documentation to see more for `hce` coroutine creation.

## Starting the Framework
To initialize the framework user code is responsible for calling `hce::initialize()` and holding onto the resulting `std::unique_ptr`. The returned pointer should stay in scope until all other `hce` operations have completed and joined:
```
hce::schedule(co): Schedule a coroutine and return an awaitable which can be awaited to return the coroutine return value
hce::scope(co, ...): Schedule and join with one or more coroutines, ignoring return values
hce::sleep(hce::duration): Block a coroutine for a period of time
hce::block(Callable, args...): Call a function which may block the calling thread and join the coroutine with the result
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
    std::unique_ptr<hce::lifecycle> lf = hce::initialize();
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

## Communication
This library allows communication between coroutines, threads, and any combination there-in using `hce::chan<T>`s. 

`hce::chan` (a "channel") is a specialized communication mechanism which works correctly for both `hce` coroutines and system threads, and any combination of the two. This makes communication between coroutines and non-coroutines trivial.

A simple example program:
```
#include <iostream>
#include <hce.hpp> 

hce::co<void> my_coroutine(hce::chan<int> in_ch, hce::chan<int> out_ch) {
    int i;
    
    // receive a value from main
    co_await in_ch.recv(i);
    std::cout << "my_coroutine received " << i << std::endl;
    
    // send a value to main
    co_await out_ch.send(i+1);
    co_return;
}

int main() {
    auto in_ch = hce::chan<int>::make();
    auto out_ch = hce::chan<int>::make();

    hce::schedule(my_coroutine(in_ch, out_ch));

    // send a value to my_coroutine
    in_ch.send(16); // system threads do not call co_await 

    // receive a value from my_coroutine
    int my_result;
    out_ch.recv(my_result);

    std::cout << "main received " << my_result << std::endl;
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

### Alternative channel implementations
`hce::chan`s with a buffered internal queue can be constructed by passing a buffer size to `hce::chan<T>::make(int buffer_size)`. `hce::chan<T>`s with a buffer will not block on `send()` operations until the buffer becomes full. This can be used to optimize send operations from system threads (non-coroutines), where blocking the entire thread is a much more expensive operation.

Meaning of `buffer_size`:
`>0`: channel buffer of a specific maximum size (blocks on send if buffer is full)
`0`: channel buffer of no size (direct point to point data transfer, blocks if no receiver)
`<0`: channel buffer of unlimited size (never blocks on send)

Generate `Doxygen` documentation to see more, specifically for `hce::chan<T>` unbuffered/buffered/unlimited channel construction and other API.

## Thread Blocking Calls
Arbitrary functions which may block a calling coroutine are unsafe to use directly by a coroutine because they will block the calling thread. Doing this will stop the processing of coroutines and in the worst case cause system deadlock. 

Instead, those functions can be executed with a call to `hce::block()` to safely block the coroutine without blocking the system thread:

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
    // wrapping my_blocking_function in hce::block() allows other coroutines to run
    int my_result = co_await hce::block(my_blocking_function, arg);
    co_return my_result;
}

int main() {
    int my_result = hce::join(my_coroutine(3));
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

`hce::block()` calls launch and self manage system threads to guarantee operations execute in an isolated manner. It is possible to configure an `hce::scheduler` to keep one or more block worker threads in existence to lower the cost for repeated `hce::block()` operations.

Generate `Doxygen` documentation to see more for `hce::block()` and `hce::scheduler` configuration options.

## Thread Non-Blocking Calls

A coroutine can implement a non-blocking code by using `hce::yield<T>` objects. `co_await`ing an instance of an `hce::yield<T>` object yields a value as the result of the `co_await` statement, but suspends execution of the coroutine allowing other coroutines to run first. 

Example:
```
int i = co_await hce::yield<int>(3); // i == 3 when coroutine resumes
```

`hce::yield<void>` is a legal instance of `hce::yield`, but returns no value, merely suspending execution of the calling coroutine:
```
 co_await hce::yield<void>(); 
```

`hce::yield<T>` can be returned arbitrarily from functions as well. Best practice is to implement non-blocking code which might fail to return an `hce::yield` templated to the result of the operation. The calling coroutine can then `co_await` the non-blocking operation until it succeeds, or the coroutine executes some fallback behavior:
```
enum result {
    success,
    failure
};

hce::yield<result> non_blocking_op() {
    // ... implementation ...
    return hce::yield<result>(/* value yield will return on co_await */);
}

hce::co<result> attempt_non_blocking() {
    result r = failure;

    do {
        // coroutine yields execution on co_await for each attempt
        r = co_await non_blocking_op();
    } while(r == failure);
            
    co_return r;
};
```

## Debug Logging
This project utilizes the [emilk/loguru](https://github.com/emilk/loguru) project for debug logging, writing to stdout and stderr by default. Logging features are provided primarily for the development of this library, but work has been done to make it fairly robust and may be applicable for user development and production code debugging purposes. See the [logging primer](logging.md) for more information.
