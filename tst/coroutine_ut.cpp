//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"

#include <string>
#include <sstream>
#include <vector>
#include <exception>

#include <gtest/gtest.h>  

namespace test {
namespace coroutine {

inline hce::co<void> co_void() {
    co_return;
}

template <typename T>
inline hce::co<T> co(T* t) {
    co_return *t;
}

template <typename T>
inline hce::co<T*> co_ptr(T* t) {
    co_return t;
}

}
}

TEST(coroutine, address) {
    hce::co<void> co;
    EXPECT_EQ(co.address(), co.address());
    EXPECT_EQ(nullptr, co.address());
    co = test::coroutine::co_void();
    EXPECT_EQ(co.address(), co.address());
    EXPECT_NE(nullptr, co.address());
    void* old_addr = co.address();
    co = test::coroutine::co_void();
    EXPECT_NE(old_addr, co.address());
}

TEST(coroutine, release) {
    hce::co<void> co = test::coroutine::co_void();
    EXPECT_TRUE(co);
    std::coroutine_handle<> hdl = co.release();
    EXPECT_FALSE(co);
    co = hce::co<void>(hdl);
    EXPECT_TRUE(co);
}

TEST(coroutine, reset) {
    {
        hce::co<void> co = test::coroutine::co_void();
        EXPECT_TRUE(co);
        co.reset();
        EXPECT_FALSE(co);
    }

    {
        hce::co<void> co = test::coroutine::co_void();
        hce::co<void> co2 = test::coroutine::co_void();
        EXPECT_TRUE(co);
        EXPECT_TRUE(co2);
        co.reset(co2.release());
        EXPECT_TRUE(co);
        EXPECT_FALSE(co2);
    }
}

TEST(coroutine, swap) {
    hce::co<void> co = test::coroutine::co_void();
    hce::co<void> co2 = test::coroutine::co_void();
    EXPECT_TRUE(co);
    EXPECT_TRUE(co2);

    void* co_addr = co.address();
    void* co2_addr = co2.address();

    co.swap(co2);

    EXPECT_NE(co_addr, co.address());
    EXPECT_EQ(co2_addr, co.address());
    EXPECT_NE(co2_addr, co2.address());
    EXPECT_EQ(co_addr, co2.address());

    co.swap(co2);

    EXPECT_EQ(co_addr, co.address());
    EXPECT_NE(co2_addr, co.address());
    EXPECT_EQ(co2_addr, co2.address());
    EXPECT_NE(co_addr, co2.address());
}

TEST(coroutine, co_return_void) {
    hce::co<void> co = test::coroutine::co_void();
    EXPECT_TRUE(co);
    EXPECT_FALSE(co.done());
    co.resume();
    EXPECT_TRUE(co.done());
}

TEST(coroutine, co_return_value) {
    {
        int i = 3;
        hce::co<int> co = test::coroutine::co(&i);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ(3, *(co.promise().result));
        EXPECT_NE(&i, co.promise().result.get());
    }

    {
        std::string s = "3";
        hce::co<std::string> co = test::coroutine::co(&s);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ("3", *(co.promise().result));
        EXPECT_NE(&s, co.promise().result.get());
    }
}

TEST(coroutine, co_return_value_erased) {
    {
        int i = 3;
        hce::coroutine co = test::coroutine::co(&i);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ(3, *(co.to_promise<hce::co<int>>().result));
        EXPECT_NE(&i, co.to_promise<hce::co<int>>().result.get());
    }
    {
        int i = 3;
        hce::coroutine co = test::coroutine::co_ptr(&i);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ(3, **(co.to_promise<hce::co<int*>>().result));
        EXPECT_EQ(&i, *(co.to_promise<hce::co<int*>>().result));
    }

    {
        std::string s = "3";
        hce::coroutine co = test::coroutine::co(&s);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ("3", *(co.to_promise<hce::co<std::string>>().result));
        EXPECT_NE(&s, co.to_promise<hce::co<std::string>>().result.get());
    }

    {
        std::string s = "3";
        hce::coroutine co = test::coroutine::co_ptr(&s);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ("3", **(co.to_promise<hce::co<std::string*>>().result));
        EXPECT_EQ(&s, *(co.to_promise<hce::co<std::string*>>().result));
    }
}

