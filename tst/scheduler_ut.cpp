//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <thread>
#include <chrono>
#include <vector>
#include <list>
#include <forward_list>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"

#include <gtest/gtest.h> 

namespace test {
namespace scheduler {

template <typename T>
struct queue {
    template <typename TSHADOW>
    void push(TSHADOW&& t) {
        {
            std::lock_guard<hce::spinlock> lk(slk);
            vals.push_back(std::forward<TSHADOW>(t));
        }
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<hce::spinlock> lk(slk);
        while(!vals.size()) {
            cv.wait(lk);
        }

        T res = std::move(vals.front());
        vals.pop_front();
        return res;
    }

private:
    hce::spinlock slk;
    std::condition_variable_any cv;
    std::deque<T> vals;
};

inline hce::co<void> co_void() { co_return; }

inline hce::co<void> co_push_int(queue<int>& q, int i) {
    q.push(i);
    co_return;
}

inline hce::co<void> co_scheduler_in_check(queue<void*>& q) { 
    q.push(hce::scheduler::in() ? (void*)1 : (void*)0);
    co_return;
}

inline hce::co<void> co_scheduler_local_check(queue<void*>& q) { 
    q.push(&(hce::scheduler::local()));
    co_return;
}

inline hce::co<void> co_scheduler_global_check(queue<void*>& q) { 
    q.push(&(hce::scheduler::global()));
    co_return;
}

}
}

TEST(scheduler, make_with_lifecycle) {
    std::shared_ptr<hce::scheduler> sch;

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
        lf->suspend();
        EXPECT_EQ(hce::scheduler::state::suspended, sch->status());
        lf->resume();
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

TEST(scheduler, conversions) {
    std::shared_ptr<hce::scheduler> sch;

    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());

        hce::scheduler& sch_ref = *sch;
        EXPECT_EQ(&sch_ref, sch.get());

        std::shared_ptr<hce::scheduler> sch_cpy = *sch;
        EXPECT_EQ(sch_cpy, sch);

        std::weak_ptr<hce::scheduler> sch_weak = *sch;
        EXPECT_EQ(sch_weak.lock(), sch);
    }

    EXPECT_EQ(hce::scheduler::state::halted, sch->status());
}

TEST(scheduler, install) {
    // halt with lifecycle 
    {
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        EXPECT_EQ(hce::scheduler::state::ready, sch->status());

        std::thread thd([&]{ sch->install(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, sch->status());
        thd.join();
    }

    // halt during suspend
    {
        test::scheduler::queue<hce::scheduler::state> state_q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        auto config = hce::scheduler::config::make();

        // install 1 init handler
        config->on_init.install([&]{ 
            state_q.push(hce::scheduler::state::ready);
        });

        // install 3 suspend handlers
        for(size_t i=0; i<3; ++i) {
            config->on_suspend.install([&]{ 
                state_q.push(hce::scheduler::state::suspended);
            });
        }

        // install 2 halt handlers
        for(size_t i=0; i<2; ++i) {
            config->on_halt.install([&]{ 
                state_q.push(hce::scheduler::state::halted);
            });
        }

        std::thread thd([&]{ sch->install(std::move(config)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        EXPECT_EQ(hce::scheduler::state::ready, state_q.pop());

        lf->suspend();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        
        lf->resume();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lf->suspend();
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        thd.join();
    }

    // halt during run
    {
        test::scheduler::queue<hce::scheduler::state> state_q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        auto config = hce::scheduler::config::make();

        // install 1 init handler
        config->on_init.install([&]{ 
            state_q.push(hce::scheduler::state::ready);
        });

        // install 3 suspend handlers
        for(size_t i=0; i<3; ++i) {
            config->on_suspend.install([&]{ 
                state_q.push(hce::scheduler::state::suspended);
            });
        }

        // install 2 halt handlers
        for(size_t i=0; i<2; ++i) {
            config->on_halt.install([&]{ 
                state_q.push(hce::scheduler::state::halted);
            });
        }

        std::thread thd([&]{ sch->install(std::move(config)); });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::running, sch->status());

        EXPECT_EQ(hce::scheduler::state::ready, state_q.pop());

        lf->suspend();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::suspended, state_q.pop());
        
        lf->resume();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        lf.reset();
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        EXPECT_EQ(hce::scheduler::state::halted, state_q.pop());
        thd.join();
    }
}

TEST(scheduler, schedule) {
    {
        test::scheduler::queue<int> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        sch->schedule(test::scheduler::co_push_int(q,3));
        sch->schedule(test::scheduler::co_push_int(q,2));
        sch->schedule(test::scheduler::co_push_int(q,1));

        try {
            EXPECT_EQ(3, q.pop());
            EXPECT_EQ(2, q.pop());
            EXPECT_EQ(1, q.pop());

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        test::scheduler::queue<int> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::vector<hce::co<void>> cos;
        cos.push_back(test::scheduler::co_push_int(q,3));
        cos.push_back(test::scheduler::co_push_int(q,2));
        cos.push_back(test::scheduler::co_push_int(q,1));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ(3, q.pop());
            EXPECT_EQ(2, q.pop());
            EXPECT_EQ(1, q.pop());

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        test::scheduler::queue<int> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::list<hce::co<void>> cos;
        cos.push_back(test::scheduler::co_push_int(q,3));
        cos.push_back(test::scheduler::co_push_int(q,2));
        cos.push_back(test::scheduler::co_push_int(q,1));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ(3, q.pop());
            EXPECT_EQ(2, q.pop());
            EXPECT_EQ(1, q.pop());

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }

    {
        test::scheduler::queue<int> q;
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        std::forward_list<hce::co<void>> cos;
        cos.push_front(test::scheduler::co_push_int(q,3));
        cos.push_front(test::scheduler::co_push_int(q,2));
        cos.push_front(test::scheduler::co_push_int(q,1));

        try {
            sch->schedule(std::move(cos));

            EXPECT_EQ(1, q.pop());
            EXPECT_EQ(2, q.pop());
            EXPECT_EQ(3, q.pop());

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }
}

TEST(scheduler, schedule_and_thread_locals) {
    {
        test::scheduler::queue<void*> sch_q;
        hce::scheduler* global_sch = &(hce::scheduler::global());
        std::unique_ptr<hce::scheduler::lifecycle> lf;
        auto sch = hce::scheduler::make(lf);
        std::thread thd([&]{ sch->install(); });

        try {
            sch->schedule(test::scheduler::co_scheduler_in_check(sch_q));
            sch->schedule(test::scheduler::co_scheduler_local_check(sch_q));
            sch->schedule(test::scheduler::co_scheduler_global_check(sch_q));

            EXPECT_NE(nullptr, sch_q.pop());

            hce::scheduler* recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_EQ(sch.get(), recv);
            EXPECT_NE(global_sch, recv);

            recv = (hce::scheduler*)(sch_q.pop());
            EXPECT_NE(sch.get(), recv);
            EXPECT_EQ(global_sch, recv);

            lf.reset();
            thd.join();
        } catch(const std::exception& e) {
            LOG_F(ERROR, e.what());
        }
    }
}
