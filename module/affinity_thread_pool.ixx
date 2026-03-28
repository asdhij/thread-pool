/**
 * @file affinity_thread_pool.ixx
 * @brief Affinity-based thread pool implementation.
 *
 * This module provides a lightweight thread pool where each worker thread
 * owns a thread-local task queue. The pool exposes per-slot references so
 * callers can obtain a stable pointer to a worker's queue while the worker
 * is active. Synchronization uses a packed atomic (status bits + refcount)
 * per slot and a global running-thread counter.
 *
 * @copyright Copyright (c) 2026 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-02-25
 */

module;
import thread_pool;
import thread_pool.policy;
import thread_pool.queue;
import thread_pool.task;
#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
export module thread_pool.affinity_pool;

namespace thread_pool {

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
} && std::destructible<Alloc>;

template <typename T, typename = void>
struct has_tuple_size : std::false_type {};

template <typename T>
struct has_tuple_size<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

template <typename T>
concept tuple_like = has_tuple_size<std::remove_reference_t<T>>::value && (std::tuple_size<std::remove_reference_t<T>>::value == 0 || requires(T t) { std::get<0>(std::forward<T>(t)); });

template <typename T, typename Tuple>
concept can_make_from_tuple = tuple_like<Tuple> && requires(Tuple tuple) { std::make_from_tuple<T>(std::forward<Tuple>(tuple)); };

template <typename T, typename Tuple>
concept can_nothrow_make_from_tuple = tuple_like<Tuple> && requires(Tuple tuple) { { std::make_from_tuple<T>(std::forward<Tuple>(tuple)) } noexcept; };

namespace slot_status {

constexpr std::uint8_t status_shift = std::numeric_limits<std::size_t>::digits - 2;
constexpr std::size_t ref_count_mask = std::numeric_limits<std::size_t>::max() >> 2;

/**
 * @brief High-bit flags stored inside ThreadSlot::reference_count_.
 *
 * The top two bits are used as status flags while the remaining low bits
 * hold a reference count. Use `ref_count_mask` to extract only the counter
 * portion. The flags are:
 * - `initializing`: set while a worker thread is being created for the slot.
 * - `ready`: set when the worker has completed initialization and the
 *   queue_ pointer is published.
 */
constexpr std::size_t initializing = static_cast<std::size_t>(1) << (status_shift + 1);
constexpr std::size_t ready = static_cast<std::size_t>(1) << status_shift;

}  // namespace slot_status

/**
 * @brief Per-thread slot container.
 *
 * Holds the thread-local queue pointer and a packed atomic used for
 * lightweight reference counting and status flags.
 *
 * @tparam TaskQueue The thread-local queue type used by a worker.
 */
template <std::destructible TaskQueue> requires(!std::is_reference_v<TaskQueue>)
struct alignas(std::hardware_destructive_interference_size) ThreadSlot {
  /**
   * @brief Decrement the packed reference counter and notify a waiter when the low-bit counter reaches zero.
   *
   * @return constexpr std::size_t The previous value of the packed counter.
   */
  constexpr std::size_t release_ref(const std::memory_order& order) noexcept {
    const std::size_t refs = reference_count_.fetch_sub(1, order);
    if ((refs & slot_status::ref_count_mask) == 1) [[unlikely]] { reference_count_.notify_one(); }
    return refs;
  }

  /// @brief Increment the packed reference counter and return the previous value.
  constexpr std::size_t acquire_ref(const std::memory_order& order) noexcept { return reference_count_.fetch_add(1, order); }

  /// @brief Published pointer to the thread-local task queue instance for this slot.
  std::add_pointer_t<TaskQueue> queue_{nullptr};

  /**
   * @brief Packed atomic field containing both reference count and status flags.
   *
   * Low bits are reference counter, high bits are status flags (see `slot_status`).
   * External code must use acquire_ref()/release_ref() to update this field.
   */
  std::atomic<std::size_t> reference_count_{0};
};

/**
 * @brief Scoped reference to a worker slot.
 *
 * RAII handle that holds a reference to a worker slot while the object is
 * alive. Obtain instances via `AffinityThreadPool::get_thread_reference()`.
 * While valid, `operator->()` provides access to the referenced slot's
 * task queue.
 *
 * @note Access the queue only via `operator->()`; storing or using the raw
 * pointer beyond the lifetime of this object is not guaranteed correct and
 * may lead to undefined behaviour.
 *
 * @par Thread-safety
 * - This object MUST NOT be transferred across threads. The thread that
 *   obtains a `ThreadReference` must be the same thread that destroys it.
 * - The same-thread requirement preserves current semantics and permits a
 *   future migration to an RCU-like mechanism for performance while
 *   maintaining backward compatibility.
 *
 * @tparam TaskQueue The task queue type stored in the referenced slot.
 */
