// SPDX-License-Identifier: MIT
// Author: Blayne Dennis

#include <array>
#include <cstdint>
#include <tuple>
#include <utility>
#include <bit>

#include "memory.hpp"   
#include "alloc.hpp"   

namespace { // anonymous

// memory block allocations have this before their beginning address
struct header {
    uint8_t index; // index of the source bucket

    // allocate a block of memory with a header
    static header* allocate(const std::size_t size, const uint8_t index) {
        // the final allocated value is a block containing the cache header
        // and the requested memory block
        header* hdr = (header*)std::malloc(sizeof(header) + size);
        hdr->index = index; // set the header value for deallocate() lookup
        return hdr;
    }

    // deallocate a block of memory with a header
    static void deallocate(header* hdr) {
        std::free(hdr);
    }

    // acquire the header pointer from the allocated block
    static header* to(const void* ptr) {
        return (header*)((char*)ptr - sizeof(header));
    }

    // acquire the allocated block from the header pointer
    static void* from(const header* hdr) {
        return (void*)((char*)hdr + sizeof(header));
    }
};

/// Reusable block type whose size is exactly a power of two.
/// Used as the value_type of each size-class pool.
template <std::size_t Bytes>
struct power_of_two_block {
    static_assert(Bytes != 0 && (Bytes & (Bytes - 1)) == 0,
                  "Bytes must be a non-zero power of two");

    // Align to the block size itself (safe for powers of two that do not
    // exceed the platform's maximum alignment).
    alignas(Bytes) std::byte storage[Bytes];
};

/// Compile-time exponent → block size.
template <std::size_t Exponent>
inline constexpr std::size_t power_of_two_size = 1ull << Exponent;

/// Next power of two (at least 1). Zero maps to 1.
inline constexpr std::size_t next_power_of_two(std::size_t n) noexcept {
    if (n == 0) return 1;
    return std::bit_ceil(n);
}

/// Floor log2 (undefined for 0).
inline constexpr std::size_t log2_floor(std::size_t n) noexcept {
    return std::bit_width(n) - 1;
}

/// Size-class index for a requested byte size.
/// Index 0 == 2^MinExponent, index k == 2^(MinExponent+k).
template <std::size_t MinExponent, std::size_t NumClasses>
inline constexpr std::size_t size_class_index(std::size_t bytes) noexcept {
    constexpr std::size_t min_size = power_of_two_size<MinExponent>;
    constexpr std::size_t max_size = power_of_two_size<MinExponent + NumClasses - 1>;

    if(bytes <= min_size){ return 0; }
    if(bytes > max_size){ return NumClasses; }  // sentinel: too large

    std::size_t rounded = next_power_of_two(bytes);
    return log2_floor(rounded) - MinExponent;
}

/**
 * @brief Collection of pool_allocators, one per power-of-two size class.
 *
 * Each element of the underlying tuple is a pool_allocator of a distinct
 * power_of_two_block<2^k>.  Allocation requests are rounded up to the next
 * power of two and served from the corresponding pool.
 *
 * @tparam MinExponent  Smallest size class is 2^MinExponent bytes.
 * @tparam NumClasses   Number of successive powers of two to support.
 * @tparam BlockLimit   Forwarded to each pool_allocator (default from config).
 */
template <std::size_t MinExponent = 3,          // 8 bytes
          std::size_t NumClasses  = 10,         // up to 2^(3+9) = 4 KiB
          std::size_t BlockLimit  = 0>          // 0 → use config default
struct size_class_pools {
    static_assert(NumClasses > 0, "need at least one size class");

    static constexpr std::size_t min_exponent = MinExponent;
    static constexpr std::size_t num_classes  = NumClasses;
    static constexpr std::size_t min_size     = power_of_two_size<MinExponent>;
    static constexpr std::size_t max_size     = power_of_two_size<MinExponent + NumClasses - 1>;

    // Index-sequence generator that produces the tuple of pools
    template <std::size_t... Exps>
    static auto make_pools(std::index_sequence<Exps...>) {
        if constexpr (BlockLimit == 0) {
            return std::tuple<pool_t<MinExponent + Exps>...>{};
        } else {
            return std::tuple<pool_t<MinExponent + Exps>...>(
                pool_t<MinExponent + Exps>(BlockLimit)...);
        }
    }

