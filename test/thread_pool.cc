/**
 * @file thread_pool.cc
 * @brief Main test file for thread pool
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-27
 */

import thread_pool;
import thread_pool.task;
import thread_pool.queue;
import thread_pool.policy;

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace {

// Simple task type for testing
struct SimpleTask {
  std::atomic<int>* counter;

  SimpleTask() noexcept : counter(nullptr) {}
  SimpleTask(std::atomic<int>& c) noexcept : counter(&c) {}

  void operator()() noexcept {
    if (counter) { counter->fetch_add(1, std::memory_order_relaxed); }
  }
};

// Verify SimpleTask satisfies task concept
static_assert(thread_pool::task<SimpleTask>, "SimpleTask should satisfy task concept");

// Delayed task for testing concurrency
struct DelayedTask {
  std::atomic<int>* counter;
  std::chrono::milliseconds delay;

  DelayedTask() noexcept = default;
  DelayedTask(std::atomic<int>* c, std::chrono::milliseconds d) noexcept : counter(c), delay(d) {}

  void operator()() noexcept {
    std::this_thread::sleep_for(delay);
    if (counter) { counter->fetch_add(1, std::memory_order_relaxed); }
  }
};

// Test policy for dependency injection verification
struct TestPolicy {
  std::atomic<int>* thread_start_count_;
  std::atomic<int>* thread_exit_count_;
  std::atomic<int>* pool_stop_count_;
  std::atomic<int>* pool_shutdown_count_;
  std::atomic<int>* pool_destroy_count_;

  TestPolicy(std::atomic<int>& thread_start_count, std::atomic<int>& thread_exit_count, std::atomic<int>& pool_stop_count, std::atomic<int>& pool_shutdown_count,
             std::atomic<int>& pool_destroy_count) noexcept :
      thread_start_count_(&thread_start_count),
      thread_exit_count_(&thread_exit_count),
      pool_stop_count_(&pool_stop_count),
      pool_shutdown_count_(&pool_shutdown_count),
      pool_destroy_count_(&pool_destroy_count) {}

  void on_thread_start() noexcept { thread_start_count_->fetch_add(1); }
  void on_thread_exit() noexcept { thread_exit_count_->fetch_add(1); }
  void on_pool_stop() { pool_stop_count_->fetch_add(1); }
  void on_pool_shutdown() { pool_shutdown_count_->fetch_add(1); }
  void on_pool_destroy() noexcept { pool_destroy_count_->fetch_add(1); }
};

// Verify TestPolicy satisfies policy requirements
static_assert(thread_pool::Policy::has_on_thread_start<TestPolicy>);
static_assert(thread_pool::Policy::has_on_thread_exit<TestPolicy>);
static_assert(thread_pool::Policy::has_on_pool_stop<TestPolicy>);
static_assert(thread_pool::Policy::has_on_pool_shutdown<TestPolicy>);
static_assert(thread_pool::Policy::has_on_pool_destroy<TestPolicy>);

// Basic functionality tests
template <thread_pool::task Task>
class ThreadPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_EQ(pool_.thread_count(), 0);
    EXPECT_TRUE(pool_.task_queue().empty());
    EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
  }

  void TearDown() override {
    EXPECT_EQ(pool_destroy_count_, 0);
    EXPECT_GE(pool_shutdown_count_, pool_destroy_count_);
  }

  std::atomic<int> task_counter_{0};
  std::atomic<int> thread_start_count_{0};
  std::atomic<int> thread_exit_count_{0};
  std::atomic<int> pool_stop_count_{0};
  std::atomic<int> pool_shutdown_count_{0};
  std::atomic<int> pool_destroy_count_{0};
  thread_pool::ThreadPool<Task, thread_pool::DefaultQueue<Task>, TestPolicy> pool_{
    TestPolicy{thread_start_count_, thread_exit_count_, pool_stop_count_, pool_shutdown_count_, pool_destroy_count_}
  };
};

using SimpleTaskPoolTest = ThreadPoolTest<SimpleTask>;
using DelayedTaskPoolTest = ThreadPoolTest<DelayedTask>;

