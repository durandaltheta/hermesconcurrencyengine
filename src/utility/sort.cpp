#include "hce/utility/sort.hpp"
#include <functional>  // std::less
#include <algorithm>

namespace hce {
namespace sort {

void cmp_swap(void*& a, void*& b) {
    if(std::less<Ptr>{}(b, a)) {
        std::swap(a, b);
    }
}

void pointers(void** arr, std::size_t n) {
    switch (n) {
    case 0:
    case 1:
        return;

    case 2:
        cmp_swap(arr[0], arr[1]);
        return;

    case 3:
        cmp_swap(arr[0], arr[2]);
        cmp_swap(arr[0], arr[1]);
        cmp_swap(arr[1], arr[2]);
        return;

    case 4:
        cmp_swap(arr[0], arr[2]); cmp_swap(arr[1], arr[3]);
        cmp_swap(arr[0], arr[1]); cmp_swap(arr[2], arr[3]);
        cmp_swap(arr[1], arr[2]);
        return;

    case 5:
        cmp_swap(arr[0], arr[3]); cmp_swap(arr[1], arr[4]);
        cmp_swap(arr[0], arr[2]); cmp_swap(arr[1], arr[3]);
        cmp_swap(arr[0], arr[1]); cmp_swap(arr[2], arr[4]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[3], arr[4]);
        cmp_swap(arr[2], arr[3]);
        return;

    case 6:
        cmp_swap(arr[0], arr[5]); cmp_swap(arr[1], arr[3]); cmp_swap(arr[2], arr[4]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[3], arr[4]);
        cmp_swap(arr[0], arr[3]); cmp_swap(arr[2], arr[5]);
        cmp_swap(arr[0], arr[1]); cmp_swap(arr[2], arr[3]); cmp_swap(arr[4], arr[5]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[3], arr[4]);
        return;

    case 7:
        cmp_swap(arr[0], arr[6]); cmp_swap(arr[2], arr[3]); cmp_swap(arr[4], arr[5]);
        cmp_swap(arr[0], arr[2]); cmp_swap(arr[1], arr[4]); cmp_swap(arr[3], arr[6]);
        cmp_swap(arr[0], arr[1]); cmp_swap(arr[2], arr[5]); cmp_swap(arr[3], arr[4]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[4], arr[6]);
        cmp_swap(arr[2], arr[3]); cmp_swap(arr[4], arr[5]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[3], arr[4]); cmp_swap(arr[5], arr[6]);
        return;

    case 8:
        cmp_swap(arr[0], arr[2]); cmp_swap(arr[1], arr[3]); cmp_swap(arr[4], arr[6]); cmp_swap(arr[5], arr[7]);
        cmp_swap(arr[0], arr[4]); cmp_swap(arr[1], arr[5]); cmp_swap(arr[2], arr[6]); cmp_swap(arr[3], arr[7]);
        cmp_swap(arr[0], arr[1]); cmp_swap(arr[2], arr[3]); cmp_swap(arr[4], arr[5]); cmp_swap(arr[6], arr[7]);
        cmp_swap(arr[2], arr[4]); cmp_swap(arr[3], arr[5]);
        cmp_swap(arr[1], arr[4]); cmp_swap(arr[3], arr[6]);
        cmp_swap(arr[1], arr[2]); cmp_swap(arr[3], arr[4]); cmp_swap(arr[5], arr[6]);
        return;

    default:
        // Introsort; already uses insertion sort for small subranges.
        std::sort(arr, arr + n, std::less<Ptr>{});
        return;
    }
}

std::size_t unique_non_null(void** arr, std::size_t n) noexcept {
    std::size_t unique = 0;
    for (std::size_t i = 0; i < n; ++i) {
        void* p = arr[i];
        if (p == nullptr)
            continue;

        // Linear scan of the already-accepted uniques (tiny for N < 8)
        bool seen = false;
        for (std::size_t j = 0; j < unique; ++j) {
            if (arr[j] == p) {
                seen = true;
                break;
            }
        }
        if (!seen)
            arr[unique++] = p;
    }

    // Clear the tail so later code can rely on nulls
    for (std::size_t i = unique; i < n; ++i)
        arr[i] = nullptr;

    return unique;
}

} // sort 
} // hce