    // Helper: pool type for a given exponent
    template <std::size_t Exp>
    using pool_t = hce::pool_allocator<
        power_of_two_block<
            power_of_two_size<Exp>>>;

    using pools_t = decltype(make_pools(std::make_index_sequence<NumClasses>{}));

    // Runtime-index dispatch via recursive compile-time unrolling
    template <std::size_t I>
    void* allocate_at(std::size_t idx) {
        if (I == idx) {
            auto& p = std::get<I>(pools_);
            return p.allocate(1);          // one block of this size class
        }

        if constexpr (I + 1 < NumClasses) {
            return allocate_at<I + 1>(idx);
        }
        
        return nullptr;
    }

    template <std::size_t I>
    void deallocate_at(std::size_t idx, void* ptr) {
        if(I == idx) {
            auto& p = std::get<I>(pools_);
            using block = power_of_two_block<power_of_two_size<MinExponent + I>>;
            p.deallocate(static_cast<block*>(ptr), 1);
            return;
        }

        if constexpr (I + 1 < NumClasses) {
            deallocate_at<I + 1>(idx, ptr);
        }
    }

    size_class_pools() = default;

    /// Allocate a block large enough for `bytes` (rounded up to next power of two).
    /// Returns nullptr if the request exceeds the largest size class.
    void* allocate(std::size_t bytes) {
        const std::size_t idx = size_class_index<MinExponent, NumClasses>(bytes);
        if (idx >= NumClasses) return nullptr;

        return allocate_at<0>(idx);
    }

    /// Deallocate a block previously returned by allocate().
    /// The original requested size (or the rounded size) must be supplied.
    void deallocate(void* p, std::size_t bytes) {
        if (!p) return;
        const std::size_t idx = size_class_index<MinExponent, NumClasses>(bytes);
        if (idx >= NumClasses) return;

        deallocate_at<0>(idx, p);
    }

    /// Direct access to a particular size-class pool (compile-time index).
    template <std::size_t Index>
    auto& pool() noexcept {
        static_assert(Index < NumClasses, "index out of range");
        return std::get<Index>(pools_);
    }

    template <std::size_t Index>
    const auto& pool() const noexcept {
        static_assert(Index < NumClasses, "index out of range");
        return std::get<Index>(pools_);
    }

    pools_t pools_;
};

constexpr std::size_t min_exponent = 3; // 8 bytes, 64 bit
constexpr std::size_t max_buckets = 11;
constexpr std::size_t max_bucket_byte_size = 1ull << (min_exponent + max_buckets - 1);

static size_class_pools<min_exponent, max_buckets, 0>& tl_pools() {
    thread_local size_class_pools<min_exponent, max_buckets, 0> p;
    return p;
}

} // anonymous

namespace hce {
namespace memory {

// Non-pooled path: header + payload, index = sentinel so pool::deallocate
// can still route it if someone crosses the APIs.
void* allocate(std::size_t size) {
    constexpr uint8_t sentinel = static_cast<uint8_t>(max_buckets); // "not pooled"
    return header::from(header::allocate(size, sentinel));
}

void deallocate(void* p) {
    if(!p){ return; }
    header::deallocate(header::to(p));
}

namespace pool {

void* allocate(std::size_t size) {
    // Account for the header that will live in front of the user pointer.
    const std::size_t needed = size + sizeof(header);

    if(needed > max_bucket_byte_size) {
        return hce::memory::allocate(size);          // sentinel header path
    }

    const std::size_t idx = size_class_index<min_exponent, max_buckets>(needed);
    void* block = tl_pools().allocate_at<0>(idx);    // raw power-of-two block
    
    if(!block) {
        return hce::memory::allocate(size);
    }

    // Embed the index at the front of the block and hand back the payload.
    header* hdr = static_cast<header*>(block);
    hdr->index  = static_cast<uint8_t>(idx);
    return header::from(hdr);
}

void deallocate(void* p) {
    if (!p) return;

    header* hdr = header::to(p);
    const uint8_t idx = hdr->index;

    if (idx < max_buckets) {
        // Pooled: the block address is the header address.
        tl_pools().deallocate_at<0>(idx, hdr);
    } else {
        // Sentinel → original malloc path.
        hce::memory::deallocate(p);
    }
}

} // pool
} // memory
} // hce
