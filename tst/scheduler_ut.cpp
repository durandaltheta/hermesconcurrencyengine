//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <deque>
#include <thread>
#include <chrono>

#include "loguru.hpp"
#include "atomic.hpp"
#include "scheduler.hpp"

#include <gtest/gtest.h> 

namespace test {
namespace scheduler {

inline hce::co<void> co_void() { co_return; }

inline hce::co<bool> co_scheduler_in_check() { 
    co_return hce::scheduler::in(); 
}

inline hce::co<hce::scheduler*> co_scheduler_local_check() { 
    co_return &(hce::scheduler::local()); 
}

inline hce::co<hce::scheduler*> co_scheduler_global_check() { 
    co_return &(hce::scheduler::global()); 
}

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

    int pop() {
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