TEST(coroutine, co_await_void) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::awt<void>::interface,
                hce::spinlock>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready) : 
                hce::awaitable::lockable<
                    hce::awt<void>::interface,
                    hce::spinlock>(lk_,false),
                hdl_(hdl),
                ready_(ready)
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                for(auto& a : as) {
                    hce::awt<void> awt = hce::awt<void>::make(a);
                    EXPECT_NE(nullptr, awt.address());
                    co_await std::move(awt);
                    EXPECT_EQ(nullptr, awt.address());
                }
            }

            inline bool on_ready() { 
                if(*ready_) {
                    *ready_ = false;
                    return true;
                } else {
                    return false;
                }
            }

            inline void on_resume(void* m) { }

            inline void destination(std::coroutine_handle<> hdl) { 
                *hdl_ = hdl; 
            }

        private:
            hce::spinlock lk_;
            std::coroutine_handle<>* hdl_;
            bool* ready_;
        };

        std::coroutine_handle<> hdl;
        bool flag=true;
        hce::co<void> co;
        std::vector<ai*> as;

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);

        as.emplace_back(new ai(&hdl, &flag));
        as.emplace_back(new ai(&hdl, &flag));
        co = ai::op1(as);

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        EXPECT_TRUE(flag);

        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        as[1]->resume(nullptr);

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        co = hce::coroutine(hdl);
        co.resume();

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}

TEST(coroutine, co_await_int) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::awt<int>::interface,
                hce::spinlock>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready, int i) : 
                hce::awaitable::lockable<
                    hce::awt<int>::interface,
                    hce::spinlock>(lk_,false),
                hdl_(hdl),
                ready_(ready),
                i_(i)
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                int i=0;
                for(auto& a : as) {
                    hce::awt<int> awt = hce::awt<int>::make(a);
                    EXPECT_NE(nullptr, awt.address());
                    int result_i = co_await std::move(awt);
                    EXPECT_EQ(i, result_i);
                    EXPECT_EQ(nullptr, awt.address());
                    ++i;
                }
            }

            inline bool on_ready() { 
                if(*ready_) {
                    *ready_ = false;
                    return true;
                } else {
                    return false;
                }
            }

            inline void on_resume(void* m) { }
            inline int get_result() { return i_; }

            inline void destination(std::coroutine_handle<> hdl) { 
                *hdl_ = hdl; 
            }

        private:
            hce::spinlock lk_;
            std::coroutine_handle<>* hdl_;
            bool* ready_;
            int i_;
        };

        std::coroutine_handle<> hdl;
        bool flag=true;
        hce::co<void> co;
        std::vector<ai*> as;
        int i=0;

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);

        as.emplace_back(new ai(&hdl, &flag, i++));
        as.emplace_back(new ai(&hdl, &flag, i++));
        co = ai::op1(as);

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        EXPECT_TRUE(flag);

        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        as[1]->resume(nullptr);

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        co = hce::coroutine(hdl);
        co.resume();

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}

TEST(coroutine, co_await_string) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::awt<std::string>::interface,
                hce::spinlock>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready, int i) : 
                hce::awaitable::lockable<
                    hce::awt<std::string>::interface,
                    hce::spinlock>(lk_,false),
                hdl_(hdl),
                ready_(ready),
                s_(std::to_string(i))
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                int i=0;
                for(auto& a : as) {
                    hce::awt<std::string> awt = hce::awt<std::string>::make(a);
                    EXPECT_NE(nullptr,awt.address());
                    std::string result_i = co_await std::move(awt);
                    EXPECT_EQ(std::to_string(i), result_i);
                    EXPECT_EQ(nullptr,awt.address());
                    ++i;
                }
            }

            inline bool on_ready() { 
                if(*ready_) {
                    *ready_ = false;
                    return true;
                } else {
                    return false;
                }
            }

            inline void on_resume(void* m) { }
            inline std::string get_result() { return s_; }

            inline void destination(std::coroutine_handle<> hdl) { 
                *hdl_ = hdl; 
            }

        private:
            hce::spinlock lk_;
            std::coroutine_handle<>* hdl_;
            bool* ready_;
            std::string s_;
        };

        std::coroutine_handle<> hdl;
        bool flag=true;
        hce::co<void> co;
        std::vector<ai*> as;
        int i=0;

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);

        as.emplace_back(new ai(&hdl, &flag, i++));
        as.emplace_back(new ai(&hdl, &flag, i++));
        co = ai::op1(as);

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        EXPECT_TRUE(flag);

        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        as[1]->resume(nullptr);

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_FALSE(co);
        EXPECT_FALSE(flag);

        co = hce::coroutine(hdl);
        co.resume();

        EXPECT_NE(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}
