#include "timer.hpp"

hce::timer::service& hce::timer::service::get() {
    static hce::timer::service ts;
    HCE_TRACE_FUNCTION_ENTER("hce::timer::service::get",ts);
    return ts;
}
