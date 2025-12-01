# Policy — 概念与 DefaultPolicy

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[English](../en-US/policy.md)

## Policy 钩子概念
所有 Policy 钩子都是可选的——线程池在调用前通过概念检查它们的可用性。

- `has_on_thread_start<Policy>`
  - 检查是否存在 `void on_thread_start() noexcept`。
  - 如果存在，将在每个工作线程启动时调用。

- `has_on_thread_exit<Policy>`
  - 检查是否存在 `void on_thread_exit() noexcept`。
  - 如果存在，将在每个工作线程退出时调用。

- `has_on_pool_destroy<Policy>`
  - 检查是否存在 `void on_pool_destroy() noexcept`。
  - 如果存在，将在 `ThreadPool` 析构函数开始时调用。

- `has_on_pool_stop<Policy>`
  - 检查是否存在 `void on_pool_stop()`（可以是 `noexcept` 或非 `noexcept`）。还有 `has_on_pool_stop_nothrow` 用于测试 `noexcept`。
  - 如果存在，将在过渡到 `Stopping` 状态时调用。

- `has_on_pool_shutdown<Policy>`
  - 检查是否存在 `void on_pool_shutdown()`。还有 `has_on_pool_shutdown_nothrow`。
  - 如果存在，将在过渡到 `Stopped` 状态时调用。

- `has_on_task_enqueue_failed<Policy, Task...>`
  - 检查是否存在 `void on_task_enqueue_failed(Task&&...) noexcept`。
  - 如果存在，将在任务提交失败时（例如，队列已满）调用。它用于回退行为。
  - 如果不存在，失败的任务将被直接丢弃。

## DefaultPolicy
- `DefaultPolicy` 暴露了一个静态的 `on_task_enqueue_failed(Task&&...) noexcept`，它将立即调用提供的任务（回退行为）。
- 对于用户未提供的钩子，它表现为无操作。

> 默认的 `on_task_enqueue_failed` 实现受限于 `noexcept` 可调用任务（与实现中的模板约束匹配）。如果失败的任务不是 `noexcept` 可调用的，`DefaultPolicy` 将不提供回退调用，任务将被丢弃。如果您需要对可能抛出异常的任务提供回退行为，请实现一个自定义的 `Policy::on_task_enqueue_failed` 来安全地处理异常。

## 自定义 Policy 示例
`Policy` 对象可用于观察或挂钩到生命周期事件：

```cpp
struct LoggingPolicy {
  void on_thread_start() noexcept { /* 记录线程启动 */ }
  void on_thread_exit() noexcept { /* 记录线程退出 */ }
  void on_pool_stop() { /* 记录停止 */ }
  void on_pool_shutdown() { /* 记录关闭 */ }
  static void on_task_enqueue_failed(auto&&... tasks) noexcept { /* 回退处理 */ }
};
```

## 注意事项
- 标记为 `noexcept` 的 `Policy` 方法将在 `ThreadPool` 实现的 `noexcept` 上下文中调用；非 `noexcept` 钩子仅在方法签名允许的情况下调用。
- `Policy` 概念检测使库能够为不需要钩子的用户保持性能。