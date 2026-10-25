//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#include "synchronization.hpp"

#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

// Helper: join an awaitable by letting it go out of scope (blocks until complete)
template <typename Awt>
void join(Awt awt) {
    // destruction joins
}

// Helper to time an operation roughly
template <typename F>
auto time_call(F&& f) {
    auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::steady_clock::now() - start;
}

} // namespace

// ============================================================================
// mutex::lock
// ============================================================================

TEST(synchronization, mutex_lock_immediate) {
    std::mutex mtx;

    // Fast path: unlocked, try_lock succeeds, ready awaitable
    {
        auto awt = hce::mutex::lock(mtx);
        join(std::move(awt));
        // lock is held by this thread
        EXPECT_FALSE(mtx.try_lock());
        mtx.unlock();
    }

    // Confirm we can lock again
    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(synchronization, mutex_lock_contended) {
    std::mutex mtx;
    mtx.lock(); // hold it

    std::atomic<bool> acquired{false};
    std::thread releaser([&] {
        std::this_thread::sleep_for(5ms);
        mtx.unlock();
    });

    auto start = std::chrono::steady_clock::now();
    {
        auto awt = hce::mutex::lock(mtx); // should block until unlock
        join(std::move(awt));
        acquired = true;
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    releaser.join();
    EXPECT_TRUE(acquired);
    EXPECT_GE(elapsed, 4ms); // waited for the releaser
    EXPECT_FALSE(mtx.try_lock()); // we hold it
    mtx.unlock();
}

// ============================================================================
// unique_lock::lock
// ============================================================================

TEST(synchronization, unique_lock_lock_immediate) {
    std::mutex mtx;
    std::unique_lock<std::mutex> ul(mtx, std::defer_lock);

    {
        auto awt = hce::unique_lock::lock(ul);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
    }
    ul.unlock();
}

TEST(synchronization, unique_lock_lock_contended) {
    std::mutex mtx;
    mtx.lock();

    std::unique_lock<std::mutex> ul(mtx, std::defer_lock);

    std::thread releaser([&] {
        std::this_thread::sleep_for(5ms);
        mtx.unlock();
    });

    {
        auto awt = hce::unique_lock::lock(ul);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
    }

    releaser.join();
    ul.unlock();
}

// ============================================================================
// unique_lock::construct
// ============================================================================

TEST(synchronization, unique_lock_construct_immediate) {
    std::mutex mtx;
    std::unique_lock<std::mutex> ul; // empty

    {
        auto awt = hce::unique_lock::construct(ul, mtx);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
        EXPECT_EQ(ul.mutex(), &mtx);
    }
    ul.unlock();
}

TEST(synchronization, unique_lock_construct_contended) {
    std::mutex mtx;
    mtx.lock();

    std::unique_lock<std::mutex> ul;

    std::thread releaser([&] {
        std::this_thread::sleep_for(5ms);
        mtx.unlock();
    });

    {
        auto awt = hce::unique_lock::construct(ul, mtx);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
    }

    releaser.join();
    ul.unlock();
}

// ============================================================================
// unique_lock::try_lock_for / try_lock_until
// ============================================================================

TEST(synchronization, unique_lock_try_lock_for_success) {
    std::timed_mutex mtx;
    mtx.lock();

    std::unique_lock<std::timed_mutex> ul(mtx, std::defer_lock);

    std::thread releaser([&] {
        std::this_thread::sleep_for(3ms);
        mtx.unlock();
    });

    {
        auto awt = hce::unique_lock::try_lock_for(ul, 50ms);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
    }

    releaser.join();
    ul.unlock();
}

TEST(synchronization, unique_lock_try_lock_for_timeout) {
    std::timed_mutex mtx;
    mtx.lock(); // never released in time

    std::unique_lock<std::timed_mutex> ul(mtx, std::defer_lock);

    auto elapsed = time_call([&] {
        auto awt = hce::unique_lock::try_lock_for(ul, 8ms);
        join(std::move(awt));
    });

    EXPECT_FALSE(ul.owns_lock());
    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 50ms); // didn't hang

    mtx.unlock();
}

TEST(synchronization, unique_lock_try_lock_until_success) {
    std::timed_mutex mtx;
    mtx.lock();

    std::unique_lock<std::timed_mutex> ul(mtx, std::defer_lock);

    std::thread releaser([&] {
        std::this_thread::sleep_for(3ms);
        mtx.unlock();
    });

    auto deadline = std::chrono::steady_clock::now() + 50ms;
    {
        auto awt = hce::unique_lock::try_lock_until(ul, deadline);
        join(std::move(awt));
        EXPECT_TRUE(ul.owns_lock());
    }

    releaser.join();
    ul.unlock();
}

TEST(synchronization, unique_lock_try_lock_until_timeout) {
    std::timed_mutex mtx;
    mtx.lock();

    std::unique_lock<std::timed_mutex> ul(mtx, std::defer_lock);

    auto deadline = std::chrono::steady_clock::now() + 8ms;
    auto elapsed = time_call([&] {
        auto awt = hce::unique_lock::try_lock_until(ul, deadline);
        join(std::move(awt));
    });

    EXPECT_FALSE(ul.owns_lock());
    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 50ms);

    mtx.unlock();
}

