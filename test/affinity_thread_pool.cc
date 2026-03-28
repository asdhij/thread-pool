/**
 * @file affinity_thread_pool.cc
 * @brief Unit tests for AffinityThreadPool and ThreadReference
 *
 * These tests validate basic lifecycle operations of the affinity-based
 * thread pool: starting a worker, obtaining a scoped `ThreadReference`,
 * enqueuing a task into the worker's thread-local queue, and requesting
 * a stop so the worker exits cleanly. Tests are intentionally small and
 * exercise the public API used by clients of the pool.
 *
 * @copyright Copyright (c) 2026 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-03-14
 */

import thread_pool.affinity_pool;
import thread_pool.task;
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>
#include <utility>

namespace {

// Constructing the pool should set the configured max thread count.
TEST(AffinityThreadPoolBasicTest, Construction) {
  thread_pool::AffinityThreadPool<> pool{3};
  EXPECT_EQ(pool.max_thread_count(), 3u);
}

// Starting slots: success, busy, and invalid-argument cases are exercised
// separately to keep each test focused on the `start()` behavior.
TEST(AffinityThreadPoolStartTest, StartErrors) {
  thread_pool::AffinityThreadPool<> pool{2};

  // Start slot 0 successfully.
  const auto ok = pool.start(0);
  EXPECT_TRUE(ok.has_value());

  // Starting the same slot again should report device_or_resource_busy.
  const auto busy = pool.start(0);
  EXPECT_FALSE(busy.has_value());
  EXPECT_EQ(busy.error().value(), std::to_underlying(std::errc::device_or_resource_busy));

  // Out-of-range start should report invalid_argument.
  const auto invalid = pool.start(42);
  EXPECT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().value(), std::to_underlying(std::errc::invalid_argument));

  // Teardown: request stop on the running slot so destructor can join cleanly.
  {
    thread_pool::AffinityThreadPool<>::thread_reference_type ref;
    for (int i = 0; i < 200 && !(ref = pool.get_thread_reference(0)); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
    EXPECT_TRUE(static_cast<bool>(ref));
    ref->notify_for_stop();
    EXPECT_TRUE(thread_pool::AffinityThreadPool<>::const_thread_reference_type{std::move(ref)}->empty());
  }
  pool.join_all_threads();
}

// Verify obtaining a thread reference after a worker starts, then release it.
TEST(AffinityThreadPoolRefTest, GetAndReleaseReference) {
  thread_pool::AffinityThreadPool<> pool{1};
  EXPECT_TRUE(pool.start(0).has_value());

  // Wait until ready and obtain a reference.
  thread_pool::AffinityThreadPool<>::thread_reference_type ref;
  for (int i = 0; i < 200 && !(ref = pool.get_thread_reference(0)); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
  EXPECT_TRUE(static_cast<bool>(ref));

  // Release reference and request stop so the worker can exit.
  ref->notify_for_stop();
  {
    thread_pool::AffinityThreadPool<>::const_thread_reference_type cref;
    cref = std::move(ref);
    cref = std::move(cref);
    EXPECT_NE(cref, nullptr);
    EXPECT_NE(cref, ref);
    EXPECT_NE(ref, cref);
  }
  EXPECT_EQ(ref, nullptr);
  pool.join_all_threads();
}

// Minimal enqueue test: start a worker, enqueue a single task and verify it runs.
TEST(AffinityThreadPoolEnqueueTest, EnqueueProcessed) {
  thread_pool::AffinityThreadPool<> pool{1};
  EXPECT_TRUE(pool.start(0).has_value());

  thread_pool::AffinityThreadPool<>::thread_reference_type ref;
  for (int i = 0; i < 200 && !(ref = pool.get_thread_reference(0)); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
  EXPECT_TRUE(static_cast<bool>(ref));

  std::atomic<int> counter{0};
  EXPECT_TRUE(ref->enqueue([&counter]() noexcept { ++counter; }));

  for (int i = 0; i < 200 && counter.load() == 0; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds{1}); }
  EXPECT_EQ(counter.load(), 1);

  ref->notify_for_stop();
  ref = nullptr;
  pool.join_all_threads();
}

// Out-of-range reference returns null.
TEST(AffinityThreadPoolRefTest, GetThreadReferenceOutOfRange) {
  const thread_pool::AffinityThreadPool<> pool{1};
  EXPECT_EQ(pool.get_thread_reference(42), nullptr);
}

}  // anonymous namespace