//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <cassert>
#include "hce.hpp"
#include "test_helpers.hpp"
#include "test_module_helpers.hpp"

struct module_impl : public hce::module {
    virtual ~module_impl(){} 

    hce::co<int> start(void* context) {
        return op((test::module::interface*)context);
    }

private:
    static inline hce::co<int> op(test::module::interface* intf) {
        std::string fname = "module_impl";
        test::module::command command;
        bool cont = true;

        while(true) {
            // continue until command channel is closed
            cont = co_await intf->comch.recv(command);

            if(cont) {
                switch(command) {
                    case test::module::command::error:
                        HCE_ERROR_FUNCTION_BODY(fname, "received command: error");
                        break;
                    case test::module::command::send_concurrent_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: send_concurrent_req");
                        co_await intf->resch.send(co_await intf->cchs.send(fname.c_str()));
                        break;
                    case test::module::command::send_parallel_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: send_parallel_req");
                        co_await intf->resch.send(co_await intf->pchs.send(fname.c_str()));
                        break;
                    case test::module::command::receive_concurrent_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: receive_concurrent_req");
                        co_await intf->resch.send(co_await intf->cchs.receive(fname.c_str()));
                        break;
                    case test::module::command::receive_parallel_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: receive_parallel_req");
                        co_await intf->resch.send(co_await intf->pchs.receive(fname.c_str()));
                        break;
                    case test::module::command::blocking_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: blocking_req");
                        co_await intf->resch.send(co_await intf->blk.launch());
                        break;
                    case test::module::command::timing_req:
                        HCE_INFO_FUNCTION_BODY(fname, "received command: timing_req");
                        co_await intf->resch.send(co_await intf->tmr.launch());
                        break;
                }
            } else {
                break;
            }
        }

        HCE_INFO_FUNCTION_BODY(fname, "done");

        // prove we can send non-zero
        co_return test::module::interface::expected_code;
    }
};

extern "C" void* hce_module_create() {
    return new module_impl;
}

extern "C" void hce_module_destroy(void* module) {
    delete (module_impl*)module;
}
