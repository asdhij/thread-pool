/**
 * @file task_queue.cc
 * @brief Tests for task queue module
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-02
 */

import thread_pool.queue;
import thread_pool.task;
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace {

static_assert(std::is_nothrow_destructible_v<thread_pool::DefaultQueue<int>>, "DefaultQueue should be nothrow destructible");
static_assert(!std::is_copy_constructible_v<thread_pool::DefaultQueue<int>>, "DefaultQueue should not be copy constructible");
static_assert(!std::is_move_constructible_v<thread_pool::DefaultQueue<int>>, "DefaultQueue should not be move constructible");

static_assert(thread_pool::nothrow_enqueueable<thread_pool::DefaultQueue<int>, int>, "DefaultQueue should satisfy nothrow_enqueueable concept");
static_assert(thread_pool::nothrow_dequeueable<thread_pool::DefaultQueue<int>, int>, "DefaultQueue should satisfy nothrow_dequeueable concept");

struct TestElement {
  int id;
  double value;

  TestElement() noexcept : id(0), value(0.0) {}
  TestElement(int i, double v) noexcept : id(i), value(v) {}

  TestElement(TestElement&& other) noexcept : id(other.id), value(other.value) { other.id = -1; }

  TestElement& operator=(TestElement&& other) noexcept {
    id = other.id;
    value = other.value;
    other.id = -1;
    return *this;
  }

  ~TestElement() noexcept = default;

  bool operator==(const TestElement& other) const noexcept { return id == other.id && value == other.value; }
};

// Basic functionality tests
TEST(DefaultQueueTest, BasicConstruction) {
  thread_pool::DefaultQueue<int> queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST(DefaultQueueTest, SingleEnqueueDequeue) {
  thread_pool::DefaultQueue<int> queue;

  // Test single element enqueue
  EXPECT_TRUE(queue.enqueue(42));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1);

  // Test single element dequeue
  int value = 0;
  EXPECT_TRUE(queue.dequeue(value));
  EXPECT_EQ(value, 42);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST(DefaultQueueTest, MultipleEnqueueDequeue) {
  thread_pool::DefaultQueue<int> queue;

  // Enqueue multiple elements
  for (int i = 0; i < 10; ++i) { EXPECT_TRUE(queue.enqueue(i)); }

  EXPECT_EQ(queue.size(), 10);

  // Dequeue in order
  for (int i = 0, value = -1; i < 10; ++i, value = -1) {
    EXPECT_TRUE(queue.dequeue(value));
    EXPECT_EQ(value, i);
  }

  EXPECT_TRUE(queue.empty());
}

TEST(DefaultQueueTest, BulkDequeue) {
  thread_pool::DefaultQueue<int> queue;
  constexpr size_t bulk_size = 5;

  // Prepare data
  for (int i = 0; i < 10; ++i) { EXPECT_TRUE(queue.enqueue(i)); }

  // Bulk dequeue
  std::array<int, bulk_size> buffer;
  size_t dequeued = queue.dequeue_bulk(std::span{buffer});

  EXPECT_EQ(dequeued, bulk_size);
  EXPECT_EQ(queue.size(), 10 - bulk_size);

  // Verify dequeue order
  for (size_t i = 0; i < bulk_size; ++i) { EXPECT_EQ(buffer[i], static_cast<int>(i)); }

  EXPECT_EQ(queue.size(), 5);
}

TEST(DefaultQueueTest, BulkDequeuePartial) {
  thread_pool::DefaultQueue<int> queue;

  // Only 3 elements available, but requesting 5
  EXPECT_TRUE(queue.enqueue(1));
  EXPECT_TRUE(queue.enqueue(2));
  EXPECT_TRUE(queue.enqueue(3));

  std::array<int, 5> buffer;
  size_t dequeued = queue.dequeue_bulk(std::span{buffer});

  EXPECT_EQ(dequeued, 3);
  EXPECT_TRUE(queue.empty());

  EXPECT_EQ(buffer[0], 1);
  EXPECT_EQ(buffer[1], 2);
  EXPECT_EQ(buffer[2], 3);
}

TEST(DefaultQueueTest, DequeueFromEmpty) {
  thread_pool::DefaultQueue<int> queue;

  int value = 42;
  // Dequeue from empty queue should return false
  EXPECT_FALSE(queue.dequeue(value));
  EXPECT_EQ(value, 42);  // Value should not be modified

  std::array<int, 5> buffer;
  size_t dequeued = queue.dequeue_bulk(std::span{buffer});
  EXPECT_EQ(dequeued, 0);
}

TEST(DefaultQueueTest, CustomType) {
  thread_pool::DefaultQueue<TestElement> queue;

  TestElement elem1{1, 3.14};
  TestElement elem2{2, 2.71};

  EXPECT_TRUE(queue.enqueue(std::move(elem1)));
  EXPECT_TRUE(queue.enqueue(std::move(elem2)));

  TestElement result;
  EXPECT_TRUE(queue.dequeue(result));
  EXPECT_EQ(result.id, 1);
  EXPECT_EQ(result.value, 3.14);

  EXPECT_TRUE(queue.dequeue(result));
  EXPECT_EQ(result.id, 2);
  EXPECT_EQ(result.value, 2.71);
}

// Concurrency correctness tests
class DefaultQueueConcurrencyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    produced_count = 0;
    consumed_count = 0;
  }

  std::atomic<int> produced_count{0};
  std::atomic<int> consumed_count{0};
  thread_pool::DefaultQueue<int> queue;
};

