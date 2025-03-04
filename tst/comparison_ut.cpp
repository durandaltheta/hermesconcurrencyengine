//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 
#include <thread>

#include "loguru.hpp"
#include "atomic.hpp"
#include "list.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
#include "threadpool.hpp"
#include "channel.hpp"

#include <gtest/gtest.h>  
#include "test_helpers.hpp"

namespace test {

void print_hardware_concurrency()
{
#if 0
    std::cout << "hardware_concurrency[" 
              << std::thread::hardware_concurrency() 
              << "]"
              << std::endl;
#endif
}

template <typename T>
std::string key_value_str(std::string key, std::string value)
{
    return std::move(key) + std::string("[") + std::move(value) + std::string("]");
}

template <typename T>
std::string key_value_str(std::string key, T value)
{
    return std::move(key) + std::string("[") + std::to_string(std::move(value)) + std::string("]");
}

template <typename F, typename... As>
void launch_core_multiplier_op(F&& f, size_t multiplier, As&&... as)
{
    size_t core_count = std::thread::hardware_concurrency();
    size_t concurrent_count = multiplier * core_count;

    std::cout << key_value_str("core count", core_count) 
              << ", " 
              << key_value_str("concurrent operation count", concurrent_count) 
              << std::endl;

    f(concurrent_count, std::forward<As>(as)...);
}

template <typename LOCK>
void system_thread_simple_communication_op(
        std::uint64_t thread_total, 
        std::uint64_t recv_total)
{
    struct ops {
        static inline void com0(
                hce::chan<int> ch0,
                hce::chan<int> ch1,
                std::uint64_t recv_total) {
            std::thread thd(ops::com1,ch0,ch1,recv_total);
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                ch0.send(recv);
                ch1.recv(message);
            }

            thd.join();
        }

        static inline void com1(
                hce::chan<int> ch0,
                hce::chan<int> ch1,
                std::uint64_t recv_total) {
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                ch0.recv(message);
                ch1.send(recv);
            }
        }
    };

    std::uint64_t repeat = thread_total/2;
    std::vector<std::thread> thds;
    thds.reserve(repeat);

    for(std::uint64_t c=0; c<repeat; ++c) {
        hce::chan<int> ch0, ch1;
        ch0.construct<LOCK>(0);
        ch1.construct<LOCK>(0);
        thds.push_back(std::thread(ops::com0,ch0,ch1,recv_total));
    }

    while(thds.size()) {
        thds.back().join();
        thds.pop_back();
    }
}

template <typename LOCK>
void concurrent_simple_communication_op(
        std::uint64_t thread_total, 
        std::uint64_t recv_total)
{
    struct ops {
        static hce::co<void> launcher(std::uint64_t thread_total, std::uint64_t recv_total)  {
            std::uint64_t repeat = thread_total/2;
            std::vector<hce::awt<void>> awts;
            awts.reserve(repeat);

            for(std::uint64_t c=0; c<repeat; ++c) {
                hce::chan<int> ch0, ch1;
                ch0.construct<LOCK>(0);
                ch1.construct<LOCK>(0);
                awts.push_back(hce::threadpool::schedule(ops::com0(ch0,ch1,recv_total)));
            }

            while(awts.size()) {
                co_await std::move(awts.back());
                awts.pop_back();
            }
        }

        static inline hce::co<void> com0(
                hce::chan<int> ch0,
                hce::chan<int> ch1,
                std::uint64_t recv_total) {
            auto awt = hce::schedule(ops::com1(ch0,ch1,recv_total));
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                co_await ch0.send(recv);
                co_await ch1.recv(message);
            }

            co_await std::move(awt);
        }

        static inline hce::co<void> com1(
                hce::chan<int> ch0,
                hce::chan<int> ch1,
                std::uint64_t recv_total) {
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                co_await ch0.recv(message);
                co_await ch1.send(recv);
            }
        }
    };

    hce::schedule(ops::launcher(thread_total, recv_total));
}

}

//------------------------------------------------------------------------------
// hce::lockfree 

// coroutine 

TEST(comparison_concurrent_simple_communication_over_lockfree_channel, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::lockfree>, 1, 10000);
}

TEST(comparison_concurrent_simple_communication_over_lockfree_channel, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::lockfree>, 2, 10000);
}

TEST(comparison_concurrent_simple_communication_over_lockfree_channel, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::lockfree>, 4, 10000);
}

TEST(comparison_concurrent_simple_communication_over_lockfree_channel, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::lockfree>, 8, 10000);
}

TEST(comparison_concurrent_simple_communication_over_lockfree_channel, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::lockfree>, 16, 10000);
}

//------------------------------------------------------------------------------
// hce::spinlock 

// coroutine

TEST(comparison_concurrent_simple_communication_over_spinlock_channel, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::spinlock>, 1, 10000);
}

TEST(comparison_concurrent_simple_communication_over_spinlock_channel, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::spinlock>, 2, 10000);
}

TEST(comparison_concurrent_simple_communication_over_spinlock_channel, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::spinlock>, 4, 10000);
}

TEST(comparison_concurrent_simple_communication_over_spinlock_channel, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::spinlock>, 8, 10000);
}

TEST(comparison_concurrent_simple_communication_over_spinlock_channel, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<hce::spinlock>, 16, 10000);
}

// thread

TEST(comparison_system_thread_simple_communication_over_spinlock_channel, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<hce::spinlock>, 1, 10000);
}

TEST(comparison_system_thread_simple_communication_over_spinlock_channel, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<hce::spinlock>, 2, 10000);
}

TEST(comparison_system_thread_simple_communication_over_spinlock_channel, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<hce::spinlock>, 4, 10000);
}

TEST(comparison_system_thread_simple_communication_over_spinlock_channel, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<hce::spinlock>, 8, 10000);
}

TEST(comparison_system_thread_simple_communication_over_spinlock_channel, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<hce::spinlock>, 16, 10000);
}

//------------------------------------------------------------------------------
// std::mutex

// coroutine 

TEST(comparison_concurrent_simple_communication_over_mutex_channel, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<std::mutex>, 1, 10000);
}

TEST(comparison_concurrent_simple_communication_over_mutex_channel, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<std::mutex>, 2, 10000);
}

TEST(comparison_concurrent_simple_communication_over_mutex_channel, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<std::mutex>, 4, 10000);
}

TEST(comparison_concurrent_simple_communication_over_mutex_channel, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<std::mutex>, 8, 10000);
}

TEST(comparison_concurrent_simple_communication_over_mutex_channel, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op<std::mutex>, 16, 10000);
}

// system thread

TEST(comparison_system_thread_simple_communication_over_mutex_channel, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<std::mutex>, 1, 10000);
}

TEST(comparison_system_thread_simple_communication_over_mutex_channel, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<std::mutex>, 2, 10000);
}

TEST(comparison_system_thread_simple_communication_over_mutex_channel, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<std::mutex>, 4, 10000);
}

TEST(comparison_system_thread_simple_communication_over_mutex_channel, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<std::mutex>, 8, 10000);
}

TEST(comparison_system_thread_simple_communication_over_mutex_channel, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine) {
    test::launch_core_multiplier_op(test::system_thread_simple_communication_op<std::mutex>, 16, 10000);
}
