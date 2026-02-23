/**
 * @file thread_pool.ixx
 * @brief Implementation of a customizable thread pool with task submission and dynamic thread management
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-27
 */

module;
import thread_pool.policy;
import thread_pool.queue;
import thread_pool.task;
#include <array>
#include <atomic>
#include <climits>
#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
export module thread_pool;

namespace thread_pool {

/**
 * @brief stop mask for num_queued_tasks_
 */
constexpr std::size_t stop_mask = static_cast<std::size_t>(1) << (sizeof(std::size_t) * CHAR_BIT - 1);

/**
 * @brief Enum representing the status of the thread pool.
 */
export enum class ThreadPoolStatus : unsigned char { Running = 0, Stopping = 1, Stopped = 2 };

/**
 * @brief Concept defining the basic requirements for an allocator type
 *
 * An allocator must provide:
 * - An instance of std::allocator_traits
 * - allocate(n) returning pointer to value_type
 * - deallocate(ptr, n) for memory release
 */
template <typename Alloc>
concept allocator = requires(Alloc alloc, std::size_t n, typename std::allocator_traits<Alloc>::pointer ptr) {
  typename std::allocator_traits<Alloc>::value_type;
  { alloc.allocate(n) } -> std::same_as<typename std::allocator_traits<Alloc>::pointer>;
  { std::allocator_traits<Alloc>::allocate(alloc, n) } -> std::same_as<typename std::allocator_traits<Alloc>::pointer>;
  { alloc.deallocate(ptr, n) } -> std::same_as<void>;
  std::allocator_traits<Alloc>::deallocate(alloc, ptr, n);
  std::allocator_traits<Alloc>::destroy(alloc, ptr);
};

template<typename T, typename = void>
struct has_tuple_size : std::false_type {};

template<typename T>
struct has_tuple_size<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

template<typename T>
concept tuple_like = has_tuple_size<std::remove_reference_t<T>>::value && (std::tuple_size<std::remove_reference_t<T>>::value == 0 || requires (T t) {
  std::get<0>(std::forward<T>(t));
});

template <typename T, typename Tuple>
concept can_make_from_tuple = tuple_like<Tuple> && requires (Tuple tuple) { std::make_from_tuple<T>(std::forward<Tuple>(tuple)); };

template <typename T, typename Tuple>
concept can_nothrow_make_from_tuple = tuple_like<Tuple> && requires (Tuple tuple) { { std::make_from_tuple<T>(std::forward<Tuple>(tuple)) } noexcept; };

/**
 * @brief Thread pool class template for managing and executing tasks concurrently.
 * @tparam Task The task type to be executed by the thread pool, should satisfy thread_pool::task concept.
 * @tparam TaskQueue The task queue type used to store and manage tasks, should satisfy thread_pool::task_queue concept.
 * @tparam Policy The policy type for customizing thread pool behavior.
 * @tparam ThreadAllocator The allocator type for managing thread resources.
 */
export template <task Task = DefaultTask, std::destructible TaskQueue = DefaultQueue<Task>, std::destructible Policy = DefaultPolicy, allocator ThreadAllocator = std::allocator<std::stop_source>> requires task_queue<TaskQueue, Task>
class ThreadPool {
 public:
  using thread_allocator_type = typename std::allocator_traits<ThreadAllocator>::template rebind_alloc<std::stop_source>;

  /**
   * @brief Constructs a ThreadPool with a given policy and task queue arguments.
   * @tparam Pol The type of the policy to be used.
   * @tparam Queue The types of arguments to construct the task queue.
   * @param policy The policy instance to be used by the thread pool.
   * @param args The arguments to construct the task queue.
   */
  template <typename Pol, typename... Queue> requires std::constructible_from<Policy, Pol> && std::constructible_from<TaskQueue, Queue...>
  constexpr explicit ThreadPool(Pol &&policy, Queue &&...args) noexcept(std::is_nothrow_constructible_v<Policy, Pol> && std::is_nothrow_constructible_v<TaskQueue, Queue...>) :
      policy_{std::forward<Pol>(policy)}, tasks_{std::forward<Queue>(args)...} {}

