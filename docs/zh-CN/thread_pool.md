# ThreadPool — API 参考

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[English](../en-US/thread_pool.md)

## 简介
`thread_pool::ThreadPool` 是一个通用的模板化线程池，旨在通过依赖注入进行适配。

## 模板参数
- `Task`（默认：`thread_pool::DefaultTask`）
  - 满足 `thread_pool::task` 概念的类型。
- `TaskQueue`（默认：`thread_pool::DefaultQueue<Task>`）
  - `Task` 存储和检索类型；必须满足针对 `Task` 类型的 `thread_pool::task_queue` 概念。
- `Policy`（默认：`thread_pool::DefaultPolicy`）
  - 可选钩子（`on_thread_start`、`on_thread_exit`、`on_pool_destroy`、`on_pool_stop`、`on_pool_shutdown`、`on_task_enqueue_failed`）。
- `ThreadAllocator`（默认：`std::allocator<std::stop_source>`）
  - 用于存储 `stop_sources_` 向量元素的分配器。

## 构造函数
- 使用自定义 `Policy` 实例构造，并转发参数来构造 `TaskQueue`。
  ```cpp
  template <typename Pol, typename... Queue> requires std::constructible_from<Policy, Pol> && std::constructible_from<TaskQueue, Queue...>
  constexpr explicit ThreadPool(Pol &&policy, Queue &&...args) noexcept(/*noex*/);
  ```

- 从 `args` 构造 `TaskQueue` 并使用默认 `Policy`。
  ```cpp
  template <typename... Queue> requires std::constructible_from<TaskQueue, Queue...> && std::is_default_constructible_v<Policy>
  constexpr explicit ThreadPool(Queue &&...args) noexcept(/*noex*/);
  ```

- 使用 `TaskQueue`、`Policy` 和 `ThreadAllocator` 的单独参数元组构造（按此顺序）。
  ```cpp
  template <tuple_like QueueArgs, tuple_like PolicyArgs = std::tuple<>, tuple_like ThreadAllocatorArgs = std::tuple<>>
    requires can_make_from_tuple<TaskQueue, QueueArgs> && can_make_from_tuple<Policy, PolicyArgs> && can_make_from_tuple<ThreadAllocator, ThreadAllocatorArgs>
  constexpr explicit ThreadPool(QueueArgs &&queue_args, PolicyArgs &&policy_args = std::tuple{}, ThreadAllocatorArgs &&thread_allocator_args = std::tuple{}) noexcept(/*noex*/);
  ```

- 使用默认参数构造。
  ```cpp
  constexpr ThreadPool() noexcept(std::is_nothrow_default_constructible_v<Policy> && std::is_nothrow_default_constructible_v<TaskQueue> && std::is_nothrow_default_constructible_v<ThreadAllocator>)
    requires(std::is_default_constructible_v<Policy> && std::is_default_constructible_v<TaskQueue> && std::is_default_constructible_v<ThreadAllocator>) = default;
  ```
  仅当所有三个模板参数都可默认构造时才参与重载解析。

## 主要方法
- 提交一个或多个任务。
  ```cpp
  template <typename... F> requires (nothrow_bulk_enqueueable<TaskQueue, F...> || (nothrow_enqueueable<TaskQueue, F> && ...))
  constexpr bool submit(F &&...tasks) noexcept;
  ```
  如果线程池不处于 `Running` 状态则返回 `false`；如果尝试提交则返回 `true`。单个任务入队可能仍然失败——如果提供了 `Policy` 的 `on_task_enqueue_failed` 钩子，它将被调用于处理失败的入队。

  行为：
  - 在可用且适当时使用批量入队。
  - 如果队列从空变为非空，通知工作线程（适当地使用 `notify_one`/`notify_all`）。
  - 更新原子计数器 `num_queued_tasks_` 以允许无等待的工作线程。

- 将工作线程数量调整为 `num`。
  ```cpp
  std::expected<std::size_t, std::system_error> set_thread_count(const std::size_t &num) noexcept;
  ```
  成功时返回先前的线程数。出错时，返回包含 `std::system_error` 的 unexpected：
  - 如果线程池不处于 `Running` 状态，返回 `errc::operation_not_supported`。
  - 如果添加线程时分配失败，返回 `errc::not_enough_memory`。
  - 长度错误时返回 `errc::invalid_argument`。
  - 来自 `std::jthread` 构造函数的其他系统错误。

- 返回运行中的线程数量（`num_running_threads_`）。
  ```cpp
  constexpr std::size_t thread_count() const noexcept;
  ```

- 返回枚举 `ThreadPoolStatus`：`Running`、`Stopping`、`Stopped`。
  ```cpp
  constexpr ThreadPoolStatus status() const noexcept;
  ```

- 将线程池过渡到 `Stopping` 状态。
  ```cpp
  constexpr ThreadPool& stop() noexcept;
  ```
  阻止新提交。如果可用，调用 `Policy::on_pool_stop()`。尚未请求任何线程停止。

- 将线程池过渡到 `Stopped` 状态，请求所有工作线程停止，并通知它们。
  ```cpp
  constexpr ThreadPool& shutdown() noexcept;
  ```
  如果可用，调用 `Policy::on_pool_shutdown()`。

- 阻塞直到所有工作线程都已退出（即运行中的线程数量降至零）。
  ```cpp
  constexpr void join_all_threads() const noexcept;
  ```
  此函数本身不请求线程停止——调用 `shutdown()` 或 `set_thread_count(0)` 来使线程退出，然后调用 `join_all_threads()` 来等待完成。

- 阻塞直到没有剩余排队任务（即内部排队计数器被停止位掩码后为零）。
  ```cpp
  constexpr void wait_for_tasks_completion() const noexcept;
  ```
  这通常表示所有已提交的任务都已分派——如果您还需要确保当前正在运行的任务完成，请在执行有序的 `shutdown()` 后调用 `join_all_threads()`。

- 返回底层 `TaskQueue` 的常量引用。
  ```cpp
  const TaskQueue& task_queue() const noexcept;
  ```
  线程安全性取决于 `TaskQueue` 实现。

- 获取线程分配器对象
  ```cpp
  constexpr thread_allocator_type get_thread_allocator() const noexcept(/*noex*/);
  ```

## 析构函数
```cpp
constexpr ~ThreadPool() noexcept;
```
如果可用，调用 `Policy::on_pool_destroy()`，然后调用 `shutdown()` 和 `join_all_threads()`。

## 线程行为与任务获取
工作线程使用 `std::jthread` 实现。每个工作线程重复调用 `fetch_task()` 来确定它应该获取多少任务（1 个或批量大小）。该算法使用原子字段 `num_queued_tasks_` 和 `stop_mask` 位来协调停止和线程收缩操作。

## 安全性与 noexcept 注意事项
- 代码依赖 `noexcept` 的 `enqueue`、`dequeue` 和批量 API 来确保**强 noexcept 属性**。
- `Policy` 钩子通过概念检查；仅调用存在的钩子。
- 当模板参数属性允许时，许多公共函数都是 `constexpr` 和 `noexcept`。

## 示例
请参阅 [`README.md`](README.md#快速示例) 获取最小使用示例，并参阅 [`main.cc`](../../test/main.cc) 中的测试以获取更完整的测试用例。

## 另请参阅
- [`Task`](task.md)
- [`TaskQueue`](task_queue.md)
- [`Policy`](policy.md)