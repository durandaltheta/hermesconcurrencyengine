#include <new>
#include "cleanup.hpp"

hce::cleanup::cleanup() : 
    head_(nullptr, nullptr, nullptr),
    list_(nullptr) 
{ }

hce::cleanup::~cleanup(){}

void hce::cleanup::install(hce::cleanup::operation op, void* arg) {
    if(list_) [[unlikely]] {
        hce::cleanup::node* next = (hce::cleanup::node*)(hce::memory::allocate(sizeof(node)));
        new(next) node(list_, op, arg);
        list_ = next;
    } else [[likely]] {
        head_.next = nullptr;
        head_.op = op;
        head_.install = arg;
        list_ = &head_;
    }
}

void hce::cleanup::clean() {
    // trigger the callback if it is set, then unset it
    if(list_) [[likely]] {
        hce::cleanup::data d;
        d.self = this; // set this only once

        do {
            d.install = list_->install;
            list_->op(d);
            node* old = list_;
            list_ = list_->next;

            if(old != &head_) [[unlikely]] {
                hce::memory::deallocate(old);
            }

        } while(list_);
    }
}

hce::cleanup::node::node(hce::cleanup::node* n, hce::cleanup::operation o, void* i) :
    next(n),
    op(o),
    install(i)
{ }
