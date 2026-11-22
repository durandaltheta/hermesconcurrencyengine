// SPDX-License-Identifier: Apache-2.0
// Author: Blayne Dennis 

#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <string>
#include <chrono>

#include "channel.hpp"
#include "scheduler.hpp"
#include "coroutine.hpp"
#include "logging.hpp"

#include <gtest/gtest.h>
#include "test_helpers.hpp"

// ---------------------------------------------------------------------------
// Basic construction / single-channel select
// ---------------------------------------------------------------------------

TEST(multiplex, single_channel_select) {
    hce::chan<int> ch = hce::chan<int>::make();
    int received = -1;
    std::atomic<int> calls{0};

    hce::multiplex mux{
        hce::multiplex::element{received, ch, [&]{
            calls.fetch_add(1, std::memory_order_relaxed);
        }}
    };

    // Send from another thread so the forwarder can complete
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ch.send(42);
    });

    bool ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ(42, received);
    EXPECT_EQ(1, calls.load());

    sender.join();

    // Clean shutdown
    mux.finish();
}

// ---------------------------------------------------------------------------
// Multiple channels – only the matching callback fires
// ---------------------------------------------------------------------------

TEST(multiplex, multi_channel_select_correct_callback) {
    hce::chan<int> a = hce::chan<int>::make();
    hce::chan<std::string> b = hce::chan<std::string>::make();
    hce::chan<double> c = hce::chan<double>::make();

    int iva = -1;
    std::string svb;
    double dvc = 0.0;

    std::atomic<int> calls_a{0}, calls_b{0}, calls_c{0};

    hce::multiplex mux{
        hce::multiplex::element{iva, a, [&] { calls_a.fetch_add(1); }},
        hce::multiplex::element{svb, b, [&] { calls_b.fetch_add(1); }},
        hce::multiplex::element{dvc, c, [&] { calls_c.fetch_add(1); }}
    };

    // Only send on channel b
    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        b.send(std::string("hello"));
    });

    bool ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ("hello", svb);
    EXPECT_EQ(0, calls_a.load());
    EXPECT_EQ(1, calls_b.load());
    EXPECT_EQ(0, calls_c.load());

    sender.join();
    mux.finish();
}

// ---------------------------------------------------------------------------
// Fairness / round-robin when several are ready
// ---------------------------------------------------------------------------

TEST(multiplex, round_robin_fairness) {
    constexpr int N = 4;
    std::vector<hce::chan<int>> chs;
    std::vector<int> values(N, -1);
    std::vector<std::atomic<int>> calls(N);
    for(auto& c : calls) c.store(0);

    std::vector<hce::multiplex::element> elements;
    for(int i = 0; i < N; ++i) {
        chs.emplace_back(hce::chan<int>::make());
        elements.emplace_back(values[i], chs.back(),
                              [&calls, i] { calls[i].fetch_add(1); });
    }

    hce::multiplex mux;
    for(auto& e : elements) mux.launch(std::move(e));

    // Make all channels ready
    for(int i = 0; i < N; ++i) {
        chs[i].send(100 + i);
    }

    // Perform N selects; each should fire a different callback (round-robin)
    std::vector<int> seen(N, 0);
    for(int k = 0; k < N; ++k) {
        bool ok = mux.select();
        EXPECT_TRUE(ok);
    }

    int total = 0;
    for(int i = 0; i < N; ++i) {
        int c = calls[i].load();
        EXPECT_EQ(1, c) << "channel " << i << " was not selected exactly once";
        total += c;
        EXPECT_EQ(100 + i, values[i]);
    }
    EXPECT_EQ(N, total);

    mux.finish();
}

// ---------------------------------------------------------------------------
// Close / done signalling
// ---------------------------------------------------------------------------

TEST(multiplex, channel_close_signals_done) {
    hce::chan<int> ch = hce::chan<int>::make();
    int val = 0;
    std::atomic<bool> cb_called{false};

    hce::multiplex mux{
        hce::multiplex::element{val, ch, [&] { cb_called = true; }}
    };

    ch.close();   // forwarder should see closed and send done=true

    bool ok = mux.select();
    // select returns true (received a result) but the callback must NOT fire
    EXPECT_TRUE(ok);
    EXPECT_FALSE(cb_called.load());

    mux.finish();
}

// ---------------------------------------------------------------------------
// finish() unregisters and makes further select() fail
// ---------------------------------------------------------------------------

TEST(multiplex, finish_prevents_further_select) {
    hce::chan<int> ch = hce::chan<int>::make();
    int val = 0;
    std::atomic<int> calls{0};

    hce::multiplex mux{
        hce::multiplex::element{val, ch, [&] { calls.fetch_add(1); }}
    };

    mux.finish();

    // After finish the internal received_ channel is closed
    bool ok = mux.select();
    EXPECT_FALSE(ok);
    EXPECT_EQ(0, calls.load());
}

