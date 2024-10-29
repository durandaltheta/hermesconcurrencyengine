# Building the Library 
This library can be configured in many different ways using defines and optionally user provided `hce::config` function implementations.

## Default Configuration Defines
This project has various `cmake` `-D` defines which determine compiled library runtime behavior. IE, `cmake -DHCELOGLEVEL 4 .` to set `HCELOGLEVEL` to `4` when compiling the project.

### Resource Allocation Limit Configuration Defines
- `HCETHREADLOCALMEMORYBUCKETCOUNT`: the count of buckets which thread local cache memory chunks in powers of 2
- `HCETHREADLOCALMEMORYBUCKETBYTELIMIT`: the byte limit any thread local bucket can cache before being forced to call `std::free()` on the allocated memory.
- `HCEPOOLALLOCATORDEFAULTBLOCKLIMIT`: the default resuable block `T` allocation limit in `hce::pool_allocator<T>`s, used by many mechanisms in this framework.
- `HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT`: the default coroutine resource limit in `hce::scheduler::config`, used in initializing `hce::scheduler`s during `hce::scheduler::make()`
- `HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT`: the coroutine resource limit for the `hce::scheduler` returned by `hce::scheduler::global()`
- `HCETHREADPOOLCOROUTINERESOURCELIMIT`: the coroutine resource limit for the `hce::scheduler`s in the `hce::threadpool`

Higher reusable resource allocation limits can potentially allow faster CPU throughput in scenarios with a lot of allocations, at the expense of potentially more memory being held for the lifetime of the objects. If CPU throughput is slow, and enough memory is available, these values can be increased as a potential optimization.

Specifically, the coroutine resource limits affect the count of coroutines that the global `hce::scheduler` and `hce::threadpool` managed `hce::scheduler`s will cache reusable resources for. A higher value can lower the cost of repeated allocation/deallocation, potentially improving coroutine processing throughput.

See [memory documentation](memory.md) for information about the memory and allocation design of this framework.

### Block Worker Resource Limit Configuration Defines
- `HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT`
- `HCETHREADPOOLBLOCKWORKERRESOURCELIMIT`

Count of persistent, reusable threads for running `hce::block()` calls managed by the process-wide default global `hce::scheduler` and `hce::threadpool` managed `hce::scheduler`s, respectively. Reusing a thread reduces the need for system calls. This only sets the limit for reuse caching, as many threads as necessary will be spawned to support `hce::block()`. 

If many `hce::block()` calls are being made, consider increasing these values as a throughput optimization.

### Threadpool Scheduler Count Configuration Define
- `HCETHREADPOOLSCHEDULERCOUNT`

Count of process-wide `hce::threadpool` worker threads running coroutine `hce::scheduler`s. This has a direct impact on the the number of parallel CPUs the threadpool can use.

A value of:
`0`: Allow the framework to decide the count of schedulers (the global scheduler is always spawned)
`>0`: Allow the framework to spawn the global `hce::scheduler` returned by `hce::scheduler::global()`, and an additional `count - 1` `hce::threadpool` managed `hce::scheduler`s (the first scheduler in the threadpool is always the `hce::scheduler` returned by `hce::scheduler::global()`).

### Logging Configuration Defines
- `HCELOGLEVEL`: The default `hce` loglevel of threads. See [logging documentation](logging.md)
- `HCELOGLIMIT`: A framework *AND* user code compile time option which limits what log statements are actually compiled, see [logging documentation](logging.md)

## User Code Compilation Defines 
Additionally, user code can set the following compiler defines  (IE, `g++ -DHCELOGLIMIT 4`, etc.)
.

## Custom Configuration
Various default `hce::config::` namespace `extern` functions are implemented in [config.cpp](src/config.cpp), allowing the user to override them by implementing their own versions and linking them against their code first.

If implementing custom `hce::config::` functionality, be aware that project configuration defines are used by the project default implementations (with the exception of `HCELOGLIMIT`). Therefore the user is responsible for how those values are utilized when writing their own implementations.
