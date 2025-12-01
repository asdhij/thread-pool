# Policy — Concepts & DefaultPolicy

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[简体中文](../zh-CN/policy.md)

## Policy hook concepts
All policy hooks are optional — the pool checks for their availability via concepts before calling.

- `has_on_thread_start<Policy>`
  - Checks for `void on_thread_start() noexcept`.
  - If present, it will be called at the start of each worker thread.

- `has_on_thread_exit<Policy>`
  - Checks for `void on_thread_exit() noexcept`.
  - If present, it will be called at the exit of each worker thread.

- `has_on_pool_destroy<Policy>`
  - Checks for `void on_pool_destroy() noexcept`.
  - If present, it will be called at the start of the `ThreadPool` destructor.

- `has_on_pool_stop<Policy>`
  - Checks for `void on_pool_stop()` (may be `noexcept` or not). There is also `has_on_pool_stop_nothrow` to test for `noexcept`.
  - If present, it will be called when transitioning to the `Stopping` state.

- `has_on_pool_shutdown<Policy>`
  - Checks for `void on_pool_shutdown()`. There is also `has_on_pool_shutdown_nothrow`.
  - If present, it will be called when transitioning to the `Stopped` state.

- `has_on_task_enqueue_failed<Policy, Task...>`
  - Checks for `void on_task_enqueue_failed(Task&&...) noexcept`.
  - If present, it will be called when task submission fails (e.g., queue full). It is used for fallback behavior.
  - If not present, failed tasks are simply dropped.

## DefaultPolicy
- `DefaultPolicy` exposes a static `on_task_enqueue_failed(Task&&...) noexcept` that will immediately invoke the provided tasks (fallback behavior).
- It is a no-op for hooks that are not provided by the user.

> The default `on_task_enqueue_failed` implementation is constrained to `noexcept`-invocable tasks (matching the template constraint in the implementation). If a failed task is not `noexcept`-invocable, `DefaultPolicy` will not provide a fallback invocation and the task will be dropped. If you require fallback behavior for throwing tasks, implement a custom `Policy::on_task_enqueue_failed` that safely handles exceptions.

## Custom Policy example
`Policy` objects can be used to observe or hook into lifecycle events:

```cpp
struct LoggingPolicy {
  void on_thread_start() noexcept { /* log thread start */ }
  void on_thread_exit() noexcept { /* log thread exit */ }
  void on_pool_stop() { /* log stop */ }
  void on_pool_shutdown() { /* log shutdown */ }
  static void on_task_enqueue_failed(auto&&... tasks) noexcept { /* fallback */ }
};
```

## Notes
- `Policy` methods that are marked `noexcept` will be called in `noexcept` contexts in the `ThreadPool` implementation; non-`noexcept` hooks are only called where the method signatures allow it.
- The `Policy` concept detection allows the library to maintain performance for users who do not need hooks.