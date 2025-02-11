#include <iostream>
#include <hce.hpp>

hce::co<void> my_coroutine(hce::chan<int> ch) {
    int i;

    while(co_await ch.recv(i)) {
        std::cout << "received: " << i << std::endl;
    }

    co_return;
}

int main() {
    // start the framework and stash RAII management object on stack
    auto lifecycle = hce::initialize(); 
    auto ch = hce::chan<int>::make();
    auto awt = hce::schedule(my_coroutine(ch));
    ch.send(1);
    ch.send(2);
    ch.send(3);
    ch.close();
    return 0;
}
