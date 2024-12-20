#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <thread>
#include <condition_variable>

void measure_timer_resolution() {
    std::vector<std::chrono::nanoseconds> diffs;
    const unsigned long long iterations = 100000000;
    diffs.reserve(iterations);

    auto prev = std::chrono::steady_clock::now();

    for (size_t i = 0; i < iterations; ++i) {
        auto now = std::chrono::steady_clock::now();
        diffs.push_back(now - prev);
        prev = now;
    }

    // Analyze the differences
    auto min_res = *std::min_element(diffs.begin(), diffs.end());
    auto max_res = *std::max_element(diffs.begin(), diffs.end());

    std::cout << "Timer resolution (steady_clock):\n";
    std::cout << "  Min: " << min_res.count() << " ns\n";
    std::cout << "  Max: " << max_res.count() << " ns\n";
}

void measure_wakeup_overhead() {
    constexpr int sleep_duration_microseconds = 1000; // 1ms
    constexpr unsigned long long iterations = 1000;

    std::mutex mutex;
    std::condition_variable cv;

    std::vector<std::chrono::nanoseconds> overheads;

    for (size_t i = 0; i < iterations; ++i) {
        auto target_time = std::chrono::steady_clock::now() + std::chrono::microseconds(sleep_duration_microseconds);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_until(lock, target_time);
        }

        auto actual_time = std::chrono::steady_clock::now();
        overheads.push_back(actual_time - target_time);
    }

    // Analyze the overheads
    auto min_overhead = *std::min_element(overheads.begin(), overheads.end());
    auto max_overhead = *std::max_element(overheads.begin(), overheads.end());
    auto avg_overhead = std::chrono::nanoseconds(
        std::accumulate(overheads.begin(), overheads.end(), std::chrono::nanoseconds(0)).count() / iterations
    );

    std::cout << "Wake-Up Overhead:\n";
    std::cout << "  Min: " << min_overhead.count() << " ns\n";
    std::cout << "  Max: " << max_overhead.count() << " ns\n";
    std::cout << "  Avg: " << avg_overhead.count() << " ns\n";
}

int main() {
    measure_timer_resolution();
    measure_wakeup_overhead();
    return 0;
}