export template <std::destructible TaskQueue> requires(!std::is_reference_v<TaskQueue>)
class [[nodiscard]] ThreadReference {
 private:
  using Slot = ThreadSlot<std::remove_const_t<TaskQueue>>;

 public:
  using task_queue_type = TaskQueue;

  constexpr ThreadReference() noexcept : queue_{nullptr}, slot_{nullptr} {}

  constexpr explicit ThreadReference(const std::nullptr_t) noexcept : ThreadReference{} {}
  constexpr ThreadReference& operator=(const std::nullptr_t) noexcept {
    put_slot();
    queue_ = nullptr;
    slot_ = nullptr;
    return *this;
  }

  template <typename Queue> requires std::same_as<std::remove_const_t<Queue>, std::remove_const_t<TaskQueue>> && (std::is_const_v<TaskQueue> || !std::is_const_v<Queue>)
  constexpr ThreadReference(ThreadReference<Queue>&& other) noexcept : queue_{std::exchange(other.queue_, nullptr)}, slot_{std::exchange(other.slot_, nullptr)} {}

  template <typename Queue> requires std::same_as<std::remove_const_t<Queue>, std::remove_const_t<TaskQueue>> && (std::is_const_v<TaskQueue> || !std::is_const_v<Queue>)
  constexpr ThreadReference& operator=(ThreadReference<Queue>&& other) noexcept {
    if (slot_ == other.slot_) { return *this; }
    put_slot();
    queue_ = std::exchange(other.queue_, nullptr);
    slot_ = std::exchange(other.slot_, nullptr);
    return *this;
  }

  ThreadReference(const ThreadReference&) = delete;
  ThreadReference& operator=(const ThreadReference&) = delete;

  template <typename Queue> requires std::same_as<std::remove_const_t<Queue>, std::remove_const_t<TaskQueue>>
  constexpr std::compare_three_way_result_t<Slot*> operator<=>(const ThreadReference<Queue>& other) const noexcept { return std::compare_three_way{}(slot_, other.slot_); }
  constexpr std::compare_three_way_result_t<Slot*> operator<=>(const std::nullptr_t) const noexcept { return std::compare_three_way{}(slot_, static_cast<Slot*>(nullptr)); }

  template <typename Queue> requires std::same_as<std::remove_const_t<Queue>, std::remove_const_t<TaskQueue>>
  constexpr bool operator==(const ThreadReference<Queue>& other) const noexcept { return slot_ == other.slot_; }
  constexpr bool operator==(const std::nullptr_t) const noexcept { return slot_ == nullptr; }

  /**
   * @brief Returns pointer to the referenced task queue.
   *
   * @warning Do not store or use the returned raw pointer beyond the lifetime of
   * this object; doing so is undefined behaviour.
   */
  constexpr std::add_pointer_t<TaskQueue> operator->() noexcept { return queue_; }
  constexpr std::add_pointer_t<std::add_const_t<TaskQueue>> operator->() const noexcept { return queue_; }

  /// @brief True when this reference points to a live slot.
  constexpr explicit operator bool() const noexcept { return slot_ != nullptr; }

  constexpr ~ThreadReference() noexcept { put_slot(); }

 private:
  template <std::destructible Q, std::destructible Policy, allocator>
    requires thread_local_task_queue<std::add_lvalue_reference_t<Q>> && (!std::is_rvalue_reference_v<Q> && !std::is_const_v<std::remove_reference_t<Q>> && !std::is_rvalue_reference_v<Policy>) &&
    (std::numeric_limits<std::size_t>::max() / sizeof(ThreadReference<std::remove_reference_t<Q>>) <= (slot_status::ref_count_mask))
  friend class AffinityThreadPool;

  template <std::destructible Q> requires(!std::is_reference_v<Q>)
  friend class ThreadReference;

  constexpr explicit ThreadReference(Slot* const slot) noexcept : queue_{slot->queue_}, slot_{slot} {}

  /// @brief Release the reference taken on construction or copy. This updates the packed counter in the slot's atomic value.
  constexpr void put_slot() noexcept {
    if (slot_) { slot_->release_ref(std::memory_order::release); }
  }

  std::add_pointer_t<TaskQueue> queue_;
  Slot* slot_;
};

