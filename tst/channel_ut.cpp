//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <mutex>
#include <functional>

#include "loguru.hpp"
#include "atomic.hpp"
#include "logging.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "channel.hpp"

#include <gtest/gtest.h>  
#include "test_helpers.hpp"

namespace test {
namespace channel {

template <typename T>
void context_construct_capacity_T() {
    // unbuffered spinlock
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,hce::spinlock>>();
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        ASSERT_EQ(ctx, ch.context());

        ch.construct(0);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(0,ch.size());

        auto ctx2 = ch.context();
        ch.template construct<hce::spinlock>(0);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_NE(ctx2, ch.context());
    }

    // unbuffered lockfree
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,hce::lockfree>>();
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
        ASSERT_EQ(ctx, ch.context());
        ASSERT_EQ(0,ch.size());

        ch.template construct<hce::lockfree>(0);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
        ASSERT_NE(ctx, ch.context());
    }

    // unbuffered std::mutex
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,std::mutex>>();
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        ASSERT_EQ(ctx, ch.context());
        ASSERT_EQ(0,ch.size());

        ch.template construct<std::mutex>(0);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        ASSERT_NE(ctx, ch.context());
    }

    // buffered spinlock size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,hce::spinlock>>(1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<hce::spinlock>(1);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(1,ch.size());

        auto ctx2 = ch.context();
        ch.template construct<hce::spinlock>(1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_NE(ctx2, ch.context());
        ASSERT_EQ(1337,ch.size());
    }

    // buffered lockfree size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,hce::lockfree>>(1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<hce::lockfree>(1);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(1,ch.size());

        ch.template construct<hce::lockfree>(1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(1337,ch.size());
    }

    // buffered std::mutex size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,std::mutex>>(1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<std::mutex>(1);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(1,ch.size());

        ch.template construct<std::mutex>(1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(1337,ch.size());
    }

    // unlimited spinlock size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unlimited<T,hce::spinlock>>(-1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::spinlock>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<hce::spinlock>(-1);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(-1,ch.size());

        auto ctx2 = ch.context();
        ch.template construct<hce::spinlock>(-1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::spinlock>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_NE(ctx2, ch.context());
        ASSERT_EQ(-1,ch.size());
    }

    // unlimited lockfree size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unlimited<T,hce::lockfree>>(-1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::lockfree>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<hce::lockfree>(-1);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::lockfree>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(-1,ch.size());

        ch.template construct<hce::lockfree>(-1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::lockfree>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(-1,ch.size());
    }

    // unlimited std::mutex size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        auto ctx = std::make_shared<hce::unlimited<T,std::mutex>>(-1);
        ch.context(ctx);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,std::mutex>));
        ASSERT_EQ(ctx, ch.context());

        ch.template construct<std::mutex>(-1);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,std::mutex>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(-1,ch.size());

        ch.template construct<std::mutex>(-1337);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,std::mutex>));
        ASSERT_NE(ctx, ch.context());
        ASSERT_EQ(-1,ch.size());
    }
}

}
}

TEST(channel, context_construct_capacity) {
    test::channel::context_construct_capacity_T<int>();
    test::channel::context_construct_capacity_T<unsigned int>();
    test::channel::context_construct_capacity_T<size_t>();
    test::channel::context_construct_capacity_T<float>();
    test::channel::context_construct_capacity_T<double>();
    test::channel::context_construct_capacity_T<char>();
    test::channel::context_construct_capacity_T<void*>();
    test::channel::context_construct_capacity_T<std::string>();
    test::channel::context_construct_capacity_T<test::CustomObject>();
}

namespace test {
namespace channel {

template <typename T>
void make_capacity_T() {
    // unbuffered spinlock
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::make();
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));

