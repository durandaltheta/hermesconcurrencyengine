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
#include "test_helpers.hpp"

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

template <typename T>
inline void co_return_value_T() {
    {
        T t = test::init<T>(3);
        hce::co<T> co = test::coroutine::co(&t);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ((T)test::init<T>(3), *(hce::get_promise(co).result));
        EXPECT_NE(&t, hce::get_promise(co).result.get());
    }

    // type erased
    {
        T t = test::init<T>(3);
        hce::coroutine co = test::coroutine::co(&t);
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
        EXPECT_EQ((T)test::init<T>(3), *(static_cast<typename hce::co<T>::promise_type&>(hce::get_promise(co)).result));
        EXPECT_NE(&t, static_cast<typename hce::co<T>::promise_type&>(hce::get_promise(co)).result.get());
    }
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
    co = hce::co<void>(std::move(hdl));
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
    {
        hce::co<void> co = test::coroutine::co_void();
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
    }

    // type erased
    {
        hce::coroutine co = test::coroutine::co_void();
        EXPECT_TRUE(co);
        EXPECT_FALSE(co.done());
        co.resume();
        EXPECT_TRUE(co.done());
    }
}

TEST(coroutine, co_return_value) {
    test::coroutine::co_return_value_T<int>();
    test::coroutine::co_return_value_T<unsigned int>();
    test::coroutine::co_return_value_T<size_t>();
    test::coroutine::co_return_value_T<float>();
    test::coroutine::co_return_value_T<double>();
    test::coroutine::co_return_value_T<char>();
    test::coroutine::co_return_value_T<void*>();
    test::coroutine::co_return_value_T<std::string>();
    test::coroutine::co_return_value_T<test::CustomObject>();
}

TEST(coroutine, co_await_void) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<void>::interface>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready) : 
                hce::awaitable::lockable<
                    hce::spinlock,
                    hce::awt<void>::interface>(
                        lk_,
                        hce::awaitable::await::policy::defer,
                        hce::awaitable::resume::policy::lock),
                hdl_(hdl),
                ready_(ready)
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                for(auto& a : as) {
                    hce::awt<void> awt = hce::awt<void>(a);
                    EXPECT_TRUE(awt.valid());
                    co_await std::move(awt);
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

            inline void on_suspend() { }

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

        co = hce::coroutine(std::move(hdl));
        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}

TEST(coroutine, co_await_int) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<int>::interface>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready, int i) : 
                hce::awaitable::lockable<
                    hce::spinlock,
                    hce::awt<int>::interface>(
                        lk_,
                        hce::awaitable::await::policy::defer,
                        hce::awaitable::resume::policy::lock),
                hdl_(hdl),
                ready_(ready),
                i_(i)
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                int i=0;
                for(auto& a : as) {
                    hce::awt<int> awt = hce::awt<int>(a);
                    EXPECT_TRUE(awt.valid());
                    int result_i = co_await std::move(awt);
                    EXPECT_EQ(i, result_i);
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

            inline void on_suspend() { }

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

        co = hce::coroutine(std::move(hdl));
        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}

TEST(coroutine, co_await_string) {
    {
        struct ai : 
            public hce::awaitable::lockable<
                hce::spinlock,
                hce::awt<std::string>::interface>
        {
            ai(std::coroutine_handle<>* hdl, bool* ready, int i) : 
                hce::awaitable::lockable<
                    hce::spinlock,
                    hce::awt<std::string>::interface>(
                        lk_,
                        hce::awaitable::await::policy::defer,
                        hce::awaitable::resume::policy::lock),
                hdl_(hdl),
                ready_(ready),
                s_(std::to_string(i))
            { }

            static inline hce::co<void>op1(std::vector<ai*> as) {
                int i=0;
                for(auto& a : as) {
                    hce::awt<std::string> awt = hce::awt<std::string>(a);
                    EXPECT_TRUE(awt.valid());
                    std::string result_i = co_await std::move(awt);
                    EXPECT_EQ(std::to_string(i), result_i);
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

            inline void on_suspend() { }

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

        co = hce::coroutine(std::move(hdl));
        co.resume();

        EXPECT_EQ(nullptr, hdl.address());
        EXPECT_TRUE(co);
        EXPECT_TRUE(co.done());
        EXPECT_FALSE(flag);
    }
}

namespace test {
namespace coroutine {

template <typename T>
hce::co<void> co_yield_void_and_return_void(T& t, T value) {
    co_await hce::yield<void>();
    t = value;
    co_return;
}

template <typename T>
hce::co<void> co_yield_T_and_return_void(T& t, T value) {
    t = co_await hce::yield<T>(value);
    co_return;
}

template <typename T>
void co_await_yield_T(const int init) {
    // yield void
    {
        const int value = init + 1;
        T t = test::init<T>(init);
        hce::co<void> co(
            test::coroutine::co_yield_void_and_return_void(t,(T)test::init<T>(value)));

        // expect unrun state
        EXPECT_FALSE(co.done());
        EXPECT_EQ((T)test::init<T>(init),t);
        co.resume();

        // expect yielded state
        EXPECT_FALSE(co.done());
        EXPECT_EQ((T)test::init<T>(init),t);
        co.resume();

        // expect completed state
        EXPECT_TRUE(co.done());
        EXPECT_EQ((T)test::init<T>(value),t);
    }

    // yield T
    {
        const int value = init + 1;
        T i = test::init<T>(init);
        hce::co<void> co(
            test::coroutine::co_yield_T_and_return_void(i,(T)test::init<T>(value)));

        // expect unrun state
        EXPECT_FALSE(co.done());
        EXPECT_EQ((T)test::init<T>(init),i);
        co.resume();

        // expect yielded state
        EXPECT_FALSE(co.done());
        EXPECT_EQ((T)test::init<T>(init),i);
        co.resume();

        // expect completed state
        EXPECT_TRUE(co.done());
        EXPECT_EQ((T)test::init<T>(value),i);
    }
}

}
}

TEST(coroutine, co_await_yield) {
    for(int i=0; i<5; ++i) {
        test::coroutine::co_await_yield_T<int>(i);
        test::coroutine::co_await_yield_T<unsigned int>(i);
        test::coroutine::co_await_yield_T<size_t>(i);
        test::coroutine::co_await_yield_T<float>(i);
        test::coroutine::co_await_yield_T<double>(i);
        test::coroutine::co_await_yield_T<char>(i);
        test::coroutine::co_await_yield_T<void*>(i);
        test::coroutine::co_await_yield_T<std::string>(i);
        test::coroutine::co_await_yield_T<test::CustomObject>(i);
    }
}
