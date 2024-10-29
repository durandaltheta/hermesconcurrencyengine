#include <iostream>
#include <hce.hpp>

hce::co<void> my_coroutine(hce::channel<int> ch) {
    int i;

    while(co_await ch.recv(i)) {
        std::cout << "received: " << i << std::endl;
    }
}

int main() {
    auto ch = hce::channel<int>::make();
    auto awt = hce::schedule(my_coroutine(ch));
    ch.send(1);
    ch.send(2);
    ch.send(3);
    ch.close();
    return 0;
}
