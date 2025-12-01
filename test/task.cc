/**
 * @file task.cc
 * @brief Tests for task module
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-02
 */

import thread_pool.task;
#include <gtest/gtest.h>

namespace {

// A simple callable object for testing
struct SimpleFunctor {
  int value = 0;
  void operator()() noexcept { value = 42; }
};

// Another callable object for testing move semantics
struct MoveTrackingFunctor {
  std::unique_ptr<int> data;
  bool was_moved = false;

  MoveTrackingFunctor() noexcept : data(std::make_unique<int>(100)) {}

  MoveTrackingFunctor(MoveTrackingFunctor&& other) noexcept : data(std::move(other.data)) { other.was_moved = true; }

  MoveTrackingFunctor& operator=(MoveTrackingFunctor&& other) noexcept {
    data = std::move(other.data);
    other.was_moved = true;
    return *this;
  }

  void operator()() noexcept {
    if (data) { *data += 1; }
  }
};

// Test default constructor
TEST(DefaultTaskTest, DefaultConstruction) {
  thread_pool::DefaultTask task;
  EXPECT_FALSE(static_cast<bool>(task));  // Should be empty
}

// Test construction from function pointer
void test_function() noexcept {}

TEST(DefaultTaskTest, FunctionPointerConstruction) {
  thread_pool::DefaultTask task(&test_function);
  EXPECT_TRUE(static_cast<bool>(task));  // Should be non-empty
}

// Test construction from lambda
TEST(DefaultTaskTest, LambdaConstruction) {
  int value = 0;
  auto lambda = [&value]() noexcept { value = 100; };

  thread_pool::DefaultTask task(lambda);
  EXPECT_TRUE(static_cast<bool>(task));
}

// Test invocation operator
TEST(DefaultTaskTest, Invocation) {
  SimpleFunctor functor;
  thread_pool::DefaultTask task(std::move(functor));

  EXPECT_EQ(functor.value, 0);  // Original functor should not be modified

  EXPECT_NO_FATAL_FAILURE(task());
}

// Test move construction
TEST(DefaultTaskTest, MoveConstruction) {
  SimpleFunctor functor;
  thread_pool::DefaultTask task1(std::move(functor));
  thread_pool::DefaultTask task2(std::move(task1));

  EXPECT_FALSE(static_cast<bool>(task1));  // Should be empty after move
  EXPECT_TRUE(static_cast<bool>(task2));   // New task should not be empty
}

// Test move assignment
TEST(DefaultTaskTest, MoveAssignment) {
  SimpleFunctor functor1, functor2;
  thread_pool::DefaultTask task1(std::move(functor1));
  thread_pool::DefaultTask task2(std::move(functor2));

  task1 = std::move(task2);

  EXPECT_FALSE(static_cast<bool>(task2));  // Should be empty after move
  EXPECT_TRUE(static_cast<bool>(task1));   // Should hold the object
}

// Test that copy operations are disabled
static_assert(!std::is_copy_constructible_v<thread_pool::DefaultTask>, "DefaultTask should not be copy constructible");
static_assert(!std::is_copy_assignable_v<thread_pool::DefaultTask>, "DefaultTask should not be copy assignable");

// Test noexcept guarantees
static_assert(std::is_nothrow_default_constructible_v<thread_pool::DefaultTask>, "DefaultTask should be nothrow default constructible");
static_assert(std::is_nothrow_move_constructible_v<thread_pool::DefaultTask>, "DefaultTask should be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<thread_pool::DefaultTask>, "DefaultTask should be nothrow move assignable");
static_assert(std::is_nothrow_destructible_v<thread_pool::DefaultTask>, "DefaultTask should be nothrow destructible");

// Verify that DefaultTask satisfies the task concept
static_assert(thread_pool::task<thread_pool::DefaultTask>, "DefaultTask should satisfy the task concept");

// Test empty task invocation
TEST(DefaultTaskTest, EmptyTaskInvocation) {
  thread_pool::DefaultTask empty_task;

  // Calling an empty task should not crash
  EXPECT_NO_FATAL_FAILURE(empty_task());
}

// Test complex scenario - multiple tasks
TEST(DefaultTaskTest, MultipleTasks) {
  int counter1 = 0, counter2 = 0;

  auto lambda1 = [&counter1]() noexcept { counter1 += 1; };
  auto lambda2 = [&counter2]() noexcept { counter2 += 2; };

  thread_pool::DefaultTask task1(lambda1);
  thread_pool::DefaultTask task2(lambda2);

  EXPECT_NO_FATAL_FAILURE(task1());
  EXPECT_NO_FATAL_FAILURE(task2());
  EXPECT_EQ(counter1, 1);
  EXPECT_EQ(counter2, 2);
}

}  // namespace