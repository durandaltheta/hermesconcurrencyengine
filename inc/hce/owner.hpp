#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <utility>

#include "hce/base.hpp"

namespace hce {

/**
 @brief helper class for managing process lifetime ownership 

 Intended to be used (primarily internally) in cases where:
 - lifetime of an object needs to be carefully controlled by the lifecycle instance
 - a `friend` designation is needed by a dependent class
 */
template <typename T>
struct owner {
    T t;

private:
    owner() = default;

    template <typename A, typename... As>
    owner(A&& a, As&&... as) :
        t(std::forward<A>(a), std::forward<As>(as)...)
    { }

    friend struct lifecycle;
};

} // hce
