# ThreadPool — API Reference

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[简体中文](../zh-CN/thread_pool.md)

## Introduction
`thread_pool::ThreadPool` is a generic, templated thread pool intended to be adaptable via dependency injection.

## Template parameters
- `Task` (default: `thread_pool::DefaultTask`)
  - A type satisfying the `thread_pool::task` concept.
- `TaskQueue` (default: `thread_pool::DefaultQueue<Task>`)
  - `Task` storage and retrieval type; must satisfy `thread_pool::task_queue` concept for the `Task` type.
- `Policy` (default: `thread_pool::DefaultPolicy`)
  - Optional hooks (`on_thread_start`, `on_thread_exit`, `on_pool_destroy`, `on_pool_stop`, `on_pool_shutdown`, `on_task_enqueue_failed`).
- `ThreadAllocator` (default: `std::allocator<std::stop_source>`)
  - Allocator used to store `stop_sources_` vector elements.

## Constructors
- Construct with a custom `Policy` instance and forward arguments to construct `TaskQueue`.
  ```cpp
  template <typename Pol, typename... Queue> requires std::constructible_from<Policy, Pol> && std::constructible_from<TaskQueue, Queue...>
  constexpr explicit ThreadPool(Pol &&policy, Queue &&...args) noexcept(/*noex*/);
  ```

- Construct the `TaskQueue` from `args` and use default `Policy`.
  ```cpp
  template <typename... Queue> requires std::constructible_from<TaskQueue, Queue...> && std::is_default_constructible_v<Policy>
  constexpr explicit ThreadPool(Queue &&...args) noexcept(/*noex*/);
  ```

## Primary methods
- Submit one or more tasks.
  ```cpp
  template <typename... F> requires (nothrow_bulk_enqueueable<TaskQueue, F...> || (nothrow_enqueueable<TaskQueue, F> && ...))
  constexpr bool submit(F &&...tasks) noexcept;
  ```
  Returns `false` if pool is not `Running`; `true` if submission attempt was made. Individual task enqueue may still fail — the `Policy`'s `on_task_enqueue_failed` hook will be invoked for failed enqueues if provided.

  Behavior:
  - Uses bulk enqueue when available and appropriate.
  - If queue transitions from empty to non-empty, notifies worker threads (`notify_one`/`notify_all` as appropriate).
  - Updates an atomic counter `num_queued_tasks_` to allow wait-free workers.

- Adjust the number of worker threads to `num`.
  ```cpp
  std::expected<std::size_t, std::system_error> set_thread_count(const std::size_t &num) noexcept;
  ```
  Returns the previous thread count on success. On errors, returns unexpected with `std::system_error`:
  - `errc::operation_not_supported` if pool is not `Running`.
  - `errc::not_enough_memory` if allocation fails adding threads.
  - `errc::invalid_argument` on length errors.
  - other system errors from `std::jthread` constructor.

- Returns the number of running threads (`num_running_threads_`).
  ```cpp
  constexpr std::size_t thread_count() const noexcept;
  ```

- Returns enum `ThreadPoolStatus`: `Running`, `Stopping`, `Stopped`.
  ```cpp
  constexpr ThreadPoolStatus status() const noexcept;
  ```

- Transition pool to `Stopping` state.
  ```cpp
  constexpr ThreadPool& stop() noexcept;
  ```
  Prevents new submissions. Calls `Policy::on_pool_stop()` if available. No threads are requested to stop yet.

- Transition pool to `Stopped` state, request stop on all worker threads, and notify them.
  ```cpp
  constexpr ThreadPool& shutdown() noexcept;
  ```
  Calls `Policy::on_pool_shutdown()` if available.

- Blocks until all worker threads have exited (i.e., the number of running threads drops to zero).
  ```cpp
  constexpr void join_all_threads() const noexcept;
  ```
  This function does not request thread stop itself — call `shutdown()` or `set_thread_count(0)` to cause threads to exit, then call `join_all_threads()` to wait for completion.

- Blocks until there are no queued tasks remaining (i.e., the internal queued counter masked by stop bits is zero).
  ```cpp
  constexpr void wait_for_tasks_completion() const noexcept;
  ```
  This typically indicates that all submitted tasks have been dispatched — if you also need to ensure currently-running tasks are finished, call `join_all_threads()` after performing an orderly `shutdown()`.

- Return const reference to underlying `TaskQueue`.
  ```cpp
  const TaskQueue& task_queue() const noexcept;
  ```
  Thread-safety depends on `TaskQueue` implementation.

- Get the thread allocator object
  ```cpp
  constexpr thread_allocator_type get_thread_allocator() const noexcept(/*noex*/);
  ```

## Destructor
```cpp
constexpr ~ThreadPool() noexcept;
```
Calls `Policy::on_pool_destroy()` if available, then `shutdown()` and `join_all_threads()`.

## Thread behavior and task fetching
Workers are implemented using `std::jthread`. Each worker repeatedly calls `fetch_task()` to determine how many tasks it should take (1 or bulk size). The algorithm uses an atomic `num_queued_tasks_` field and a `stop_mask` bit to coordinate stopping and thread shrink operations.

## Notes on safety and noexcept
- The code relies on `noexcept` `enqueue`, `dequeue`, and bulk APIs for **strong noexcept properties**.
- `Policy` hooks are checked by concepts; only present hooks are called.
- Many public functions are `constexpr` and `noexcept` when template parameter properties allow.

## Examples
See the root [`README.md`](../../README.md#quick-example) for a minimal usage example and the tests in [`main.cc`](../../test/main.cc) for more complete test cases.

## See also
- [`Task`](task.md)
- [`TaskQueue`](task_queue.md)
- [`Policy`](policy.md)