  /**
   * @brief Constructs a ThreadPool with default policy and given task queue arguments.
   * @tparam Queue The types of arguments to construct the task queue.
   * @param args The arguments to construct the task queue.
   */
  template <typename... Queue> requires std::constructible_from<TaskQueue, Queue...> && std::is_default_constructible_v<Policy>
  constexpr explicit ThreadPool(Queue &&...args) noexcept(std::is_nothrow_default_constructible_v<Policy> && std::is_nothrow_constructible_v<TaskQueue, Queue...>) : tasks_{std::forward<Queue>(args)...} {}

  /**
   * @brief Constructs a ThreadPool with custom task queue, policy, and thread allocator arguments.
   * @tparam QueueArgs The types of arguments to construct the task queue.
   * @tparam PolicyArgs The types of arguments to construct the policy.
   * @tparam ThreadAllocatorArgs The types of arguments to construct the thread allocator.
   * @param queue_args The arguments to construct the task queue.
   * @param policy_args The arguments to construct the policy.
   * @param thread_allocator_args The arguments to construct the thread allocator.
   */
  template <tuple_like QueueArgs, tuple_like PolicyArgs = std::tuple<>, tuple_like ThreadAllocatorArgs = std::tuple<>>
    requires can_make_from_tuple<TaskQueue, QueueArgs> && can_make_from_tuple<Policy, PolicyArgs> && can_make_from_tuple<ThreadAllocator, ThreadAllocatorArgs>
  constexpr explicit ThreadPool(QueueArgs &&queue_args, PolicyArgs &&policy_args = std::tuple{}, ThreadAllocatorArgs &&thread_allocator_args = std::tuple{})
    noexcept(can_nothrow_make_from_tuple<TaskQueue, QueueArgs> && can_nothrow_make_from_tuple<Policy, PolicyArgs> && can_nothrow_make_from_tuple<ThreadAllocator, ThreadAllocatorArgs>) :
      policy_{std::make_from_tuple<Policy>(std::forward<PolicyArgs>(policy_args))}, tasks_{std::make_from_tuple<TaskQueue>(std::forward<QueueArgs>(queue_args))},
      stop_sources_{std::make_from_tuple<ThreadAllocator>(std::forward<ThreadAllocatorArgs>(thread_allocator_args))} {}

  ///@brief Default constructor for ThreadPool, initializes with default policy and task queue.
  constexpr ThreadPool() noexcept(std::is_nothrow_default_constructible_v<Policy> && std::is_nothrow_default_constructible_v<TaskQueue> && std::is_nothrow_default_constructible_v<ThreadAllocator>)
    requires(std::is_default_constructible_v<Policy> && std::is_default_constructible_v<TaskQueue> && std::is_default_constructible_v<ThreadAllocator>) = default;

