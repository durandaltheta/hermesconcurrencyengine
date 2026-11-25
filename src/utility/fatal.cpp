#include <string>
#include <exception>

#include "hce/utility/fatal.hpp"
#include "hce/utility/logging.hpp"

namespace hce {
namespace detail {

void fatal_(std::stringstream& ss) {
    HCE_FATAL_LOG("%s", ss.str().c_str());
    std::terminate();
}

} // detail
} // hce
