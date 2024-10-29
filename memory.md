# Synopsis 
This framework makes extensive use of allocated memory, in such a way that the allocation itself is potentially a bottleneck. This is because `coroutine`s, and their single-use `awaitable` communication primitives, are allocated implementations. Furthermore, objects which implement communication mechanisms are often utilizing containers which frequently allocate and deallocate.

Allocation and deallocation, while very fast, may utilize process-wide atomic locking, which adds a small cost when every allocation has to acquire the lock, and a larger cost whenever multiple threads of execution need to allocate. Because coroutine communication (and thus context switching) is a primary point of improvement over traditional inter-thread mechanisms, it is important to address the problem of allocation, especially as inter-thread communication increases in user code.

This framework implements several tiers and types of memory allocation caching in order to improve throughput of repeated allocations through reuse. It does this *without* modifying `new`/`malloc()`/`delete`/`free()` implementations. The user is free to utilize the default implementations or link their own if they desire without interfering with the framework. 

Ultimately, `malloc()`/`free()` are *still* used for every allocated value, and are called as necessary. The following mechanisms only exist to limit how frequently they need to be called.

## High Level Allocation/Deallocation
Allocation and deallocation are done through the templated `hce::allocate<T>(size_t count)`/`hce::deallocate<T>(size_t count)` mechanisms. These functions implement memory aligned allocation (similar to `std::aligned_alloc()`). They *do not* construct the memory `T`. The template `T` is used only for determining the size of the allocation. 

This framework generally replaces usages of `new`/`delete` keywords with these mechanisms, and uses "placement `new`" in order to construct allocated pointers and explicit calls to `~T()` to destruct.

`hce::allocate<T>()`/`hce::deallocate<T>()` call these underlying functions:
```
void* hce::memory::allocate(size_t size);
void* hce::memory::allocate(size_t alignment, size_t size);
void hce::memory::deallocate(void* p);
void hce::memory::deallocate(void* p, size_t size);
```

## Thread Local Caching
The first, and most general, allocation caching mechanism is each thread has a `thread_local` `hce::memory::cache`. `cache`s cache a variety of relatively small allocation tiers for reuse. `hce::memory::allocate()`/`hce::memory::deallocate()` functions are abstractions to calling the `thread_local` `cache`.

The `cache` is configured at framework build time with the following `cmake` defined values:
- `HCETHREADLOCALMEMORYBUCKETCOUNT`: the count of buckets which cache memory chunks in powers of 2, ie: bucket[0] holds allocations of 1 byte, bucket[1] holds allocations of 2 bytes, bucket[2] holds allocations of 4 bytes, etc.
- `HCETHREADLOCALMEMORYBUCKETBYTELIMIT`: the byte limit any bucket can cache before being forced to call `std::free()` on the allocated memory.

The default `hce::config` function which is used to initialize the `cache` is this function:
```
extern hce::config::memory::cache::info& hce::config::memory::cache::get_info();
```

The user can either modify the `cmake` defines or implement a custom `get_info()`. 

Custom implementations of `get_info()` provide the opportunity for fine-grained control of the cache. For example, if the user determines that a few buckets needs a much higher (or lower) byte limit, they can implement the *exact* bucket limits and block size values in their implementation.

## Default Allocator 
The `hce::allocator<T>` is a replacement for `std::allocator<T>` which uses `hce::allocate<T>()`/`hce::deallocate<T>()` instead of directly calling `malloc()`/`free()`. This allows the user to easily make use of the framework's allocation caching in their `std::` compliant containers.

## Pool Allocator 
A second tier of allocation caching is implemented by the `hce::pool_allocator<T>`. Like `hce::allocator<T>`, `pool_allocator`s can be used as the allocator in `std::` containers. However, their purpose is to implement allocation caching for a *particular* use case, allowing code which utilizes it to set the size of the cache and to guarantee that allocated values in the `pool_allocator` are private, and therefore reusable allocated memory is guaranteed to be available if they have been previously cached.

This mechanism is especially useful for container and communication mechanisms which need to repeatedly allocate values of a pre-known size. By separately caching allocations for reuse in the pool, the code can more precisely limit the calls to `malloc()`/`free()` based on superior knowledge of a given usecase.
