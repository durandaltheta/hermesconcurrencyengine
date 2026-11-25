#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <cstddef>

namespace hce {
namespace utility {

/**
 @brief deterministic round-robin index generator

 Begin at an arbitrary start index and walk the range [0, max) repeatedly.
 */
struct round_robin {
    explicit round_robin(std::size_t start = 0) noexcept
        : current_(start) {}

    /**
     @brief return the current index, then advance
     @param max exclusive upper bound (must be > 0)
     @return index in [0, max)
     */
    inline std::size_t next(std::size_t max) noexcept {
        std::size_t ret = current_ % max;
        current_ = ret + 1;
        return ret;
    }

    /// reset the next index that will be returned
    inline void reset(std::size_t start) noexcept {
        current_ = start;
    }

    /// peek without advancing
    inline std::size_t peek(std::size_t max) const noexcept {
        return current_ % max;
    }

private:
    std::size_t current_;
};

} // utility 
} // hce
