#pragma once
//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 

#include <cstddef>

namespace hce {
namespace config {
namespace blocking {

/**
 @brief the count of reusable worker blocking threads shared by the process 

 Can be increased if `hce::block()` calls are made frequently to avoid having to 
 launch new system threads by reusing old ones. 
 */
size_t reusable_block_worker_cache_size();

} // blocking
} // config
} // hce