TEST_F(SimpleTaskPoolTest, ThreadAdjustment) {
  // Increase threads
  auto result = pool_.set_thread_count(4);
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value(), 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(pool_.thread_count(), 4);

  // Decrease threads
  result = pool_.set_thread_count(2);
  EXPECT_TRUE(result);
  EXPECT_EQ(result.value(), 4);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_EQ(pool_.thread_count(), 2);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

// Lifecycle tests
TEST(ThreadPoolLifecycleTest, PolicyInjection) {
  std::atomic<int> task_counter_{0};
  std::atomic<int> thread_start_count_{0};
  std::atomic<int> thread_exit_count_{0};
  std::atomic<int> pool_stop_count_{0};
  std::atomic<int> pool_shutdown_count_{0};
  std::atomic<int> pool_destroy_count_{0};
  thread_pool::ThreadPool<SimpleTask, thread_pool::DefaultQueue<SimpleTask>, TestPolicy> pool(
    TestPolicy{thread_start_count_, thread_exit_count_, pool_stop_count_, pool_shutdown_count_, pool_destroy_count_});

  EXPECT_TRUE(pool.set_thread_count(2));

  for (int i = 0; i < 4; ++i) { EXPECT_TRUE(pool.submit(task_counter_)); }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(pool.thread_count(), 2);

  // Stop pool to trigger policy callbacks
  EXPECT_EQ(pool.stop().status(), thread_pool::ThreadPoolStatus::Stopping);
  EXPECT_EQ(pool.shutdown().status(), thread_pool::ThreadPoolStatus::Stopped);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_TRUE(pool.task_queue().empty());
  EXPECT_EQ(task_counter_, 4);
  EXPECT_EQ(thread_start_count_, 2);
  EXPECT_EQ(thread_exit_count_, 2);
  EXPECT_EQ(pool_stop_count_, 1);
  EXPECT_EQ(pool_shutdown_count_, 1);
  EXPECT_EQ(pool_destroy_count_, 0);
}

TEST_F(SimpleTaskPoolTest, SingleTaskSubmission) {
  // Submit single task
  EXPECT_TRUE(pool_.set_thread_count(1));
  EXPECT_TRUE(pool_.submit(task_counter_));

  // Wait for task completion
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(pool_.thread_count(), 1);
  EXPECT_TRUE(pool_.task_queue().empty());
  EXPECT_EQ(task_counter_, 1);
  EXPECT_EQ(thread_start_count_, 1);
  EXPECT_EQ(thread_exit_count_, 0);
  EXPECT_EQ(pool_stop_count_, 0);
  EXPECT_EQ(pool_shutdown_count_, 0);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

TEST_F(SimpleTaskPoolTest, MultipleTaskSubmission) {
  constexpr int num_tasks = 10;
  EXPECT_TRUE(pool_.set_thread_count(1));

  // Submit multiple tasks
  for (int i = 0; i < num_tasks; ++i) { EXPECT_TRUE(pool_.submit(task_counter_)); }

  // Wait for all tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(pool_.thread_count(), 1);
  EXPECT_TRUE(pool_.task_queue().empty());
  EXPECT_EQ(task_counter_, num_tasks);
  EXPECT_EQ(thread_start_count_, 1);
  EXPECT_EQ(thread_exit_count_, 0);
  EXPECT_EQ(pool_stop_count_, 0);
  EXPECT_EQ(pool_shutdown_count_, 0);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

TEST_F(SimpleTaskPoolTest, BulkTaskSubmission) {
  // Submit multiple tasks in bulk
  EXPECT_TRUE(pool_.submit(task_counter_, task_counter_, task_counter_, task_counter_, task_counter_));
  EXPECT_TRUE(pool_.set_thread_count(1));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(pool_.thread_count(), 1);
  EXPECT_TRUE(pool_.task_queue().empty());
  EXPECT_EQ(task_counter_, 5);
  EXPECT_EQ(thread_start_count_, 1);
  EXPECT_EQ(thread_exit_count_, 0);
  EXPECT_EQ(pool_stop_count_, 0);
  EXPECT_EQ(pool_shutdown_count_, 0);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

TEST_F(SimpleTaskPoolTest, StopFunctionality) {
  // Submit some tasks first
  for (int i = 0; i < 5; ++i) { pool_.submit(task_counter_); }

  EXPECT_TRUE(pool_.set_thread_count(1));

  // Stop thread pool
  EXPECT_EQ(pool_.stop().status(), thread_pool::ThreadPoolStatus::Stopping);

  // Task submission after stop should fail
  EXPECT_FALSE(pool_.submit(task_counter_));
  auto ret = pool_.set_thread_count(2);
  EXPECT_FALSE(ret);
  EXPECT_EQ(ret.error().code(), std::errc::operation_not_supported);

  // Wait for existing tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(pool_.task_queue().empty());
  EXPECT_EQ(task_counter_, 5);
  EXPECT_EQ(thread_start_count_, 1);
  EXPECT_EQ(thread_exit_count_, 0);
  EXPECT_EQ(pool_stop_count_, 1);
  EXPECT_EQ(pool_shutdown_count_, 0);
  EXPECT_EQ(pool_.thread_count(), 1);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Stopping);
}

TEST_F(DelayedTaskPoolTest, ShutdownFunctionality) {
  EXPECT_TRUE(pool_.set_thread_count(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(pool_.thread_count(), 1);

  // Submit some long-running tasks
  for (int i = 0; i < 3; ++i) { pool_.submit(DelayedTask{&task_counter_, std::chrono::milliseconds(100)}); }

  // Shutdown immediately
  EXPECT_EQ(pool_.shutdown().status(), thread_pool::ThreadPoolStatus::Stopped);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_FALSE(pool_.submit(DelayedTask{&task_counter_, std::chrono::milliseconds(100)}));
  auto ret = pool_.set_thread_count(2);
  EXPECT_FALSE(ret);
  EXPECT_EQ(ret.error().code(), std::errc::operation_not_supported);

  // Verify counter value - not all tasks may have completed
  // This depends on the timeliness of shutdown
  EXPECT_LE(task_counter_, 3);
  EXPECT_GE(task_counter_, 0);
  EXPECT_EQ(thread_start_count_, 1);
  EXPECT_EQ(thread_exit_count_, 1);
  EXPECT_EQ(pool_stop_count_, 0);
  EXPECT_EQ(pool_shutdown_count_, 1);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Stopped);
}

// Concurrency tests
class ThreadPoolConcurrencyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_EQ(pool_.thread_count(), 0);
    EXPECT_TRUE(pool_.task_queue().empty());
    EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
  }

  thread_pool::ThreadPool<> pool_;
  std::atomic<int> task_counter{0};
  std::atomic<int> completion_counter{0};
};

TEST_F(ThreadPoolConcurrencyTest, ConcurrentTaskSubmission) {
  EXPECT_TRUE(pool_.set_thread_count(4));  // Ensure enough threads

  constexpr int num_producers = 4;
  constexpr int tasks_per_producer = 50;

  std::vector<std::thread> producers;

  // Multiple producers submit tasks simultaneously
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([this] {
      for (int i = 0; i < tasks_per_producer; ++i) {
        int task_id = task_counter.fetch_add(1, std::memory_order_relaxed);
        pool_.submit([this, task_id]() noexcept { completion_counter.fetch_add(1, std::memory_order_relaxed); });
      }
    });
  }

  // Wait for all producers to finish
  for (auto& producer : producers) { producer.join(); }

  // Wait for all tasks to complete
  const int total_tasks = num_producers * tasks_per_producer;
  auto start = std::chrono::steady_clock::now();
  while (completion_counter.load() < total_tasks && std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

  EXPECT_EQ(completion_counter.load(), total_tasks);
  EXPECT_EQ(pool_.thread_count(), 4);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

TEST_F(ThreadPoolConcurrencyTest, MixedOperations) {
  EXPECT_TRUE(pool_.set_thread_count(4));

  constexpr int num_operations = 100;
  std::atomic<int> submitted{0};
  std::atomic<int> completed{0};

  std::thread producer([this, &submitted, &completed] {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 10);

    for (int i = 0; i < num_operations; ++i) {
      // Random delay to simulate real-world scenario
      std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));

      pool_.submit([&completed]() noexcept { completed.fetch_add(1, std::memory_order_relaxed); });
      submitted++;
    }
  });

  // Adjust thread count during task execution
  std::thread adjuster([this] {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 6);

    for (int i = 0; i < 3; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      EXPECT_TRUE(pool_.set_thread_count(dis(gen)));
    }
  });

  producer.join();
  adjuster.join();

  // Wait for all tasks to complete
  auto start = std::chrono::steady_clock::now();
  while (completed.load() < num_operations && std::chrono::steady_clock::now() - start < std::chrono::seconds(3)) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

  EXPECT_EQ(completed.load(), num_operations);
  EXPECT_GT(pool_.thread_count(), 0);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

