#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include <cstddef>

namespace hce {
namespace sort {

// Branch-friendly compare-exchange (compilers commonly turn this into cmovs for pointers).
// Uses std::less for a guaranteed total order (relational < on unrelated pointers is unspecified).
void cmp_swap(void*& a, void*& b);

// In-place sort of an array of pointers by address (lowest → highest).
// Optimized for the common case N ≤ 8 via optimal sorting networks;
// falls back to std::sort for larger N.
void pointers(void** arr, std::size_t n);

} // sort 
} // hce
