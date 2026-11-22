#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <sstream>

namespace hce {
namespace detail {

void fatal_(std::stringstream& ss);

template <typename A, typename... As>
void fatal_(std::stringstream& ss, A&& a, As&&... as) {
    ss << a;
    fatal_(ss, std::forward<As>(as)...);
}

} // detail

template <typename... As>
void fatal(As&&... as) {
    std::stringstream ss;
    detail::fatal_(ss, std::forward<As>(as)...);
}

}