  /**
   * @brief Submit one or more tasks to the thread pool's task queue.
   *
   * @tparam F The type(s) of the callable task(s) being submitted.
   *
   * @note The call is constrained to implementations where either:
   *       - the TaskQueue supports nothrow bulk enqueue for all F..., or
   *       - each F is nothrow enqueueable into TaskQueue.
   *
   * @note Attempts to enqueue the provided tasks into the pool's internal TaskQueue.
   *       The routine is constexpr and noexcept; it will not throw exceptions. The
   *       function first checks whether the pool is currently running and returns
   *       false immediately if it is not. Otherwise, it attempts to enqueue the
   *       tasks using either a bulk enqueue path (when available) or an individual
   *       per-task enqueue path. Successful enqueues are accounted for in the
   *       num_queued_tasks_ counter and worker notification is performed as needed.
   *
   * @param tasks Variadic forwarding references to one or more task callables.
   *
   * @return
   *   - true  : The pool was running and the submission attempt was performed.
   *             Note that this does NOT guarantee each task was enqueued successfully.
   *   - false : The pool was not in the Running state (submit aborted).
   *
   * @details Behavior:
   *   - Early exit: If stop_flag_ != ThreadPoolStatus::Running (checked with
   *     memory_order::acquire), the function returns false and no enqueue attempts
   *     are made.
   *
   *   - Bulk enqueue path:
   *       - Taken when a nothrow bulk enqueue is available for the whole pack
   *         (or when sizeof...(F) > 1 and the bulk path is chosen by the constexpr logic).
   *       - Calls tasks_.enqueue_bulk(std::forward<F>(tasks)...).
   *       - On success: increments num_queued_tasks_ by sizeof...(F) using
   *         fetch_add(..., memory_order::release). If the previous value was zero,
   *         notifies workers: notify_all() when more than one task was submitted,
   *         notify_one() when exactly one task was submitted.
   *       - On failure: if the Policy provides a bulk on_task_enqueue_failed hook,
   *         it is invoked with all tasks; otherwise, if per-task failure hooks
   *         exist the respective hooks are invoked.
   *
   *   - Per-task enqueue path:
   *       - Used when bulk enqueue is not selected/available and the per-task
   *         enqueueability path is used by the constexpr logic.
   *       - Each task is attempted with tasks_.enqueue(std::forward<F>(task)).
   *       - For each successful enqueue, a per-call counter is incremented.
   *       - For each failed enqueue, if the Policy provides an on_task_enqueue_failed
   *         for that task type, the policy hook is invoked for that task.
   *       - After attempting all tasks, num_queued_tasks_ is atomically increased
   *         by the number of successful enqueues; if the previous count was zero,
   *         workers are notified (notify_all vs notify_one as above).
   *       - The implementation contains an assume hint that the counted successes
   *         do not exceed sizeof...(F).
   *
   * @details Concurrency and memory order:
   *   - stop_flag_ is read with memory_order::acquire to observe the pool state.
   *   - num_queued_tasks_ is updated with memory_order::release so that newly
   *     queued tasks are visible to workers after the counter update.
   *   - Notifications are used to wake waiting worker threads only when the queue
   *     transitions from empty to non-empty.
   *
   * @details Side effects:
   *   - May call policy_.on_task_enqueue_failed(...) for bulk or individual tasks
   *     depending on the failure mode and the Policy interface.
   *   - May call num_queued_tasks_.notify_one() or notify_all() to wake workers.
   *
   * @note
   *   - The function returns true when the submission attempt was performed while
   *     the pool was running; it does not reflect per-task enqueue success.
   *   - The noexcept requirement and the nothrow_* constraints ensure no exceptions
   *     propagate from the enqueue operations (or they are handled by the Policy).
   */
  template <typename... F> requires (sizeof...(F) > 0 && (nothrow_bulk_enqueueable<TaskQueue, F...> || (nothrow_enqueueable<TaskQueue, F> && ...)))
  constexpr bool submit(F &&...tasks) noexcept {
    if (stop_flag_.load(std::memory_order::acquire) != ThreadPoolStatus::Running) { return false; }

    if constexpr ((sizeof...(F) > 1 && nothrow_bulk_enqueueable<TaskQueue, F...>) || (sizeof...(F) == 1 && !(nothrow_enqueueable<TaskQueue, F> && ...))) {
      if (tasks_.enqueue_bulk(std::forward<F>(tasks)...)) {
        if (!num_queued_tasks_.fetch_add(sizeof...(F), std::memory_order::release)) {
          if constexpr (sizeof...(F) > 1) {
            num_queued_tasks_.notify_all();
          } else {
            num_queued_tasks_.notify_one();
          }
        }
      } else if constexpr (thread_pool::Policy::has_on_task_enqueue_failed<Policy, F...>) {
        policy_.on_task_enqueue_failed(std::forward<F>(tasks)...);
      } else if constexpr ((thread_pool::Policy::has_on_task_enqueue_failed<Policy, F> && ...)) {
        (policy_.on_task_enqueue_failed(std::forward<F>(tasks)), ...);
      }
    } else {
      const std::size_t num = ([this, &tasks] -> bool {
        if constexpr (thread_pool::Policy::has_on_task_enqueue_failed<Policy, F>) {
          if (tasks_.enqueue(std::forward<F>(tasks))) { return true; }
          policy_.on_task_enqueue_failed(std::forward<F>(tasks));
          return false;
        } else {
          return tasks_.enqueue(std::forward<F>(tasks));
        }
      }() + ...);
      [[assume(num <= sizeof...(F))]];
      if (num && !num_queued_tasks_.fetch_add(num, std::memory_order::release)) {
        if constexpr (sizeof...(F) > 1) {
          num_queued_tasks_.notify_all();
        } else {
          num_queued_tasks_.notify_one();
        }
      }
    }

    return true;
  }