template <typename TaskQueue, typename TaskQueueConstructArgs, typename TaskQueueInitArgs>
concept nothrow_constructible_and_initializable = can_nothrow_make_from_tuple<TaskQueue, TaskQueueConstructArgs> && tuple_like<TaskQueueInitArgs> && requires(TaskQueue tq, TaskQueueInitArgs init_args) {
  {
    static_cast<bool>([]<typename Q, typename T, std::size_t... I>(Q&& q, T&& t, std::index_sequence<I...>) noexcept -> decltype(auto) {
      return std::forward<Q>(q).initialize(std::forward<decltype(std::get<I>(std::forward<T>(t)))>(std::get<I>(std::forward<T>(t)))...);
    }(std::forward<TaskQueue>(tq), std::forward<TaskQueueInitArgs>(init_args), std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<TaskQueueInitArgs>>>{}))
  } noexcept -> std::same_as<bool>;
};

/**
 * @brief Thread exit bookkeeping helper.
 *
 * Clear the `initializing` flag in the slot and decrement the global
 * `num_running_threads` counter. This is called both when a worker fails to
 * initialize and when it exits normally.
 *
 * @note The function does not currently call `num_running_threads.notify_all()` after decrementing the counter.
 */
template <std::destructible TaskQueue>
constexpr void exit_thread(ThreadSlot<TaskQueue>* const slot, std::atomic<std::size_t>& num_running_threads) noexcept {
  slot->reference_count_.fetch_and(~slot_status::initializing, std::memory_order::relaxed);
  num_running_threads.fetch_sub(1, std::memory_order::release);
}

/**
 * @brief Thread pool with per-worker thread-local queues.
 *
 * Manages a fixed set of worker slots and provides operations to start
 * workers and obtain scoped references to per-worker queues.
 *
 * @par Supported Policy hooks
 * - `on_thread_start()` : called at the start of a worker thread if provided.
 * - `on_thread_exit()` : called just before a worker thread exits if provided.
 * - `on_pool_destroy()` : called from the destructor if provided.
 *
 * Other Policy hooks declared in `thread_pool::Policy` are not invoked by
 * this class unless documented here; the Policy concept definitions are
 * available in `module:thread_pool.policy`.
 *
 * @par TaskQueue call sequence
 * The pool invokes `TaskQueue` members for each worker in the following
 * order. Parameter names shown correspond to the parameters passed to the
 * public `start()` call.
 * - Construction: `TaskQueue` is constructed from `task_queue_construct_args`.
 * - Initialization: `TaskQueue::initialize(task_queue_init_args...)` is called.
 *   If `initialize(...)` returns `false` the worker does not run.
 * - Worker loop: repeatedly call `TaskQueue::wait_for_task()`; when it
 *   returns `true`, call `TaskQueue::process_tasks()` to execute pending work.
 * - Exit: the worker stops calling `wait_for_task()` / `process_tasks()` and
 *   the `TaskQueue` instance is destroyed.
 */
export template <std::destructible TaskQueue = DefaultThreadLocalQueue<DefaultTask>, std::destructible Policy = DefaultPolicy, allocator TaskQueueAllocator = std::allocator<TaskQueue>>
  requires thread_local_task_queue<std::add_lvalue_reference_t<TaskQueue>> && (!std::is_rvalue_reference_v<TaskQueue> && !std::is_const_v<std::remove_reference_t<TaskQueue>> && !std::is_rvalue_reference_v<Policy>) &&
  (std::numeric_limits<std::size_t>::max() / sizeof(ThreadReference<std::remove_reference_t<TaskQueue>>) <= (slot_status::ref_count_mask))
class AffinityThreadPool {
 private:
  using Slot = ThreadSlot<std::remove_reference_t<TaskQueue>>;
  using SlotAllocator = typename std::allocator_traits<TaskQueueAllocator>::template rebind_alloc<Slot>;

 public:
  using task_queue_type = TaskQueue;
  using policy_type = Policy;
  using task_queue_allocator_type = TaskQueueAllocator;
  using thread_reference_type = ThreadReference<std::remove_reference_t<TaskQueue>>;
  using const_thread_reference_type = ThreadReference<std::add_const_t<std::remove_reference_t<TaskQueue>>>;

