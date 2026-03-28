
# AffinityThreadPool — Per-worker queues

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[简体中文](../zh-CN/affinity_thread_pool.md)

## Overview
`thread_pool::AffinityThreadPool` provides a lightweight thread pool where
each worker owns a thread-local task queue (a per-slot queue). The pool
manages a fixed set of slots and exposes scoped references (`ThreadReference`)
so callers can obtain a stable pointer to a worker's queue while the worker
is active.

Key characteristics:
- Per-worker thread-local queues (no global contention for dequeue/process).
- Lightweight slot bookkeeping using a packed atomic (status bits + refcount).
- Policy hooks supported: `on_thread_start`, `on_thread_exit`, `on_pool_destroy`.
- Uses `std::thread` for workers; workers are detached and the pool waits
  for them to exit when joining.

## Template parameters
- `TaskQueue` (default: `thread_pool::DefaultThreadLocalQueue<DefaultTask>`)
  - Thread-local queue type stored and run by each worker. Must satisfy
    `thread_local_task_queue<TaskQueue>`.
- `Policy` (default: `thread_pool::DefaultPolicy`)
  - Optional lifecycle hooks; only available hooks are invoked.
- `TaskQueueAllocator` (default: allocator for `TaskQueue` storage)

## Thread lifecycle and TaskQueue interaction
The pool follows this sequence for each worker slot:
- Construct a `TaskQueue` from the provided construct arguments.
- Call `TaskQueue::initialize(...)` with the provided init args. If
  initialization returns `false`, the worker will not run.
- Publish the pointer to the thread-local queue and set the slot `ready`
  flag.
- Worker loop: `while (TaskQueue::wait_for_task()) { TaskQueue::process_tasks(); }`.
- On exit: clear the `ready` flag and wait until external references drop to
  zero, reset the published pointer, and destroy the `TaskQueue`.

## Constructor
  - Constructs internal storage for `thread_count` slots and the `Policy` and `TaskQueueAllocator`.
  ```cpp
  template <tuple_like PolicyArgs = std::tuple<>, tuple_like TaskQueueAllocatorArgs = std::tuple<>> requires can_make_from_tuple<Policy, PolicyArgs> && can_make_from_tuple<SlotAllocator, TaskQueueAllocatorArgs>
  constexpr explicit AffinityThreadPool(const std::size_t& thread_count = std::thread::hardware_concurrency(), PolicyArgs&& policy_args = std::tuple{}, TaskQueueAllocatorArgs&& task_queue_allocator_args = std::tuple{});
  ```

## Primary API
- `max_thread_count()`
  - Returns the maximum number of worker threads supported by the pool.

- `thread_count()`
  - Returns the current number of running worker threads.

- `start(size_t thread_id, task_queue_construct_args, task_queue_init_args)`
  - Starts a worker bound to `thread_id`. Returns `std::expected<void, std::error_code>`.
  - Errors include: `invalid_argument` (out of range), `device_or_resource_busy` (slot already bound), and system errors from `std::thread` construction.

- `get_thread_reference(size_t index)`
  - Returns a `ThreadReference<TaskQueue>` which holds a scoped reference to the
    slot's queue while the reference lives. Returns a null reference if the
    slot is out-of-range or not ready.

- `join_all_threads()`
  - Blocks until all running worker threads have exited.

## Destructor
  - Calls `Policy::on_pool_destroy()` (if available) and waits for workers to exit.
  > Warning: The destructor does **not** itself request workers to stop — callers
    must request thread exit (for example via queue stop semantics) before
    destroying the pool; otherwise destruction may block indefinitely.

## ThreadReference
`ThreadReference<TaskQueue>` is an RAII handle that holds a reference to a
worker slot while alive. Use `operator->()` to access the referenced queue.
Important constraints:
- `ThreadReference` must not be transferred between threads — the same
  thread that obtains the reference must destroy it.
- Do not store the raw queue pointer beyond the lifetime of the `ThreadReference`.

## Notes
- The slot state uses high-bit flags (`initializing`, `ready`) combined with a
  low-bit reference count in a packed atomic. External users should obtain
  access only via `get_thread_reference()` which manages the reference count.
- `AffinityThreadPool` intentionally exposes per-slot queues for workloads that
  benefit from affinity and low cross-thread contention.

## See also
- [`TaskQueue`](task_queue.md)