// ---------------------------------------------------------------------------
// Adding more elements via launch() after construction
// ---------------------------------------------------------------------------

TEST(multiplex, launch_additional_elements) {
    hce::chan<int> a = hce::chan<int>::make();
    int va = -1;
    std::atomic<int> calls_a{0};

    hce::multiplex mux{
        hce::multiplex::element{va, a, [&] { calls_a.fetch_add(1); }}
    };

    hce::chan<int> b = hce::chan<int>::make();
    int vb = -1;
    std::atomic<int> calls_b{0};

    mux.launch(hce::multiplex::element{vb, b, [&] { calls_b.fetch_add(1); }});

    std::thread sender([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        b.send(99);
    });

    bool ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ(99, vb);
    EXPECT_EQ(0, calls_a.load());
    EXPECT_EQ(1, calls_b.load());

    sender.join();
    mux.finish();
}

// ---------------------------------------------------------------------------
// Concurrent senders – mutual exclusion via guard
// ---------------------------------------------------------------------------

TEST(multiplex, concurrent_senders_mutual_exclusion) {
    constexpr int N = 8;
    std::vector<hce::chan<int>> chs;
    std::vector<int> values(N, -1);
    std::atomic<int> total_calls{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current{0};

    std::vector<hce::multiplex::element> elements;
    for(int i = 0; i < N; ++i) {
        chs.emplace_back(hce::chan<int>::make());
        elements.emplace_back(values[i], chs.back(), [&] {
            int prev = current.fetch_add(1);
            max_concurrent.store(std::max(max_concurrent.load(), prev + 1));
            // tiny sleep to widen the race window
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            current.fetch_sub(1);
            total_calls.fetch_add(1);
        });
    }

    hce::multiplex mux;
    for(auto& e : elements) mux.launch(std::move(e));

    std::vector<std::thread> senders;
    for(int i = 0; i < N; ++i) {
        senders.emplace_back([&, i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            chs[i].send(i);
        });
    }

    for(int k = 0; k < N; ++k) {
        bool ok = mux.select();
        EXPECT_TRUE(ok);
    }

    for(auto& t : senders) {
        t.join();
    }

    EXPECT_EQ(N, total_calls.load());
    EXPECT_EQ(1, max_concurrent.load()) << "more than one callback ran concurrently";

    mux.finish();
}

// ---------------------------------------------------------------------------
// Buffered + unbuffered mix
// ---------------------------------------------------------------------------

TEST(multiplex, mixed_buffer_types) {
    hce::chan<int> unbuf = hce::chan<int>::make(0);          // unbuffered
    hce::chan<int> buf   = hce::chan<int>::make(4);          // buffered
    hce::chan<int> unlim = hce::chan<int>::make(-1);         // unlimited

    int vu = -1, vb = -1, vl = -1;
    std::atomic<int> cu{0}, cb{0}, cl{0};

    hce::multiplex mux{
        hce::multiplex::element{vu, unbuf, [&] { cu.fetch_add(1); }},
        hce::multiplex::element{vb, buf,   [&] { cb.fetch_add(1); }},
        hce::multiplex::element{vl, unlim, [&] { cl.fetch_add(1); }}
    };

    // Send on the buffered one first (it can accept without a receiver)
    buf.send(7);

    bool ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ(7, vb);
    EXPECT_EQ(1, cb.load());
    EXPECT_EQ(0, cu.load());
    EXPECT_EQ(0, cl.load());

    mux.finish();
}

// ---------------------------------------------------------------------------
// Empty multiplex
// ---------------------------------------------------------------------------

TEST(multiplex, empty_select_fails) {
    hce::multiplex mux;   // no elements

    // Close the internal channel so select returns false immediately
    // (the implementation does this inside finish, but we can also just
    // call finish which is the supported path).
    mux.finish();

    bool ok = mux.select();
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Re-select after a successful select (re-arm)
// ---------------------------------------------------------------------------

TEST(multiplex, reselect_after_success) {
    hce::chan<int> ch = hce::chan<int>::make();
    int val = -1;
    std::atomic<int> calls{0};

    hce::multiplex mux{
        hce::multiplex::element{val, ch, [&] { calls.fetch_add(1); }}
    };

    // First select
    std::thread s1([&] { ch.send(1); });
    bool ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ(1, val);
    EXPECT_EQ(1, calls.load());
    s1.join();

    // Second select (forwarder is still running / re-armed by unblock)
    std::thread s2([&] { ch.send(2); });
    ok = mux.select();
    EXPECT_TRUE(ok);
    EXPECT_EQ(2, val);
    EXPECT_EQ(2, calls.load());
    s2.join();

    mux.finish();
}