  /**
   * @brief Adjust the number of worker threads used by the thread pool.
   *        Attempts to set the pool's worker-thread count to the value specified by num.
   *
   * @exception std::system_error The function is noexcept: it does not throw exceptions, but
   *            reports failures via the returned std::expected.
   *            - If the pool is not in the Running state, the operation is not allowed and the
   *              function returns an unexpected containing std::errc::operation_not_supported.
   *            - If memory allocation fails while trying to add threads, an unexpected containing
   *              std::errc::not_enough_memory is returned.
   *            - If the requested number of threads is too large, an unexpected containing
   *              std::errc::invalid_argument is returned.
   *
   * @details
   *    - If num is greater than the current thread count, additional workers are
   *      created (the call is forwarded to add_threads). If num is less than the
   *      current count, worker threads are removed/stopped (via shrink_threads).
   *    - If num equals the current count, no action is performed.
   *
   * @details The implementation synchronizes modifications to internal stop sources by
   *          acquiring stop_sources_mutex_ and reads stop_flag_ atomically to verify the
   *          pool status.
   *
   * @param num Desired total number of worker threads.
   * @return On success, an expected containing the previous thread count (the
   *         number of threads before this call). When increasing the count, the
   *         result returned by add_threads(...) is forwarded (which may contain
   *         a different, implementation-specific value).
   *         On failure, an unexpected<std::system_error> describing the error (e.g.
   *         operation_not_supported if the pool is not running).
   */
  constexpr std::expected<std::size_t, std::system_error> set_thread_count(const std::size_t &num) noexcept {
    if (stop_flag_.load(std::memory_order::acquire) != ThreadPoolStatus::Running) { return std::unexpected{std::system_error{std::make_error_code(std::errc::operation_not_supported)}}; }

    std::lock_guard lock{stop_sources_mutex_};
    const std::size_t old_count = stop_sources_.size();

    if (num > old_count) {
      return add_threads(num - old_count, old_count);
    } else if (num < old_count) {
      shrink_threads(old_count - num, old_count);
    }

    return old_count;
  }

  /**
   * @brief Get the current number of running worker threads in the thread pool.
   * @return std::size_t The number of running worker threads.
   */
  [[nodiscard]] constexpr std::size_t thread_count() const noexcept { return num_running_threads_.load(std::memory_order::acquire); }

  /**
   * @brief Get the current status of the thread pool.
   * @return ThreadPoolStatus The current status of the thread pool.
   */
  [[nodiscard]] constexpr ThreadPoolStatus status() const noexcept { return stop_flag_.load(std::memory_order::acquire); }

  /**
   * @brief Initiate a graceful stop of the thread pool.
   * @note It only transitions the pool to the Stopping state. No threads are
   *       terminated or stopped at this point. Subsequent calls to submit() will fail.
   *       To fully stop the pool, call shutdown() after stop().
   * @note It's asynchronous, worker threads will continue to finish other enqueued tasks
    *      until the shutdown or set_thread_count is called.
   * @return Reference to this ThreadPool instance.
   */
  constexpr ThreadPool& stop() noexcept(!thread_pool::Policy::has_on_pool_stop<Policy> || thread_pool::Policy::has_on_pool_stop_nothrow<Policy>) {
    if (ThreadPoolStatus expected = ThreadPoolStatus::Running; !stop_flag_.compare_exchange_strong(expected, ThreadPoolStatus::Stopping, std::memory_order::release, std::memory_order::acquire)) { return *this; }
    if constexpr (thread_pool::Policy::has_on_pool_stop<Policy>) { policy_.on_pool_stop(); }
    return *this;
  }