  /**
   * @brief Construct an AffinityThreadPool with the given number of slots.
   *
   * Constructs the `Policy` and the allocator from the provided tuples and
   * prepares internal storage for `thread_count` slots.
   *
   * @param thread_count Initial number of worker slots to allocate.
   * @param policy_args Arguments forwarded to construct the `Policy` instance.
   * @param task_queue_allocator_args Arguments forwarded to construct the allocator used for slot storage.
   *
   * @exception Any exceptions thrown by the constructors of `Policy` or the
   * allocator type are propagated to the caller.
   * @exception std::bad_alloc If allocation of internal storage for slots fails.
   */
  template <tuple_like PolicyArgs = std::tuple<>, tuple_like TaskQueueAllocatorArgs = std::tuple<>> requires can_make_from_tuple<Policy, PolicyArgs> && can_make_from_tuple<SlotAllocator, TaskQueueAllocatorArgs>
  constexpr explicit AffinityThreadPool(const std::size_t& thread_count = std::thread::hardware_concurrency(), PolicyArgs&& policy_args = std::tuple{}, TaskQueueAllocatorArgs&& task_queue_allocator_args = std::tuple{}) :
      policy_{std::make_from_tuple<Policy>(std::forward<PolicyArgs>(policy_args))},
      queues_{thread_count, std::make_from_tuple<SlotAllocator>(std::forward<TaskQueueAllocatorArgs>(task_queue_allocator_args))} {}

  /// @brief Maximum number of worker slots managed by the pool.
  [[nodiscard]] constexpr std::size_t max_thread_count() const noexcept { return queues_.size(); }

  /// @brief Number of currently running worker threads.
  [[nodiscard]] constexpr std::size_t thread_count() const noexcept { return num_running_threads_.load(std::memory_order::acquire); }

  /**
   * @brief Start a worker thread bound to @p thread_id.
   *
   * Starts and detaches a worker for the specified slot. Returns an engaged
   * `std::expected<void, std::error_code>` on success, or an unexpected
   * value containing the failure reason.
   *
   * @note If the newly-created `std::thread` object is destroyed without a
   * successful detach or join, `std::terminate()` will be called. The
   * implementation detaches the worker thread; failures related to detaching
   * or improper destruction may therefore terminate the process.
   *
   * @note If the operation fails due to `std::thread` construction exception, any arguments
   * passed as rvalues (including @p task_queue_construct_args and @p task_queue_init_args ) may
   * have been moved-from and their state is unspecified. Callers should not rely on the
   * original values after a failure unless they are passed as lvalues (which are copied).
   *
   * @param thread_id Index of the slot to bind the worker to.
   * @param task_queue_construct_args Arguments to construct the thread-local queue.
   * @param task_queue_init_args Initialization arguments forwarded to `TaskQueue::initialize`.
   * @return std::expected<void, std::error_code> On success returns an engaged expected object;
   *         on failure returns an `std::unexpected` with one of the following error codes:
   *         - `std::errc::invalid_argument` if @p thread_id is out of range.
   *         - `std::errc::device_or_resource_busy` if the slot is already bound.
   *         - Any system error code produced by `std::thread` construction.
   */
  template <tuple_like TaskQueueConstructArgs = std::tuple<>, tuple_like TaskQueueInitArgs = std::tuple<>> requires nothrow_constructible_and_initializable<TaskQueue, TaskQueueConstructArgs, TaskQueueInitArgs>
  constexpr std::expected<void, std::error_code> start(const std::size_t& thread_id, TaskQueueConstructArgs&& task_queue_construct_args = std::tuple{}, TaskQueueInitArgs&& task_queue_init_args = std::tuple{}) noexcept {
    if (thread_id >= max_thread_count()) [[unlikely]] { return std::unexpected{std::make_error_code(std::errc::invalid_argument)}; }
    Slot* const slot = &queues_[thread_id];
    if (slot->reference_count_.fetch_or(slot_status::initializing, std::memory_order::relaxed) & slot_status::initializing) {
      // This slot has already been bind to a thread, so we cannot start another thread with the same ID
      return std::unexpected{std::make_error_code(std::errc::device_or_resource_busy)};
    }

    num_running_threads_.fetch_add(1, std::memory_order::relaxed);

    try {
      std::thread{[this, slot, task_queue_construct_args = std::forward<TaskQueueConstructArgs>(task_queue_construct_args), task_queue_init_args = std::forward<TaskQueueInitArgs>(task_queue_init_args)] mutable noexcept {
        TaskQueue task_queue{std::make_from_tuple<TaskQueue>(std::forward<TaskQueueConstructArgs>(task_queue_construct_args))};
        if (!static_cast<bool>(std::apply([&task_queue]<typename... Args>(Args&&... args) noexcept { return task_queue.initialize(std::forward<Args>(args)...); }, std::forward<TaskQueueInitArgs>(task_queue_init_args)))) {
          // initialization failed, clear initializing flag and decrement running count
          exit_thread(slot, num_running_threads_);
          return;
        }
        // publish the pointer to the thread-local queue before marking the slot ready
        slot->queue_ = std::addressof(task_queue);
        slot->reference_count_.fetch_or(slot_status::ready, std::memory_order::release);

        if constexpr (thread_pool::Policy::has_on_thread_start<std::add_lvalue_reference_t<Policy>>) { policy_.on_thread_start(); }

        while (task_queue.wait_for_task()) { (void)task_queue.process_tasks(); }

        if constexpr (thread_pool::Policy::has_on_thread_exit<std::add_lvalue_reference_t<Policy>>) { policy_.on_thread_exit(); }

        // Clear the `ready` flag and then wait until all external references
        // (low bits of reference_count_) drop to zero. The release/notify in
        // `release_ref()` will wake this waiter when appropriate.
        for (std::size_t refs = slot->reference_count_.fetch_and(~slot_status::ready, std::memory_order::acq_rel); refs & slot_status::ref_count_mask; refs = slot->reference_count_.load(std::memory_order::acquire)) {
          slot->reference_count_.wait(refs, std::memory_order::relaxed);
        }

        // Reset published pointer before finalizing exit to avoid dangling use
        slot->queue_ = nullptr;
        exit_thread(slot, num_running_threads_);
      }}.detach();
    } catch (const std::system_error& err) {
      exit_thread(slot, num_running_threads_);
      num_running_threads_.notify_all();
      return std::unexpected{err.code()};
    }
    return {};
  }

