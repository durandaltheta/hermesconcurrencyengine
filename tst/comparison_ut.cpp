//SPDX-License-Identifier: Apache-2.0
//Author: Blayne Dennis 

#include "loguru.hpp"
#include "atomic.hpp"
#include "coroutine.hpp"
#include "scheduler.hpp"
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

void concurrent_simple_communication_op(std::uint64_t thread_total, 
                                std::uint64_t recv_total)
{
    struct ops {
        static inline hce::co<void> t0(
                hce::channel<int> ch0,
                hce::channel<int> ch1,
                std::uint64_t recv_total,
                hce::channel<int> done_ch) {
            hce::schedule(ops::t1(ch0,ch1,recv_total,done_ch));
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                co_await ch0.send(recv);
                co_await ch1.recv(message);
            }
        }

        static inline hce::co<void> t1(
                hce::channel<int> ch0,
                hce::channel<int> ch1,
                std::uint64_t recv_total,
                hce::channel<int> done_ch) {
            int message;

            for(size_t recv=0; recv<recv_total; ++recv) {
                co_await ch0.recv(message);
                co_await ch1.send(recv);
            }

            co_await done_ch.send(0);
        }
    };

    hce::channel<int> done_ch;
    done_ch.construct(std::thread::hardware_concurrency());

    std::uint64_t repeat = thread_total/2;

    for(std::uint64_t c=0; c<repeat; ++c) {
        hce::channel<int> ch0, ch1;
        ch0.construct();
        ch1.construct();
        hce::schedule(ops::t0(ch0,ch1,recv_total,done_ch));
    }

    int x; // temp val
    for(std::uint64_t c=0; c<repeat; ++c) { done_ch.recv(x); }
}

}

TEST(comparison_concurrent_simple_communication, 1x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine_over_channel)
{
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op, 1, 10000);
}

TEST(comparison_concurrent_simple_communication, 2x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine_over_channel)
{
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op, 2, 10000);
}

TEST(comparison_concurrent_simple_communication, 4x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine_over_channel)
{
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op, 4, 10000);
}

TEST(comparison_concurrent_simple_communication, 8x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine_over_channel)
{
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op, 8, 10000);
}

TEST(comparison_concurrent_simple_communication, 16x_core_count_coroutines_communicating_in_pairs_10000_msgs_sent_per_coroutine_over_channel)
{
    test::launch_core_multiplier_op(test::concurrent_simple_communication_op, 16, 10000);
}
