#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

/// ensures dynamic linkage can see the marked variable
#ifdef _WIN32
    #define HCE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
    #define HCE_EXPORT __attribute__((visibility("default")))
#else
    #define HCE_EXPORT
#endif
