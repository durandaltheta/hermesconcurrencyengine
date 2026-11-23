#include <string>
#include <exception>

#include "hce/fatal.hpp"
#include "hce/logging.hpp"

namespace hce {
namespace detail {

void fatal_(std::stringstream& ss) {
    HCE_FATAL_LOG("%s", ss.str().c_str());
    std::terminate();
}

} // detail
} // hce
