#include "environment.hpp"

std::unique_ptr<hce::environment> hce::environment::clone() {
    HCE_HIGH_FUNCTION_ENTER("hce::environment::clone");
    return std::unique_ptr<hce::environment>(new environment(
        hce::scheduler::lifecycle::service::instance_,
        hce::scheduler::global::service::instance_,
        hce::threadpool::service::instance_,
        hce::blocking::service::instance_,
        hce::timer::service::instance_));
}

hce::environment::install() {
    HCE_HIGH_METHOD_ENTER("install");
    hce::scheduler::lifecycle::service::instance_ = env.scheduler_lifecycle_service_;
    hce::scheduler::global::service::instance_ = env.scheduler_global_service_;
    hce::threadpool::service::instance_ = env.threadpool_service_;
    hce::blocking::service::instance_ = env.blocking_service_;
    hce::timer::service::instance_ = env.timer_service_;
}