        ch = hce::channel<T>::template make<hce::spinlock>();
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),0);

        ch = hce::channel<T>::template make<hce::spinlock>(0);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),0);
    }

    // unbuffered lockfree
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::lockfree>();
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));

        ch = hce::channel<T>::template make<hce::lockfree>(0);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
    }

    // unbuffered std::mutex
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<std::mutex>();
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        ASSERT_EQ(ch.size(),0);
        
        ch = hce::channel<T>::template make<std::mutex>(0);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        ASSERT_EQ(ch.size(),0);
    }

    // buffered spinlock size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::spinlock>(1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::spinlock>(1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),1337);
    }

    // buffered lockfree
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::lockfree>(1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        ASSERT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::lockfree>(1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        ASSERT_EQ(ch.size(),1337);
    }

    // buffered std::mutex
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<std::mutex>(1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        ASSERT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<std::mutex>(1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        ASSERT_EQ(ch.size(),1337);
    }

    // unlimited spinlock size 1
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::spinlock>(-1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),-1);

        ch = hce::channel<T>::template make<hce::spinlock>(-1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::spinlock>));
        ASSERT_EQ(ch.size(),-1);
    }

    // unlimited lockfree
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::lockfree>(-1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::lockfree>));
        ASSERT_EQ(ch.size(),-1);

        ch = hce::channel<T>::template make<hce::lockfree>(-1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,hce::lockfree>));
        ASSERT_EQ(ch.size(),-1);
    }

    // unlimited std::mutex
    {
        hce::channel<T> ch; 
        ASSERT_FALSE(ch);
        ch = hce::channel<T>::template make<std::mutex>(-1);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,std::mutex>));
        ASSERT_EQ(ch.size(),-1);

        ch = hce::channel<T>::template make<std::mutex>(-1337);
        ASSERT_TRUE(ch);
        ASSERT_EQ(ch.type_info(), typeid(hce::unlimited<T,std::mutex>));
        ASSERT_EQ(ch.size(),-1);
    }
}

}
}

TEST(channel, make_capacity) {
    test::channel::make_capacity_T<int>();
    test::channel::make_capacity_T<unsigned int>();
    test::channel::make_capacity_T<size_t>();
    test::channel::make_capacity_T<float>();
    test::channel::make_capacity_T<double>();
    test::channel::make_capacity_T<char>();
    test::channel::make_capacity_T<void*>();
    test::channel::make_capacity_T<std::string>();
    test::channel::make_capacity_T<test::CustomObject>();
}

namespace test {
namespace channel {

template <typename T>
hce::co<void> co_store_recv_till_close_return_void(hce::channel<T> ch, test::queue<T>& q) {
    T t;

    while(co_await ch.recv(t)) { 
        q.push(std::move(t)); 
    }
}

template <typename T>
hce::co<void> co_send_count_and_close_return_void(hce::channel<T> ch, size_t count) {
    for(; count>0; --count) {
        co_await ch.send((T)test::init<T>(count));
    }

    ch.close();
}

template <typename T>
size_t send_recv_close_T(const size_t count) {
    size_t success_count=0;

    // thread to thread
    {
        auto test = [&](hce::channel<T> ch) {
            test::queue<T> q;

            std::thread thd([](hce::channel<T> ch, test::queue<T>& q){
                T t;
                while(ch.recv(t)) { 
                    q.push(std::move(t)); 
                }
            },ch,std::ref(q));

            for(size_t i=count; i>0; --i) {
                ch.send((T)test::init<T>(i));
            }

            for(size_t i=count; i>0; --i) {
                ASSERT_EQ((T)test::init<T>(i), q.pop());
            }

            ch.close();
            thd.join();

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));
        test(hce::channel<T>::make(-1));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
        test(hce::channel<T>::template make<std::mutex>(-1));
    }

    // thread to coroutine 
    {
        auto test = [&](hce::channel<T> ch) {
            test::queue<T> q;
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_store_recv_till_close_return_void(ch,q));

            for(size_t i=count; i>0; --i) {
                ch.send((T)test::init<T>(i));
            }

            for(size_t i=count; i>0; --i) {
                ASSERT_EQ((T)test::init<T>(i), q.pop());
            }

            ch.close();

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));
        test(hce::channel<T>::make(-1));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
        test(hce::channel<T>::template make<std::mutex>(-1));
    }

    // coroutine to thread
    {
        auto test = [&](hce::channel<T> ch){
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_send_count_and_close_return_void(ch,count));

            T t;

            for(size_t i=count; i>0; --i) {
                ASSERT_TRUE((bool)ch.recv(t));
                ASSERT_EQ((T)test::init<T>(i), t);
            }

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));
        test(hce::channel<T>::make(-1));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
        test(hce::channel<T>::template make<std::mutex>(-1));
    }

    // coroutine to coroutine 
    {
        auto test = [&](hce::channel<T> ch){
            test::queue<T> q;
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_send_count_and_close_return_void(ch,count));
            auto awt2 = sch->schedule(test::channel::co_store_recv_till_close_return_void(ch,q));

            for(size_t i=count; i>0; --i) {
                ASSERT_EQ((T)test::init<T>(i), q.pop());
            }

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));
        test(hce::channel<T>::make(-1));

        // lockfree 
        test(hce::channel<T>::template make<hce::lockfree>());
        test(hce::channel<T>::template make<hce::lockfree>(1));
        test(hce::channel<T>::template make<hce::lockfree>(count));
        test(hce::channel<T>::template make<hce::lockfree>(-1));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
        test(hce::channel<T>::template make<std::mutex>(-1));
    }
            
    return success_count;
}