TEST_F(DefaultQueueConcurrencyTest, SingleProducerSingleConsumer) {
  constexpr int total_items = 1'000;

  std::thread producer([this] {
    for (int i = 0; i < total_items; ++i) {
      while (!queue.enqueue(i)) { std::this_thread::yield(); }
      produced_count++;
    }
  });

  std::thread consumer([this, &total_items] {
    int received_count = 0;
    int value;
    while (received_count < total_items) {
      if (queue.dequeue(value)) {
        EXPECT_GE(value, 0);
        EXPECT_LT(value, total_items);
        received_count++;
        consumed_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(produced_count, total_items);
  EXPECT_EQ(consumed_count, total_items);
  EXPECT_TRUE(queue.empty());
}

TEST_F(DefaultQueueConcurrencyTest, MultipleProducersMultipleConsumers) {
  constexpr int total_items_per_producer = 500;
  constexpr int num_producers = 4;
  constexpr int num_consumers = 4;
  constexpr int total_items = total_items_per_producer * num_producers;

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  // Start producers
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([this, p] {
      int start = p * total_items_per_producer;
      for (int i = 0; i < total_items_per_producer; ++i) {
        int value = start + i;
        while (!queue.enqueue(value)) { std::this_thread::yield(); }
        produced_count++;
      }
    });
  }

  // Start consumers
  std::vector<int> consumed_items(total_items, 0);
  std::mutex items_mutex;

  for (int c = 0; c < num_consumers; ++c) {
    consumers.emplace_back([this, &consumed_items, &items_mutex, &total_items] {
      int value;
      while (consumed_count < total_items) {
        if (queue.dequeue(value)) {
          EXPECT_GE(value, 0);
          EXPECT_LT(value, total_items);

          std::atomic_ref<int>(consumed_items[value])++;

          consumed_count++;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  // Wait for all producers to finish
  for (auto& producer : producers) { producer.join(); }

  // Wait for all consumers to finish
  for (auto& consumer : consumers) { consumer.join(); }

  EXPECT_EQ(produced_count, total_items);
  EXPECT_EQ(consumed_count, total_items);
  EXPECT_TRUE(queue.empty());

  // Verify each item was consumed exactly once
  for (int i = 0; i < total_items; ++i) { EXPECT_EQ(consumed_items[i], 1) << "Item " << i << " was consumed " << consumed_items[i] << " times"; }
}

TEST_F(DefaultQueueConcurrencyTest, ConcurrentBulkOperations) {
  constexpr int num_threads = 8;
  constexpr int items_per_thread = 200;
  constexpr int bulk_size = 10;

  std::vector<std::thread> threads;
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &total_produced, &total_consumed] {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(1, 3);

      // Each thread is both a producer and a consumer
      for (int i = 0; i < items_per_thread; ++i) {
        // Randomly decide to do bulk or single enqueue
        if (dis(gen) == 1) {
          // Bulk enqueue
          for (int j = 0; j < bulk_size; ++j) {
            int value = t * items_per_thread + i * bulk_size + j;
            if (queue.enqueue(value)) { total_produced++; }
          }
        } else {
          // Single enqueue
          int value = t * items_per_thread + i;
          if (queue.enqueue(value)) { total_produced++; }
        }

        // Randomly decide to do bulk or single dequeue
        if (dis(gen) == 1) {
          // Bulk dequeue
          std::array<int, bulk_size> buffer;
          size_t count = queue.dequeue_bulk(std::span{buffer});
          total_consumed += count;
        } else {
          // Single dequeue
          int value;
          if (queue.dequeue(value)) { total_consumed++; }
        }
      }
    });
  }

  for (auto& thread : threads) { thread.join(); }

  // Drain remaining items in the queue
  int value;
  while (queue.dequeue(value)) { total_consumed++; }

  EXPECT_EQ(total_produced, total_consumed);
  EXPECT_TRUE(queue.empty());
}

// Edge condition tests
TEST(DefaultQueueTest, MoveSemantics) {
  thread_pool::DefaultQueue<std::unique_ptr<int>> queue;

  auto ptr = std::make_unique<int>(42);
  EXPECT_TRUE(queue.enqueue(std::move(ptr)));
  EXPECT_EQ(ptr, nullptr);  // Should be null after move

  std::unique_ptr<int> result;
  EXPECT_TRUE(queue.dequeue(result));
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(*result, 42);
}

}  // namespace

namespace {

// Tests for DefaultThreadLocalQueue

TEST(DefaultThreadLocalQueueTest, BasicConstruction) {
  thread_pool::DefaultThreadLocalQueue<thread_pool::DefaultTask> queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST(DefaultThreadLocalQueueTest, ProcessTasksAndEnqueue) {
  thread_pool::DefaultThreadLocalQueue<thread_pool::DefaultTask> queue;
  std::atomic<int> counter{0};

  // Enqueue a noexcept lambda that increments the counter.
  EXPECT_TRUE(queue.enqueue([&counter]() noexcept { ++counter; }));
  EXPECT_EQ(queue.size(), 1);

  // Process tasks on the owning thread.
  queue.process_tasks();
  EXPECT_EQ(counter.load(), 1);
  EXPECT_TRUE(queue.empty());
}

TEST(DefaultThreadLocalQueueTest, WaitForTaskWakesOnEnqueue) {
  thread_pool::DefaultThreadLocalQueue<thread_pool::DefaultTask> queue;
  std::atomic<bool> woke{false};

  std::thread waiter([&] {
    // This should block until a task is enqueued.
    const bool has = queue.wait_for_task();
    woke.store(has);
  });

  // Give the waiter a moment to start waiting.
  std::this_thread::sleep_for(std::chrono::milliseconds{5});

  EXPECT_TRUE(queue.enqueue([]() noexcept {}));
  waiter.join();
  EXPECT_TRUE(woke.load());
}

TEST(DefaultThreadLocalQueueTest, WaitForTaskReturnsFalseOnStop) {
  thread_pool::DefaultThreadLocalQueue<thread_pool::DefaultTask> queue;
  std::atomic<bool> woke{true};

  std::thread waiter([&] {
    // Should return false when stop is requested and no tasks are pending.
    const bool has = queue.wait_for_task();
    woke.store(has);
  });

  // Give the waiter a moment to start waiting.
  std::this_thread::sleep_for(std::chrono::milliseconds{5});
  queue.notify_for_stop();
  waiter.join();
  EXPECT_FALSE(woke.load());
}

}  // namespace