TEST_F(ThreadPoolConcurrencyTest, StressTest) {
  EXPECT_TRUE(pool_.set_thread_count(8));  // More threads for stress test

  constexpr int total_tasks = 1'000;
  std::atomic<int> completed{0};

  auto start = std::chrono::steady_clock::now();

  // Rapidly submit large number of tasks
  for (int i = 0; i < total_tasks; ++i) {
    pool_.submit([&completed]() noexcept {
      // Simulate some workload
      volatile int dummy = 0;
      for (int j = 0; j < 1'000; ++j) { dummy += j; }
      completed.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // Wait for all tasks to complete
  while (completed.load() < total_tasks && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(completed.load(), total_tasks);
  EXPECT_EQ(pool_.thread_count(), 8);
  EXPECT_EQ(pool_.status(), thread_pool::ThreadPoolStatus::Running);
}

// Custom queue test
TEST(ThreadPoolCustomQueueTest, DifferentQueueTypes) {
  // Test thread pool working with different queue types
  struct CustomQueue {
    thread_pool::DefaultQueue<SimpleTask> impl;

    bool enqueue(SimpleTask&& task) noexcept { return impl.enqueue(std::move(task)); }

    bool dequeue(SimpleTask& task) noexcept { return impl.dequeue(task); }

    bool empty() const noexcept { return impl.empty(); }
  };

  thread_pool::ThreadPool<SimpleTask, CustomQueue> pool;

  std::atomic<int> counter{0};
  EXPECT_TRUE(pool.submit(SimpleTask{counter}));
  EXPECT_TRUE(pool.set_thread_count(1));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(counter.load(), 1);

  EXPECT_EQ(pool.thread_count(), 1);
  EXPECT_EQ(pool.status(), thread_pool::ThreadPoolStatus::Running);
}

}  // namespace