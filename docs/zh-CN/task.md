# Task — 概念与 DefaultTask

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[English](../en-US/task.md)

## Task 概念
一个有效的 `Task` 类型必须满足 `thread_pool::task` 概念，这要求该类型：
- 可在无参数情况下调用
- 返回 `void`
- 是 `noexcept` 可调用的
- 是 `noexcept` 可默认构造的
- 是 `noexcept` 可析构的

## DefaultTask
`DefaultTask` 是一个紧凑的类型擦除包装器，具有以下特征：
- 持有指向内部 `Storage` 基类的 `std::unique_ptr`
- `DerivedStorage<F>` 存储一个 `decay_t<F>` 可调用对象，并使用 `std::invoke` 调用它
- `DefaultTask` 支持 `operator()() noexcept`，如果存在存储的可调用对象则调用它
- 它是可移动的（移动操作是 `noexcept`）且不可复制的

> 注意：`DefaultTask` 使用动态分配来实现类型擦除，因此构造 `DefaultTask` 可能抛出 `std::bad_alloc`。如果您需要保证不抛出的提交，请优先使用避免分配的具体 `Task` 类型。

## 使用说明
- 当您需要为异构可调用对象提交进行类型擦除时，使用 `DefaultTask`
- 为了获得更好的性能，尽可能优先使用用户定义的具体 `Task` 类型（例如，携带状态和 `operator() noexcept` 的简单结构体）

## 示例
- 简单的自定义任务：
  ```cpp
  struct SimpleTask {
    std::atomic<int>* counter;
    SimpleTask(std::atomic<int>& c) noexcept : counter(&c) {}
    void operator()() noexcept { if (counter) counter->fetch_add(1); }
  };
  static_assert(thread_pool::task<SimpleTask>);
  ```

- 提交 `DefaultTask` 包装的可调用对象：
  ```cpp
  thread_pool::ThreadPool<> pool;
  pool.set_thread_count(2);
  pool.submit(thread_pool::DefaultTask([]() noexcept { /* work */ }));
  ```