  /**
   * @brief Obtain a `ThreadReference` for the worker at @p index.
   *
   * Returns a scoped reference to the worker slot's queue, or a null
   * `ThreadReference` if the slot is out-of-range or not available.
   *
   * @param index Index of the worker slot to reference.
   * @return thread_reference_type Scoped slot reference; `false` when invalid.
   */
  constexpr thread_reference_type get_thread_reference(const std::size_t& index) noexcept {
    if (index >= max_thread_count()) [[unlikely]] { return thread_reference_type{nullptr}; }
    Slot& slot = queues_[index];
    if (!(slot.acquire_ref(std::memory_order::acquire) & slot_status::ready)) [[unlikely]] {
      // slot not ready; undo increment and return null reference
      slot.release_ref(std::memory_order::relaxed);
      return thread_reference_type{nullptr};
    }
    return thread_reference_type{&slot};
  }

  /**
   * @brief Obtain a read-only `ThreadReference` for the worker at @p index.
   *
   * This const overload provides read-only access to the worker's task queue.
   * The returned reference cannot be used to modify the queue.
   *
   * @param index Index of the worker slot to reference.
   * @return const_thread_reference_type Scoped read-only slot reference; `false` when invalid.
   *
   * @see get_thread_reference(const std::size_t&)
   */
  constexpr const_thread_reference_type get_thread_reference(const std::size_t& index) const noexcept {
    if (index >= max_thread_count()) [[unlikely]] { return const_thread_reference_type{nullptr}; }
    Slot& slot = queues_[index];
    if (!(slot.acquire_ref(std::memory_order::acquire) & slot_status::ready)) [[unlikely]] {
      // slot not ready; undo increment and return null reference
      slot.release_ref(std::memory_order::relaxed);
      return const_thread_reference_type{nullptr};
    }
    return const_thread_reference_type{&slot};
  }

  /**
   * @brief Wait for all worker threads to exit.
   *
   * Blocks until the internal running-thread counter reaches zero.
   */
  constexpr void join_all_threads() noexcept {
    while (const std::size_t num_running_threads = num_running_threads_.load(std::memory_order::acquire)) { num_running_threads_.wait(num_running_threads, std::memory_order::relaxed); }
  }

  /**
   * @brief Destructor.
   *
   * Performs pool teardown and waits for any running workers to exit.
   *
   * @warning This destructor does not stop any running threads; it only waits for them to exit.
   * Therefore, the user must explicitly request all running threads to exit before destroying
   * the thread pool; otherwise, the destructor will block indefinitely.
   */
  constexpr ~AffinityThreadPool() noexcept {
    if constexpr (thread_pool::Policy::has_on_pool_destroy<std::add_lvalue_reference_t<Policy>>) { policy_.on_pool_destroy(); }
    join_all_threads();
  }

 private:
  [[no_unique_address]] Policy policy_{};
  mutable std::vector<Slot, SlotAllocator> queues_;
  std::atomic<std::size_t> num_running_threads_{0};
};

}  // namespace thread_pool