  /**
   * @brief Shutdown the thread pool, stopping all worker threads.
   * @note This function transitions the pool to the Stopped state, requests
   *       stop on all worker threads, and notifies them to wake up if they
   *       are waiting for tasks. Subsequent calls to submit() will fail.
   * @note It's asynchronous, worker threads will finish their current tasks
   *       and then stop when they check for the stop condition.
   * @return Reference to this ThreadPool instance.
   */
  constexpr ThreadPool& shutdown() noexcept(!thread_pool::Policy::has_on_pool_shutdown<Policy> || thread_pool::Policy::has_on_pool_shutdown_nothrow<Policy>) {
    if (stop_flag_.exchange(ThreadPoolStatus::Stopped, std::memory_order::acq_rel) == ThreadPoolStatus::Stopped) { return *this; }
    if (std::lock_guard lock{stop_sources_mutex_}; !stop_sources_.empty()) { shrink_threads(stop_sources_.size(), stop_sources_.size()); }
    if constexpr (thread_pool::Policy::has_on_pool_shutdown<Policy>) { policy_.on_pool_shutdown(); }
    return *this;
  }

  /**
   * @brief Wait for all running tasks to complete.
   * @note Blocks until all worker threads have exited (i.e. the number of running threads drops to zero).
   * @note This function does not request thread stop itself — call shutdown() or set_thread_count(0) to
   *       cause threads to exit, then call join_all_threads() to wait for completion.
   */
  constexpr void join_all_threads() const noexcept {
    while (const std::size_t num_running_threads = num_running_threads_.load(std::memory_order::acquire)) {
      num_running_threads_.wait(num_running_threads, std::memory_order::acquire);
    }
  }

  /**
   * @brief Wait for all queued tasks to be processed.
   * @note Blocks until there are no queued tasks remaining (i.e. the internal queued counter masked by stop bits is zero).
   * @note This typically indicates that all submitted tasks have been dispatched — if you also need to ensure
   *       currently-running tasks are finished, call join_all_threads() after performing an orderly shutdown.
   */
  constexpr void wait_for_tasks_completion() const noexcept {
    std::size_t num_queued_tasks;
    while ((num_queued_tasks = num_queued_tasks_.load(std::memory_order::acquire)) & ~stop_mask) {
      num_queued_tasks_.wait(num_queued_tasks, std::memory_order::acquire);
    }
  }

  /**
   * @brief Get the const reference to the task queue
   * @warning Thread safety depends on TaskQueue implementation
   */
  const TaskQueue &task_queue() const noexcept { return tasks_; }

  /**
   * @brief Get the thread allocator object
   * @return thread_allocator_type
   */
  [[nodiscard]] constexpr thread_allocator_type get_thread_allocator() const noexcept(noexcept(stop_sources_.get_allocator())) { return stop_sources_.get_allocator(); }

  constexpr ~ThreadPool() noexcept {
    if constexpr (thread_pool::Policy::has_on_pool_destroy<Policy>) { policy_.on_pool_destroy(); }
    shutdown();
    join_all_threads();
  }

 private:
  constexpr std::expected<std::size_t, std::system_error> add_threads(const std::size_t &num, const std::size_t &old_count) noexcept {
    try {
      stop_sources_.reserve(old_count + num);
    } catch (const std::bad_alloc &) { return std::unexpected{std::system_error{std::make_error_code(std::errc::not_enough_memory)}}; } catch (const std::length_error &) {
      return std::unexpected{std::system_error{std::make_error_code(std::errc::invalid_argument)}};
    }

    for (std::size_t i = 0; i < num; ++i) {
      try {
        std::jthread thread{&ThreadPool::worker, this};
        stop_sources_.emplace_back(thread.get_stop_source());
        thread.detach();
      } catch (const std::system_error &err) { return std::unexpected{err}; }
    }

    return old_count;
  }

