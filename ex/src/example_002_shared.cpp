#include <iostream>
#include "hce.hpp"

struct example_module : public hce::module {
    virtual ~example_module(){}

    hce::co<int> start(void* context) {
        return op(*((int*)context));
    }

private:
    static inline hce::co<int> op(int i) {
        std::cout << "example_module ran with int[" << i << "]" << std::endl;
        co_return 0;
    }
};

extern "C" void* hce_module_create() {
    return new example_module;
}

extern "C" void hce_module_destroy(void* module) {
    delete (example_module*)module;
}
