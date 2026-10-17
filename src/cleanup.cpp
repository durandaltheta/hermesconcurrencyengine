#include <new>
#include "cleanup.hpp"

hce::cleanup::cleanup() : list_(nullptr) { }
hce::cleanup::~cleanup(){}

void hce::cleanup::install(operation op, void* arg) {
    node* next = (node*)(this->cleanup_alloc(sizeof(node)));
    new(next) node(list_, op, arg);
    list_ = next;
}

void hce::cleanup::clean() {
    // trigger the callback if it is set, then unset it
    if(list_) [[likely]] {
        do {
            data d{list_->install, this};
            list_->op(d);
            node* old = list_;
            list_ = list_->next;
            this->cleanup_dealloc(old);
        } while(list_);
    }
}

hce::cleanup::node::node(node* n, operation o, void* i) :
    next(n),
    op(o),
    install(i)
{ }
