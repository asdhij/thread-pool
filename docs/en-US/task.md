# Task — Concepts & DefaultTask

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[简体中文](../zh-CN/task.md)

## Task concept
A valid `Task` type must satisfy the `thread_pool::task` concept, which requires that the type:
- Is invocable with no parameters.
- Returns `void`.
- Is `noexcept`-invocable.
- Is `noexcept` default-constructible.
- Is `noexcept` destructible.

## DefaultTask
`DefaultTask` is a compact type-erased wrapper with the following characteristics:
- Holds a `std::unique_ptr` to an internal `Storage` base class.
- `DerivedStorage<F>` stores a `decay_t<F>` callable and invokes it with `std::invoke`.
- `DefaultTask` supports `operator()() noexcept`, which invokes the stored callable if present.
- It is movable (move operations are `noexcept`) and non-copyable.

> Note: `DefaultTask` uses dynamic allocation to type-erase callables and thus constructing a `DefaultTask` may throw `std::bad_alloc`. If you require submissions that are guaranteed not to throw, prefer concrete `Task` types that avoid allocation.

## Usage notes
- Use `DefaultTask` when you need type-erasure for heterogeneous callable submissions.
- Prefer user-defined concrete `Task` types (for example, simple structs carrying state and `operator() noexcept`) for better performance when possible.

## Examples
- Simple custom task:
  ```cpp
  struct SimpleTask {
    std::atomic<int>* counter;
    SimpleTask(std::atomic<int>& c) noexcept : counter(&c) {}
    void operator()() noexcept { if (counter) counter->fetch_add(1); }
  };
  static_assert(thread_pool::task<SimpleTask>);
  ```

- Submitting `DefaultTask`-wrapped callables:
  ```cpp
  thread_pool::ThreadPool<> pool;
  pool.set_thread_count(2);
  pool.submit(thread_pool::DefaultTask([]() noexcept { /* work */ }));
  ```