template <typename T>
hce::co<void> co_store_recv_interrupt_with_close_return_void(hce::channel<T> ch, test::queue<T>& q) {
    q.push((T)test::init<T>(0));

    T t;
    while(co_await ch.recv(t)) { 
        q.push((T)test::init<T>(1));
    }

    q.push((T)test::init<T>(2));
}

template <typename T>
hce::co<void> co_send_count_interrupt_with_close_return_void(hce::channel<T> ch) {
    ch.close();
    co_return;
}

template <typename T>
size_t send_recv_interrupt_with_close_T(const size_t count) {
    size_t success_count=0;

    // thread to thread
    {
        auto test = [&](hce::channel<T> ch) {
            test::queue<T> q;

            std::thread thd([](hce::channel<T> ch, test::queue<T>& q){
                T t;
                q.push((T)test::init<T>(0));

                while(ch.recv(t)) {
                    q.push((T)test::init<T>(1)); 
                }

                q.push((T)test::init<T>(2));
            },ch,std::ref(q));

            ch.close();

            ASSERT_EQ((T)test::init<T>(0), q.pop());
            ASSERT_EQ((T)test::init<T>(2), q.pop());

            ch.close();
            thd.join();
        };

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(count));
        }

        ++success_count;

        // std::mutex
        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(count));
        }

        ++success_count;
    }

    // thread to coroutine 
    {
        auto test = [&](hce::channel<T> ch) {
            test::queue<T> q;
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_store_recv_interrupt_with_close_return_void(ch,q));

            ch.close();

            ASSERT_EQ((T)test::init<T>(0), q.pop());
            ASSERT_EQ((T)test::init<T>(2), q.pop());
        };

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(count));
        }

        ++success_count;

        // std::mutex
        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(count));
        }

        ++success_count;
    }

    // coroutine to thread
    {
        auto test = [&](hce::channel<T> ch){
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_send_count_interrupt_with_close_return_void(ch));

            T t;
            ASSERT_FALSE((bool)ch.recv(t));
        };

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(count));
        }

        ++success_count;

        // std::mutex
        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(count));
        }

        ++success_count;
    }

    // coroutine to coroutine 
    {
        auto test = [&](hce::channel<T> ch){
            test::queue<T> q;
            auto lf = hce::scheduler::make();
            std::shared_ptr<hce::scheduler> sch = lf->scheduler();
            auto awt = sch->schedule(test::channel::co_store_recv_interrupt_with_close_return_void(ch,q));
            auto awt2 = sch->schedule(test::channel::co_send_count_interrupt_with_close_return_void(ch));

            ASSERT_EQ((T)test::init<T>(0), q.pop());
            ASSERT_EQ((T)test::init<T>(2), q.pop());
        };

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::make(count));
        }

        ++success_count;

        // lockfree 
        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<hce::lockfree>());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<hce::lockfree>(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<hce::lockfree>(count));
        }

        ++success_count;

        // std::mutex
        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>());
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(1));
        }

        ++success_count;

        for(size_t i=count; i>0; --i) {
            test(hce::channel<T>::template make<std::mutex>(count));
        }

        ++success_count;
    }

    return success_count;
}

}
}

TEST(channel, send_recv_close) {
    size_t success_count = 0;
    const size_t expected = 36;

    // count of sends and receives
    auto test = [&](const size_t count) {
        ASSERT_EQ(expected, test::channel::send_recv_close_T<int>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<unsigned int>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<size_t>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<float>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<double>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<char>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<void*>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<std::string>(count));
        ASSERT_EQ(expected, test::channel::send_recv_close_T<test::CustomObject>(count));
        ++success_count;
    };

    test(0);
    test(1);
    test(10);
    test(100);
    test(1000);
    ASSERT_EQ(5,success_count);
}

TEST(channel, send_recv_interrupt_close) {
    size_t success_count = 0;
    const size_t expected = 27;

    // count of sends and receives
    auto test = [&](const size_t count) {
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<int>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<unsigned int>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<size_t>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<float>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<double>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<char>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<void*>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<std::string>(count));
        ASSERT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<test::CustomObject>(count));
        ++success_count;
    };

    test(0);
    test(1);
    test(5);
    test(10);
    test(15);
    ASSERT_EQ(5,success_count);
}