// ============================================================================
// condition_variable::wait (no timeout)
// ============================================================================

TEST(synchronization, cv_wait) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);

    std::thread notifier([&] {
        std::this_thread::sleep_for(5ms);
        cv.notify_one();
    });

    {
        auto awt = hce::condition_variable::wait(cv, lk);
        join(std::move(awt));
        // still owns the lock after wait returns
        EXPECT_TRUE(lk.owns_lock());
    }

    notifier.join();
}

TEST(synchronization, cv_wait_predicate) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);
    std::atomic<bool> ready{false};

    std::thread notifier([&] {
        std::this_thread::sleep_for(5ms);
        ready = true;
        cv.notify_one();
    });

    {
        auto awt = hce::condition_variable::wait(cv, lk, [&] { return ready.load(); });
        join(std::move(awt));
        EXPECT_TRUE(ready);
        EXPECT_TRUE(lk.owns_lock());
    }

    notifier.join();
}

// ============================================================================
// condition_variable::wait_for
// ============================================================================

TEST(synchronization, cv_wait_for_notified) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);

    std::thread notifier([&] {
        std::this_thread::sleep_for(3ms);
        cv.notify_one();
    });

    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_for(cv, lk, 50ms);
        join(std::move(awt));
    });

    notifier.join();
    EXPECT_LT(elapsed, 30ms); // notified early
    EXPECT_TRUE(lk.owns_lock());
}

TEST(synchronization, cv_wait_for_timeout) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);

    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_for(cv, lk, 8ms);
        join(std::move(awt));
    });

    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 40ms);
    EXPECT_TRUE(lk.owns_lock());
}

TEST(synchronization, cv_wait_for_predicate_notified) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);
    std::atomic<bool> ready{false};

    std::thread notifier([&] {
        std::this_thread::sleep_for(3ms);
        ready = true;
        cv.notify_one();
    });

    {
        auto awt = hce::condition_variable::wait_for(cv, lk, 50ms, [&] { return ready.load(); });
        join(std::move(awt));
        EXPECT_TRUE(ready);
    }

    notifier.join();
}

TEST(synchronization, cv_wait_for_predicate_timeout) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);
    std::atomic<bool> ready{false};

    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_for(cv, lk, 8ms, [&] { return ready.load(); });
        join(std::move(awt));
    });

    EXPECT_FALSE(ready);
    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 40ms);
}

// ============================================================================
// condition_variable::wait_until
// ============================================================================

TEST(synchronization, cv_wait_until_notified) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);

    std::thread notifier([&] {
        std::this_thread::sleep_for(3ms);
        cv.notify_one();
    });

    auto deadline = std::chrono::steady_clock::now() + 50ms;
    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_until(cv, lk, deadline);
        join(std::move(awt));
    });

    notifier.join();
    EXPECT_LT(elapsed, 30ms);
}

TEST(synchronization, cv_wait_until_timeout) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);

    auto deadline = std::chrono::steady_clock::now() + 8ms;
    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_until(cv, lk, deadline);
        join(std::move(awt));
    });

    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 40ms);
}

TEST(synchronization, cv_wait_until_predicate_notified) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);
    std::atomic<bool> ready{false};

    std::thread notifier([&] {
        std::this_thread::sleep_for(3ms);
        ready = true;
        cv.notify_one();
    });

    auto deadline = std::chrono::steady_clock::now() + 50ms;
    {
        auto awt = hce::condition_variable::wait_until(cv, lk, deadline, [&] { return ready.load(); });
        join(std::move(awt));
        EXPECT_TRUE(ready);
    }

    notifier.join();
}

TEST(synchronization, cv_wait_until_predicate_timeout) {
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(mtx);
    std::atomic<bool> ready{false};

    auto deadline = std::chrono::steady_clock::now() + 8ms;
    auto elapsed = time_call([&] {
        auto awt = hce::condition_variable::wait_until(cv, lk, deadline, [&] { return ready.load(); });
        join(std::move(awt));
    });

    EXPECT_FALSE(ready);
    EXPECT_GE(elapsed, 6ms);
    EXPECT_LT(elapsed, 40ms);
}

// ============================================================================
// Additional coverage: spinlock as LOCKABLE (if interface matches)
// ============================================================================

TEST(synchronization, mutex_lock_with_spinlock) {
    hce::spinlock slk;

    {
        auto awt = hce::mutex::lock(slk);
        join(std::move(awt));
        EXPECT_FALSE(slk.try_lock());
        slk.unlock();
    }
}
