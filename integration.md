# Integration with Existing Code 
Integrating this library into existing code can be done fairly easily and with few additional system resources consumed. For example, if `HCETHREADPOOLSCHEDULERCOUNT` is set to 1 during library compile time (limiting the workers on the `hce::threadpool` to 1), then this library will typically only maintain 2 additional threads by default (with more potentially launched and destroyed dynamically based on runtime `hce::block()` calls). 

The global scheduling API (like `hce::schedule()`, `hce::threadpool::schedule()`, etc.) can be used in most cases to implement all of the user's coroutine scheduling needs.

## Communication
Because `hce::channel<T>` objects work with both coroutines and non-coroutines, they can be used to easily get data in and out of coroutines running in this framework.

It is possible to construct `hce::channel<T>`s with a buffered implementation
(specify a size in `hce::channel<T>::make(int buffer_size)`) which can optimize sends from non-coroutines, allowing those system threads to continue processing without waiting for the channel recipient to receive the value.

It is also possible to construct an `hce::channel<T>` with an internal queue that has no size limit (an "unlimited" channel), by providing a *negative* value to `hce::channel<T>::make(int buffer_size)`.

Additionally, `hce::channel<T>`s can be constructed to use `std::mutex` instead of `hce::spinlock` for its internal atomic synchronization. This is generally not helpful or more performant *EXCEPT* in the edgecase where many system threads are competing for access to a single channel. In this scenario `std::mutex` allows the operating system to correctly block competing system threads. In most situations (IE, coroutine to coroutine communication, communication between only one sender and receiver, and particularly communication between coroutines on the same scheduler) the default `hce::spinlock` should provide superior performance. `hce::channel<T>` can be constructed with `std::mutex` by specifying with `hce::channel<T>::make<std::mutex>()` or `hce::channel<T>::make<std::mutex>(int buffer_size)`.

Similarly it is *also* possible to construct a channel without *any* atomic synchronization using `hce::channel<T>::make<hce::lockfree>()`. This is only safe when communicating between 2 or more coroutines executing on the *SAME* scheduler. Usage of `hce::schedule()` guarantees this behavior, so any coroutines scheduled with that mechanism (or when one `coroutine` schedules additional `coroutine`s using `hce::schedule()`) can make use of an `hce::lockfree` channel. This can slighly improve performance in situations where there is a lot of communication in performance critical sections.

As always, actual testing is best for determining optimizations.

## Replacing std synchronization primitives
`hce::mutex` and `hce::condition_variable` can be carefully used to replace usage of `std::mutex` and `std::condition_variable`, potentially transforming code to be coroutine-safe. This operation needs to be done with care and deeper knowledge of this project. Study of the `Doxygen` documentation is recommended.

## Optimization Considerations
Consider what values should be set for your usecase and can be passed to `cmake` with `cmake -DMYVARIABLE=myvalue .` when configuring the library:
- `HCEPOOLALLOCATORDEFAULTBLOCKLIMIT`
- `HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT`
- `HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT`
- `HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT`
- `HCETHREADPOOLCOROUTINERESOURCELIMIT`
- `HCETHREADPOOLBLOCKWORKERRESOURCELIMIT`
- `HCETHREADPOOLSCHEDULERCOUNT`
- `HCELOGLEVEL`
- `HCELOGLIMIT`

Additonally, the following compiler define should be considered when building user code:
- `HCELOGLIMIT`

# Library Debug Logging 
Information on `hce` debug logging, with details about `HCELOGLIMIT` and `HCELOGLEVEL`, made by this library can be found in the [logging documentation](logging.md).
