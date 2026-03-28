# TaskQueue — 概念与 DefaultQueue

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[English](../en-US/task_queue.md)

## 队列概念
- `nothrow_enqueueable<Queue, Args...>`
  - 检查 `queue.enqueue(args...)` 是否为返回 `bool` 的 `noexcept` 操作。

- `nothrow_bulk_enqueueable<Queue, Args...>`
  - 检查 `queue.enqueue_bulk(args...)` 是否为返回 `bool` 的 `noexcept` 操作。
  - 批量入队必须是**原子性**的：要么所有任务都入队，要么一个都不入队。

- `nothrow_dequeueable<Queue, Ret>`
  - 检查 `queue.dequeue(ret)` 是否为 `noexcept`。
  - 该概念不约束返回类型；实现通常返回 `bool`。

- `nothrow_bulk_dequeueable<Queue, Ret, Extent>`
  - 检查 `queue.dequeue_bulk(std::span<Ret, Extent>)` 是否为 `noexcept`。
  - 该概念不约束返回类型；实现通常返回一个计数（例如 `std::size_t`）。
  - 如果 `Queue::dequeue_bulk_size` 已定义且有效，则它指定了 span 的大小。否则，使用基于缓存行大小的默认大小。对于运行时确定的批量大小，`Extent` 将设置为 `std::dynamic_extent`。

- `thread_pool::task_queue<Queue, Task, Extent>`
  - 如果 `Task` 满足 [`thread_pool::task`](task.md#task-概念) 概念，并且 `Queue` 支持针对 `Task` 的 `nothrow_dequeueable` 或 `nothrow_bulk_dequeueable`，则该类型是 `Task` 的 `task_queue`。
  - `Extent` 参数用于在检查 `nothrow_bulk_dequeueable` 概念时指定批量出队操作的范围。

- `thread_pool::thread_local_task_queue<Queue>`
  - 表示一种由单个工作线程拥有的队列类型，适用于亲和性风格的池；需要提供最小接口：`process_tasks()`（无异常）和 `wait_for_task()`（无异常 -> `bool`），后者在有工作或请求停止时返回。

## DefaultQueue\<T>
使用 `T` 参数化的默认互斥锁保护队列实现：

### 对 T 的要求（`nothrow_assign_destructible` 概念）：
  - 通过 `move_if_noexcept` 进行移动赋值是 `noexcept`。
  - `T` 不是引用类型也不是 const 类型。
  - `T` 是 `noexcept` 可析构的。

### 公共 API：
- 类型定义：
  ```cpp
  using value_type = T;
  using allocator_type = std::pmr::polymorphic_allocator<T>;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = std::allocator_traits<allocator_type>::pointer;
  using const_pointer = std::allocator_traits<allocator_type>::const_pointer;
  ```

- 构造函数：
  ```cpp
  constexpr DefaultQueue() noexcept = default;
  ```

- 容量
  ```cpp
  // 返回存储的任务数量。
  constexpr size_type size() const noexcept;

  // 如果没有存储任务则返回 true。
  constexpr bool empty() const noexcept;
  ```

- 修改器
  ```cpp
  // 原地构造一个 T；如果内存分配失败或 T 的构造函数抛出异常则返回 false。
  template <typename... Args> requires std::constructible_from<T, Args...>
  constexpr bool enqueue(Args&&... args) noexcept;

  // 如果队列非空：将前端元素移动到 ret（使用 move_if_noexcept）并弹出；返回 true。否则返回 false。
  constexpr bool dequeue(T& ret) noexcept;

  // 尝试将最多 ret.size() 个任务出队到 ret 中；返回实际出队的数量。
  template <std::size_t Extent> requires (Extent > 0 || Extent == std::dynamic_extent)
  constexpr size_type dequeue_bulk(const std::span<T, Extent>& ret) noexcept;
  ```

### 实现说明：
- 使用 `std::pmr::unsynchronized_pool_resource` 和带有 `std::pmr::polymorphic_allocator` 的 `std::deque` 以提高存储效率。
- 使用 `std::mutex` 保护操作；适用于通用用途。如果您需要无锁或专用队列，请实现所需的概念。
- 所有公共方法在可能的情况下都是 `constexpr`，并根据其执行的操作标记为 `noexcept`。

## DefaultThreadLocalQueue\<T>

该库还提供了 `DefaultThreadLocalQueue<T>`，一个简单的线程本地队列，旨在由单个工作线程拥有。对于线程本地队列，框架期望两个操作：

- `process_tasks()` — 执行待处理的任务（无异常抛出）。
- `wait_for_task()` — 阻塞直到有工作可用或请求停止；当有任务时返回 `true`，`false` 表示调用者应退出（无异常抛出 -> `bool`）。

此外，线程本地队列应提供一个 `initialize(...)` 可调用对象，返回一个可转换为 `bool` 的值以指示初始化成功。

`DefaultThreadLocalQueue<T>` 实现了这些行为：
- `constexpr bool initialize() noexcept` — 默认返回 `true`。
- `enqueue(Args&&...) noexcept` — 原地构造一个任务并通知等待者；如果请求停止或分配/构造失败，返回 `false`。
- `process_tasks() noexcept` — 执行并弹出所有排队的任务（调用者必须是拥有者线程）。
- `wait_for_task() noexcept -> bool` — 在条件变量上阻塞，直到有任务可用或请求停止；存在任务时返回 `true`。
- `notify_for_stop() noexcept` — 将队列标记为停止并唤醒等待者。

该实现使用 `std::pmr::unsynchronized_pool_resource` 进行存储，并使用 `std::condition_variable` 实现工作线程的阻塞等待。

## 自定义队列示例
```cpp
struct CustomQueue {
  thread_pool::DefaultQueue<SimpleTask> impl;
  bool enqueue(SimpleTask&& t) noexcept { return impl.enqueue(std::move(t)); }
  bool dequeue(SimpleTask& t) noexcept { return impl.dequeue(t); }
  bool empty() const noexcept { return impl.empty(); }
};
thread_pool::ThreadPool<SimpleTask, CustomQueue> pool;
```
上述 `CustomQueue` 包装了 `DefaultQueue<SimpleTask>`，并满足针对 [`SimpleTask`](task.md#示例) 的 `task_queue` 概念。

## 批量入队
当您的队列支持 `noexcept` 的 `enqueue_bulk` 时，`ThreadPool::submit` 将对多个参数使用批量路径（更快且执行一次原子性入队）。

### 批量出队大小常量
该实现暴露了一个默认的批量出队大小常量，当队列未提供 `dequeue_bulk_size` 时使用：

```cpp
constexpr std::size_t dequeue_bulk_size_v = (std::hardware_constructive_interference_size * CHAR_BIT) / 16;
```

如果队列定义了一个有效的 `Queue::dequeue_bulk_size`，则使用该值。