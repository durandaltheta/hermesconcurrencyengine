# Synopsis 
This framework makes extensive use of allocated memory, in such a way that allocation itself is potentially a bottleneck. This is because `coroutine`s, and their single-use `awaitable` communication primitives, are allocated implementations. Furthermore, objects which implement communication mechanisms are often utilizing containers which themselves frequently allocate and deallocate.

Allocation and deallocation, while very fast, may utilize process-wide atomic locking, which adds a small cost when every allocation has to acquire the lock, and a larger cost whenever multiple threads of execution need to allocate. Because coroutine communication (and thus context switching) is a primary point of improvement over traditional inter-thread mechanisms, it is important to address the problem of allocation, especially as inter-thread communication increases in user code.

This framework implements several tiers and types of memory allocation caching in order to improve throughput of repeated allocations through reuse. It does this *without* modifying `new`/`malloc()`/`delete`/`free()` implementations. The user is free to utilize the default implementations or link their own if they desire without interfering with the framework. 

Ultimately, `malloc()`/`free()` are *still* used for every allocated value, and are called as necessary. The following mechanisms only exist to limit how frequently they need to be called.

## High Level Allocation/Deallocation
Allocation and deallocation are done through the templated `hce::allocate<T>(size_t count)`/`hce::deallocate<T>()` mechanisms. These functions implement memory aligned allocation (similar to `std::aligned_alloc()`). They *do not* construct the memory `T`. The template `T` is used only for determining the size of the allocation. 

This framework generally replaces usages of `new`/`delete` keywords with these mechanisms, and uses "placement `new`" in order to construct allocated pointers and explicit calls to `~T()` to destruct.

`hce::allocate<T>()`/`hce::deallocate()` call these underlying functions:
```
void* hce::memory::allocate(size_t size);
void hce::memory::deallocate(void* p);
```

## Thread Local Caching
The first, and most general, allocation caching mechanism is each thread has a `thread_local` `hce::memory::cache`. `cache`s house a variety of relatively small allocation buckets for allocation reuse. `hce::memory::allocate()`/`hce::memory::deallocate()` functions are abstractions to calling the `thread_local` `cache`'s methods, which are lockless.

The `cache` is configured at framework build time with the following `cmake` defined values:
- `HCETHREADLOCALMEMORYBUCKETCOUNT`: the count of buckets which cache memory chunks in powers of 2, ie: bucket[0] holds allocations of 1 byte, bucket[1] holds allocations of 2 bytes, bucket[2] holds allocations of 4 bytes, etc.
- `HCETHREADLOCALMEMORYBUCKETBYTELIMIT`: the byte limit any bucket can cache before being forced to call `std::free()` on the allocated memory.

The default `hce::config` function which is used to initialize the `cache` is this function:
```
extern hce::config::memory::cache::info& hce::config::memory::cache::info::get();
```

The user can either modify the `cmake` defines or implement and link a custom `hce::config::memory::cache::info::get()`. The default implementation provides different caching configurations based on the type of thread that needs the cache. For example, the thread running the `hce::scheduler` returned by `hce::scheduler::global()` is expected to have the highest memory reuse burden of any system thread, and is given due consideration.

Custom implementations of `info()` provide the opportunity for fine-grained control of the cache. For example, if the user determines that a few buckets needs a much higher (or lower) byte limit, they can implement the *exact* bucket limits and block size values in their implementation.

## Default Allocator 
The `hce::allocator<T>` is a replacement for `std::allocator<T>` which uses `hce::allocate<T>()`/`hce::deallocate<T>()` instead of directly calling `malloc()`/`free()`. This allows the user to easily make use of the framework's allocation caching in their `std::` compliant containers.

## Pool Allocator 
A second tier of allocation caching is implemented by the `hce::pool_allocator<T>`. Like `hce::allocator<T>`, `pool_allocator`s can be used as the allocator in `std::` containers. However, their purpose is to implement allocation caching for a *particular* use case, allowing code which utilizes it to set the size of the cache and to guarantee that allocated values in the `pool_allocator` are private, and therefore reusable allocated memory is guaranteed to be available if they have been previously cached.

This mechanism is especially useful for container and communication mechanisms which need to repeatedly allocate values of a pre-known size. By separately caching allocations for reuse in the pool, the code can more precisely limit the calls to `malloc()`/`free()` based on superior knowledge of a given usecase.

## Scheduler Coroutine Resource Limits
A third example of memory allocation optimization comes in the form of `hce::scheduler` coroutine resource limits.

List of memory related resource limits:
- `HCESCHEDULERDEFAULTCOROUTINERESOURCELIMIT`: A fallback resource limit
- `HCEGLOBALSCHEDULERCOROUTINERESOURCELIMIT`: The global `hce::scheduler` resource limit
- `HCETHREADPOOLCOROUTINERESOURCELIMIT`: `hce::threadpool` `hce::scheduler` resource limit

Resource limits are more of a heuristic than the previously described strategies. Specifically, they are tied to the specific size limits of `hce::pool_allocator<T>`s used internally by `hce::scheduler` objects, related to things like coroutine queue memory caching. Sane limits allow for more efficient coroutine processing in the median case, because less time is spent by the `hce::scheduler` allocating resources which are constantly being reused. 

These values have *no* effect on the actual code running *inside* the coroutines running on a given `hce::scheduler`. They are instead about smoothing the algorithmic processing of scheduling itself.

## Blocking Call Resource Limits
A fourth example of memory allocation optimization is in the creation and caching of `hce::blocking::call()` workers because threads are unavoidably expensive:
 - they require system calls for startup, kernel scheduling, and shutdown (very slow)
 - their memory cost is larger than most user objects (due to stack size and `thread_local`s)

Due to this there is a need to balance startup speed via worker thread caching versus holding unnecessary amounts of memory from caching worker threads. 

Therefore this framework provides a few key configurations which allow the user to fine tune their blocking call management:
- `HCEPROCESSBLOCKWORKERRESOURCELIMIT`: The count of cacheable workers shared amongst a process-wide cache.
- `HCEGLOBALSCHEDULERBLOCKWORKERRESOURCELIMIT`: The count of cacheable workers for *only* the thread running the global scheduler in a lockfree cache.
- `HCEDEFAULTSCHEDULERBLOCKWORKERRESOURCELIMIT`: The count of cacheable workers for threads running  other schedulers in their own lockfree caches.

If memory consumption is a primary concern, consider limiting or setting to `0` each of these values, forcing deallocation after every `hce::blocking::call()`.

If CPU efficiency is the primary concern, set `HCEPROCESSBLOCKWORKERRESOURCELIMIT` high enough to handle the median count of blocking tasks. 

If lock contention on the `hce::blocking::service` object is a limiting factor, ensure the scheduler resource limits are high enough so that the lock on the `hce::blocking::service` is lowered.
