/**
 * @file task_queue.ixx
 * @brief Defines concepts and a default implementation for thread pool queues
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-10-03
 */

module;
import thread_pool.task;
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <deque>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <queue>
#include <span>
#include <type_traits>
#include <utility>
export module thread_pool.queue;

namespace thread_pool {

/**
 * @brief Concept to check if a queue supports nothrow enqueue operation
 * @tparam Queue The queue type to check
 * @tparam Args The argument types for the enqueue method
 * @note The enqueue method should return a bool indicating success or failure of the operation.
 */
export template <typename Queue, typename... Args>
concept nothrow_enqueueable = requires(Queue queue, Args&&... args) {
  { queue.enqueue(std::forward<Args>(args)...) } noexcept -> std::same_as<bool>;
};

/**
 * @brief Concept to check if a queue supports nothrow bulk enqueue operation
 * @tparam Queue The queue type to check
 * @tparam Args The argument types for the enqueue_bulk method.
 * @note Each argument should correspond to a construction of the tasks to be enqueued.
 * @note The operation must be atomic, meaning that either all tasks are enqueued successfully,
 *       or none are enqueued in case of failure. If the operation fails, both the queue and
 *       the tasks should remain unchanged.
 * @note The enqueue_bulk method should return a bool indicating success or failure of the operation.
 */
export template <typename Queue, typename... Args>
concept nothrow_bulk_enqueueable = requires(Queue queue, Args&&... args) {
  { queue.enqueue_bulk(std::forward<Args>(args)...) } noexcept -> std::same_as<bool>;
};

/**
 * @brief Concept to check if a queue supports nothrow dequeue operation
 * @tparam Queue The queue type to check
 * @tparam Ret The argument type for the dequeue method
 * @note This concept checks only that queue.dequeue(ret) is noexcept.
 *       The return value is intentionally not constrained by the concept because
 *       some queue implementations may have different return conventions.
 */
export template <typename Queue, typename Ret>
concept nothrow_dequeueable = requires(Queue queue, Ret& ret) {
  { (void)queue.dequeue(ret) } noexcept;
};

/**
 * @brief Concept to check if a queue supports nothrow bulk dequeue operation
 * @tparam Queue The queue type to check
 * @tparam Ret The argument type for the dequeue_bulk method
 * @note The dequeue_bulk method receives a std::span of Ret type,
 *       which will be filled with the dequeued tasks.
 * @note The size of tasks span is the maximum number of tasks expected to dequeue,
 *       so the actual number of tasks dequeued may be less than or equal to that size.
 * @note This concept checks only that `queue.dequeue_bulk(span)` is `noexcept`.
 *       The return value is intentionally not constrained by the concept.
 */
export template <typename Queue, typename Ret>
concept nothrow_bulk_dequeueable = requires(Queue queue, std::span<Ret, std::dynamic_extent> tasks) {
  { (void)queue.dequeue_bulk(tasks) } noexcept;
};

/**
 * @brief Concept to check if a type is a valid task queue
 * @tparam Queue The queue type to check
 * @tparam Task The task type(s) to check, should satisfy thread_pool::task concept
 * @note A valid task queue must support either nothrow dequeue or nothrow bulk dequeue operations.
 * @note The enqueue and enqueue_bulk operations are not checked here, as they are typically used
 *       when adding tasks to the queue, which is not a requirement for being a task queue.
 */
export template <typename Queue, typename Task>
concept task_queue = task<Task> && (nothrow_dequeueable<Queue, Task> || nothrow_bulk_dequeueable<Queue, Task>);

template <typename T>
concept nothrow_assign_destructible = requires(T a, T b) {
  // Check if the assignment operation is noexcept
  { a = std::move_if_noexcept(b) } noexcept;
} && !std::is_reference_v<T> && !std::is_const_v<T> && std::is_nothrow_destructible_v<T>;

/**
 * @brief Default task queue implementation using std::queue
 * @tparam T The task type to be stored in the queue
 */
export template <nothrow_assign_destructible T>
class DefaultQueue {
 public:
  using value_type = T;
  using allocator_type = std::pmr::polymorphic_allocator<T>;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = std::allocator_traits<allocator_type>::pointer;
  using const_pointer = std::allocator_traits<allocator_type>::const_pointer;

  constexpr DefaultQueue() noexcept = default;

  DefaultQueue(const DefaultQueue&) = delete;
  DefaultQueue(DefaultQueue&&) noexcept = delete;
  DefaultQueue& operator=(const DefaultQueue&) = delete;
  DefaultQueue& operator=(DefaultQueue&&) = delete;

  /**
   * @brief Returns the number of tasks currently in the queue.
   * @return size_type The number of tasks in the queue.
   */
  [[nodiscard]] constexpr size_type size() const noexcept {
    std::lock_guard lock(lock_);
    return tasks_.size();
  }

  /**
   * @brief Checks if the queue is empty.
   * @return true If the queue is empty.
   * @return false If the queue is not empty.
   */
  [[nodiscard]] constexpr bool empty() const noexcept {
    std::lock_guard lock(lock_);
    return tasks_.empty();
  }

  /**
   * @brief Enqueues a new task into the queue.
   * @tparam Args The types of arguments to construct the task.
   * @param args The arguments to construct the task.
   * @return true If the task was enqueued successfully.
   * @return false If memory allocation fails or if T's construction throws.
   */
  template <typename... Args> requires std::constructible_from<T, Args...>
  [[nodiscard]] constexpr bool enqueue(Args&&... args) noexcept {
    std::lock_guard lock(lock_);
    try {
      tasks_.emplace(std::forward<Args>(args)...);
    } catch (...) {
      return false;
    }
    return true;
  }

  /**
   * @brief Dequeues a task from the queue.
   * @param ret The variable to store the dequeued task.
   * @return true If a task was successfully dequeued.
   * @return false If the queue was empty and no task was dequeued.
   */
  [[nodiscard]] constexpr bool dequeue(T& ret) noexcept {
    std::lock_guard lock(lock_);
    if (tasks_.empty()) [[unlikely]] { return false; }
    ret = std::move_if_noexcept(tasks_.front());
    tasks_.pop();
    return true;
  }

  /**
   * @brief Dequeues multiple tasks from the queue.
   * @tparam Extent The extent of the span to store dequeued tasks.
   * @param ret The span to store the dequeued tasks.
   * @return size_type The number of tasks actually dequeued.
   */
  template <std::size_t Extent> requires (Extent > 0 || Extent == std::dynamic_extent)
  [[nodiscard]] constexpr size_type dequeue_bulk(const std::span<T, Extent>& ret) noexcept {
    std::lock_guard lock(lock_);
    const size_type real = std::min(tasks_.size(), ret.size());
    if (!real) [[unlikely]] { return 0; }
    std::for_each_n(ret.begin(), real, [this](T& t) noexcept {
      t = std::move_if_noexcept(tasks_.front());
      tasks_.pop();
    });
    return real;
  }

  constexpr ~DefaultQueue() noexcept = default;

 private:
  std::pmr::unsynchronized_pool_resource resource_;
  std::queue<T, std::deque<T, allocator_type>> tasks_{&resource_};
  mutable std::mutex lock_;
};

}  // namespace thread_pool