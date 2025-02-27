//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <cassert>
#include "hce.hpp"
#include "test_helpers.hpp"
#include "test_module_helpers.hpp"

struct module_ut : public hce::module {
    virtual ~module_ut(){} 

    hce::co<int> start(void* context) {
        return op((test::module::interface*)context);
    }

private:
    static inline hce::co<int> op(test::module::interface* intf) {
        // concurrent channel receive/sends test
        co_await intf->conc_chs.receive(false);
        co_await intf->conc_chs.send(false);

        // parallel channel receive/sends test
        co_await intf->para_chs.receive(true);
        co_await intf->para_chs.send(true);

        // block tests
        co_await intf->blk.launch();

        // timer tests
        co_await intf->tmr.launch();

        // prove we can send non-zero
        co_return interface::expected_code;
    }
};

extern "C" void* hce_module_create() {
    return new module_ut;
}

extern "C" void hce_module_destroy(void* module) {
    delete (module_ut*)module;
}
