#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis

#include <cstddef>

namespace hce {
namespace config {
namespace network {

/**
 @brief count of worker threads 

 If 0, count is set to hardware concurrency
*/
size_t thread_count();

} // network
} // config
} // hce