  constexpr void shrink_threads(const std::size_t &num, const std::size_t &old_count) noexcept {
    bool was_waiting = num_threads_waiting_for_stop_.fetch_add(num, std::memory_order::acquire);
    for (std::size_t i = old_count - num; i < old_count; ++i) { stop_sources_[i].request_stop(); }
    if (!was_waiting && !(num_queued_tasks_.fetch_xor(stop_mask, std::memory_order::release) & stop_mask)) { num_queued_tasks_.notify_all(); }
    stop_sources_.erase(stop_sources_.end() - num, stop_sources_.end());
  }

  constexpr static inline void worker(const std::stop_token &stop_token, ThreadPool *const pool) noexcept {
    pool->num_running_threads_.fetch_add(1, std::memory_order::relaxed);

    if constexpr (thread_pool::Policy::has_on_thread_start<Policy>) { pool->policy_.on_thread_start(); }

    if constexpr (nothrow_bulk_dequeueable<TaskQueue, Task>) {
      std::array<Task, (std::hardware_constructive_interference_size * CHAR_BIT) / 16> tasks;
      while (const std::size_t bulk_size = pool->fetch_task(stop_token, tasks.size())) {
        (void)pool->tasks_.dequeue_bulk(std::span<Task>{tasks.data(), bulk_size});
        for (std::size_t i = 0; i < bulk_size; ++i) { std::invoke(tasks[i]); }
      }
    } else {
      Task task;
      while (pool->fetch_task(stop_token, 1)) {
        (void)pool->tasks_.dequeue(task);
        std::invoke(task);
      }
    }

    if constexpr (thread_pool::Policy::has_on_thread_exit<Policy>) { pool->policy_.on_thread_exit(); }

    // decrement wait-for-stop counter; if this was the last thread, clear stop bits
    if (pool->num_threads_waiting_for_stop_.fetch_sub(1, std::memory_order::acq_rel) == 1 && !(pool->num_queued_tasks_.fetch_xor(stop_mask, std::memory_order::acq_rel) & stop_mask)) [[unlikely]] {
      pool->num_queued_tasks_.notify_all();
    }

    pool->num_running_threads_.fetch_sub(1, std::memory_order::release);
  }

  /**
   * @brief fetch number of tasks to process
   * @param stop_token the stop token to check for stop request
   * @param max_n the maximum number of tasks to process
   * @return std::size_t number of tasks to process. 0 if stop is requested, otherwise at least 1
   */
  constexpr std::size_t fetch_task(const std::stop_token &stop_token, const std::size_t &max_n) noexcept {
    [[assume(max_n > 0)]];
    std::size_t available;
    while (!stop_token.stop_requested()) {
      available = num_queued_tasks_.load(std::memory_order::acquire);
      if (stop_token.stop_requested()) [[unlikely]] { return 0; }
      while (const std::size_t tmp = available & ~stop_mask) {
        if (const std::size_t min = std::min(tmp, max_n); num_queued_tasks_.compare_exchange_weak(available, available - min, std::memory_order::relaxed, std::memory_order::acquire)) {
          [[assume(max_n > 1 || min == 1)]];
          return min;
        } else if (stop_token.stop_requested()) [[unlikely]] {
          return 0;
        }
      }
      num_queued_tasks_.wait(0, std::memory_order::acquire);
    }
    return 0;
  }

  TaskQueue tasks_;
  std::mutex stop_sources_mutex_;
  std::vector<std::stop_source, thread_allocator_type> stop_sources_;
  std::atomic<std::size_t> num_running_threads_{0}, num_threads_waiting_for_stop_{0}, num_queued_tasks_{0};
  std::atomic<ThreadPoolStatus> stop_flag_{ThreadPoolStatus::Running};
  Policy policy_;
};

}  // namespace thread_pool