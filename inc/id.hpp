//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_ID__
#define __HERMES_COROUTINE_ENGINE_ID__

#include <memory>
#include <cstddef>
#include <string>
#include <sstream>

#include "logging.hpp"

namespace hce {

/**
 @brief unique identifier object

 Arbitrary allocated memory. The unique address of this memory is usable as a key 
 */
struct id : public std::shared_ptr<std::byte>, public printable {
    template <typename... As>
    id(As&&... as) : std::shared_ptr<std::byte>(std::forward<As>(as)...) {
        HCE_TRACE_CONSTRUCTOR();
    }

    virtual ~id() { HCE_TRACE_DESTRUCTOR(); }

    static inline hce::string info_name() { return "hce::id"; }
    inline hce::string name() const { return id::info_name(); }

    inline hce::string content() const {
        hce::stringstream ss;
        auto addr = get();

        if(addr) {
            ss << hce::type::name<std::shared_ptr<std::byte>>() << "::get():" << addr;
        } else {
            ss << "nullptr";
        }

        return ss.str();
    }
};

}

#endif
