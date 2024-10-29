//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <mutex>
#include <functional>

#include "loguru.hpp"
#include "atomic.hpp"
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
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,hce::spinlock>>();
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct();
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(0,ch.size());

        auto ctx2 = ch.context();
        ch.template construct<hce::spinlock>();
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_NE(ctx2, ch.context());
    }

    // unbuffered lockfree
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,hce::lockfree>>();
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());
        EXPECT_EQ(0,ch.size());

        ch.template construct<hce::lockfree>();
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
    }

    // unbuffered std::mutex
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::unbuffered<T,std::mutex>>();
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        EXPECT_EQ(ctx, ch.context());
        EXPECT_EQ(0,ch.size());

        ch.template construct<std::mutex>();
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        EXPECT_NE(ctx, ch.context());
    }

    // buffered spinlock size 1
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,hce::spinlock>>(1);
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_EQ(ctx, ch.context());

        ch.construct(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        ch.construct(0);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        auto ctx2 = ch.context();
        ch.template construct<hce::spinlock>(1337);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_NE(ctx2, ch.context());
        EXPECT_EQ(1337,ch.size());
    }

    // buffered lockfree size 1
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,hce::lockfree>>(1);
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_EQ(ctx, ch.context());

        ch.template construct<hce::lockfree>(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        ch.template construct<hce::lockfree>(0);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        ch.template construct<hce::lockfree>(1337);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1337,ch.size());
    }

    // buffered std::mutex size 1
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        auto ctx = std::make_shared<hce::buffered<T,std::mutex>>(1);
        ch.context(ctx);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_EQ(ctx, ch.context());

        ch.template construct<std::mutex>(1);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        ch.template construct<std::mutex>(0);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1,ch.size());

        ch.template construct<std::mutex>(1337);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_NE(ctx, ch.context());
        EXPECT_EQ(1337,ch.size());
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
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::make();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));

        ch = hce::channel<T>::template make<hce::spinlock>();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::spinlock>));
        EXPECT_EQ(ch.size(),0);
    }

    // unbuffered lockfree
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::lockfree>();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,hce::lockfree>));
    }

    // unbuffered std::mutex
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::template make<std::mutex>();
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::unbuffered<T,std::mutex>));
        EXPECT_EQ(ch.size(),0);
    }

    // buffered spinlock size 1
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::spinlock>(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::spinlock>(0);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::spinlock>(1337);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::spinlock>));
        EXPECT_EQ(ch.size(),1337);
    }

    // buffered lockfree
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::template make<hce::lockfree>(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::lockfree>(0);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<hce::lockfree>(1337);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,hce::lockfree>));
        EXPECT_EQ(ch.size(),1337);
    }

    // buffered std::mutex
    {
        hce::channel<T> ch; 
        EXPECT_FALSE(ch);
        ch = hce::channel<T>::template make<std::mutex>(1);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<std::mutex>(0);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_EQ(ch.size(),1);

        ch = hce::channel<T>::template make<std::mutex>(1337);
        EXPECT_TRUE(ch);
        EXPECT_EQ(ch.type_info(), typeid(hce::buffered<T,std::mutex>));
        EXPECT_EQ(ch.size(),1337);
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
    while(co_await ch.recv(t)) { q.push(std::move(t)); }
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
                ch.send(test::init<T>(i));
            }

            for(size_t i=count; i>0; --i) {
                EXPECT_EQ((T)test::init<T>(i), q.pop());
            }

            ch.close();
            thd.join();

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
    }

    // thread to coroutine 
    {
        auto test = [&](hce::channel<T> ch) {
            test::queue<T> q;
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_store_recv_till_close_return_void(ch,q));

            for(size_t i=count; i>0; --i) {
                ch.send(test::init<T>(i));
            }

            for(size_t i=count; i>0; --i) {
                EXPECT_EQ((T)test::init<T>(i), q.pop());
            }

            ch.close();
            lf.reset();
            thd.join();

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
    }

    // coroutine to thread
    {
        auto test = [&](hce::channel<T> ch){
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_send_count_and_close_return_void(ch,count));

            T t;

            for(size_t i=count; i>0; --i) {
                EXPECT_TRUE((bool)ch.recv(t));
                EXPECT_EQ((T)test::init<T>(i), t);
            }

            lf.reset();
            thd.join();
            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
    }

    // coroutine to coroutine 
    {
        auto test = [&](hce::channel<T> ch){
            test::queue<T> q;
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_send_count_and_close_return_void(ch,count));
            sch->schedule(test::channel::co_store_recv_till_close_return_void(ch,q));

            for(size_t i=count; i>0; --i) {
                EXPECT_EQ((T)test::init<T>(i), q.pop());
            }

            lf.reset();
            thd.join();

            ++success_count;
        };

        test(hce::channel<T>::make());
        test(hce::channel<T>::make(1));
        test(hce::channel<T>::make(count));

        // lockfree 
        test(hce::channel<T>::template make<hce::lockfree>());
        test(hce::channel<T>::template make<hce::lockfree>(1));
        test(hce::channel<T>::template make<hce::lockfree>(count));

        // std::mutex
        test(hce::channel<T>::template make<std::mutex>());
        test(hce::channel<T>::template make<std::mutex>(1));
        test(hce::channel<T>::template make<std::mutex>(count));
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

            EXPECT_EQ((T)test::init<T>(0), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());

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
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_store_recv_interrupt_with_close_return_void(ch,q));

            ch.close();

            EXPECT_EQ((T)test::init<T>(0), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());

            lf.reset();
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

    // coroutine to thread
    {
        auto test = [&](hce::channel<T> ch){
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_send_count_interrupt_with_close_return_void(ch));

            T t;
            EXPECT_FALSE((bool)ch.recv(t));

            lf.reset();
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

    // coroutine to coroutine 
    {
        auto test = [&](hce::channel<T> ch){
            test::queue<T> q;
            auto inst = hce::scheduler::make();
            auto lf = std::move(inst->lifecycle());
            std::shared_ptr<hce::scheduler> sch = inst->scheduler();
            std::thread thd([](std::unique_ptr<hce::scheduler::install> i){ }, std::move(inst));
            sch->schedule(test::channel::co_store_recv_interrupt_with_close_return_void(ch,q));
            sch->schedule(test::channel::co_send_count_interrupt_with_close_return_void(ch));

            EXPECT_EQ((T)test::init<T>(0), q.pop());
            EXPECT_EQ((T)test::init<T>(2), q.pop());

            lf.reset();
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
    const size_t expected = 27;

    // count of sends and receives
    auto test = [&](const size_t count) {
        EXPECT_EQ(expected, test::channel::send_recv_close_T<int>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<unsigned int>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<size_t>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<float>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<double>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<char>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<void*>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<std::string>(count));
        EXPECT_EQ(expected, test::channel::send_recv_close_T<test::CustomObject>(count));
        ++success_count;
    };

    test(0);
    test(1);
    test(10);
    test(100);
    test(1000);
    EXPECT_EQ(5,success_count);
}

TEST(channel, send_recv_interrupt_close) {
    size_t success_count = 0;
    const size_t expected = 27;

    // count of sends and receives
    auto test = [&](const size_t count) {
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<int>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<unsigned int>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<size_t>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<float>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<double>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<char>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<void*>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<std::string>(count));
        EXPECT_EQ(expected, test::channel::send_recv_interrupt_with_close_T<test::CustomObject>(count));
        ++success_count;
    };

    test(0);
    test(1);
    test(5);
    test(10);
    test(15);
    EXPECT_EQ(5,success_count);
}

