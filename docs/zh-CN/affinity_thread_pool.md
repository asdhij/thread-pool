
# AffinityThreadPool — 每个工作线程本地队列

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[English](../en-US/affinity_thread_pool.md)

## 概述
`thread_pool::AffinityThreadPool` 提供了一个轻量级线程池，其中每个工作线程
维护自己的线程本地任务队列（按槽存放）。线程池管理固定数量的槽，
并通过 `ThreadReference` 暴露有作用域的引用，以便在工作线程处于活动时
让调用方安全访问该线程的队列。

主要特性：
- 每个工作线程独立的本地队列（减少全局出队/处理的争用）。
- 使用打包原子（状态位 + 引用计数）进行轻量级槽管理。
- 支持的 Policy 钩子：`on_thread_start`、`on_thread_exit`、`on_pool_destroy`。
- 使用 `std::thread` 启动工作线程；工作线程被 detached，池在 join 时等待其退出。

## 模板参数
- `TaskQueue`（默认：`thread_pool::DefaultThreadLocalQueue<DefaultTask>`）
  - 每个工作线程使用的本地队列类型，必须满足 `thread_local_task_queue<TaskQueue>` 概念。
- `Policy`（默认：`thread_pool::DefaultPolicy`）
  - 可选的生命周期钩子；仅在可用时调用。
- `TaskQueueAllocator`（默认：用于 TaskQueue 存储的分配器）

## 线程生命周期与 TaskQueue 交互
池对每个工作槽执行以下顺序：
- 使用提供的构造参数构造 `TaskQueue` 实例。
- 调用 `TaskQueue::initialize(...)` 并传入初始化参数；如果其返回 `false`，则该工作线程不运行。
- 发布线程本地队列指针并设置槽的 `ready` 标志。
- 工作循环：`while (TaskQueue::wait_for_task()) { TaskQueue::process_tasks(); }`。
- 退出时：清除 `ready` 标志，等待外部引用计数归零，重置已发布指针并销毁 `TaskQueue`。

## 构造函数
  - 为 `thread_count` 个槽分配内部存储并构造 `Policy` 和 `TaskQueueAllocator`。
  ```cpp
  template <tuple_like PolicyArgs = std::tuple<>, tuple_like TaskQueueAllocatorArgs = std::tuple<>> requires can_make_from_tuple<Policy, PolicyArgs> && can_make_from_tuple<SlotAllocator, TaskQueueAllocatorArgs>
  constexpr explicit AffinityThreadPool(const std::size_t& thread_count = std::thread::hardware_concurrency(), PolicyArgs&& policy_args = std::tuple{}, TaskQueueAllocatorArgs&& task_queue_allocator_args = std::tuple{});
  ```

## 主要 API
- `max_thread_count()`
  - 返回池支持的最大工作线程数量。

- `thread_count()`
  - 返回当前正在运行的工作线程数量。

- `start(size_t thread_id, task_queue_construct_args, task_queue_init_args)`
  - 在指定槽上启动工作线程，返回 `std::expected<void, std::error_code>`。
  - 错误包括：`invalid_argument`（索引越界）、`device_or_resource_busy`（槽已被绑定）及 `std::thread` 构造相关的系统错误。

- `get_thread_reference(size_t index)`
  - 返回一个 `ThreadReference<TaskQueue>`，在该引用存续期间持有对槽队列的有作用域引用；若槽不可用或未就绪则返回空引用。

- `join_all_threads()`
  - 阻塞直到所有运行中的工作线程退出。

## 析构函数
  - 如果可用，调用 `Policy::on_pool_destroy()`，然后等待工作线程退出。
  > 警告：析构函数本身**不会**请求线程停止——请在销毁池之前显式请求线程退出，否则析构可能无限阻塞。

## ThreadReference
`ThreadReference<TaskQueue>` 是一个 RAII 类型，它在存在期间持有对工作槽的引用。通过 `operator->()` 访问队列。
重要限制：
- `ThreadReference` 不得在线程间传递——获取该引用的线程必须负责销毁它。
- 不要在 `ThreadReference` 生命周期之外保存或使用裸指针。

## 备注
- 槽状态使用高位标志（`initializing`、`ready`）和低位引用计数的打包原子表示。外部应通过 `get_thread_reference()` 访问槽以正确管理引用计数。
- `AffinityThreadPool` 适用于需要线程亲和性且希望降低跨线程争用的工作负载。

## 另请参阅
- [`TaskQueue`](task_queue.md)