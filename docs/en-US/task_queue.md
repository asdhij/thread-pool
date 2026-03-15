# TaskQueue — Concepts & DefaultQueue

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)

[简体中文](../zh-CN/task_queue.md)

## Queue concepts
- `nothrow_enqueueable<Queue, Args...>`
  - Checks whether `queue.enqueue(args...)` is a `noexcept` operation returning `bool`.

- `nothrow_bulk_enqueueable<Queue, Args...>`
  - Checks whether `queue.enqueue_bulk(args...)` is a `noexcept` operation returning `bool`.
  - Bulk enqueue must be **atomic**: either all tasks are enqueued or none.

- `nothrow_dequeueable<Queue, Ret>`
  - Checks whether `queue.dequeue(ret)` is `noexcept`.
  - The concept does not constrain the return type; implementations commonly return `bool`.

- `nothrow_bulk_dequeueable<Queue, Ret, Extent>`
  - Checks whether `queue.dequeue_bulk(std::span<Ret, Extent>)` is `noexcept`.
  - The concept does not constrain the return type; implementations commonly return a count (e.g., `std::size_t`).
  - If `Queue::dequeue_bulk_size` is defined and effective, it specifies the size of the span. Otherwise, a default size based on cache line size is used. For runtime-determined bulk sizes, `Extent` will be set to `std::dynamic_extent`.

- `thread_pool::task_queue<Queue, Task, Extent>`
  - A type is a `task_queue` for `Task` if `Task` satisfies [`thread_pool::task`](task.md#task-concept) and the `Queue` supports either `nothrow_dequeueable` or `nothrow_bulk_dequeueable` for `Task`.
  - The `Extent` parameter is used to specify the extent for bulk dequeue operations when checking the `nothrow_bulk_dequeueable` concept.

## DefaultQueue\<T>
Default, mutex-protected queue implementation parameterized with `T`:

### Requirements for T (`nothrow_assign_destructible` concept):
  - Move-assign via `move_if_noexcept` is `noexcept`.
  - `T` is not a reference nor const.
  - `T` is `noexcept` destructible.

### Public API:
- Typedefs:
  ```cpp
  using value_type = T;
  using allocator_type = std::pmr::polymorphic_allocator<T>;
  using size_type = std::size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = std::allocator_traits<allocator_type>::pointer;
  using const_pointer = std::allocator_traits<allocator_type>::const_pointer;
  ```

- Constructor:
  ```cpp
  constexpr DefaultQueue() noexcept = default;
  ```

- Capacity
  ```cpp
  // Returns number of stored tasks.
  constexpr size_type size() const noexcept;

  // Returns true if no tasks are stored.
  constexpr bool empty() const noexcept;
  ```

- Modifiers
  ```cpp
  // Constructs a T in-place; returns false if memory allocation fails or if T's construction throws.
  template <typename... Args> requires std::constructible_from<T, Args...>
  constexpr bool enqueue(Args&&... args) noexcept;

  // If queue not empty: moves front into ret (move_if_noexcept) and pops; returns true. Otherwise false.
  constexpr bool dequeue(T& ret) noexcept;

  // Attempts to dequeue up to ret.size() tasks into ret; returns number actually dequeued.
  template <std::size_t Extent> requires (Extent > 0 || Extent == std::dynamic_extent)
  constexpr size_type dequeue_bulk(const std::span<T, Extent>& ret) noexcept;
  ```

### Implementation notes:
- Uses `std::pmr::unsynchronized_pool_resource` and `std::deque` with `std::pmr::polymorphic_allocator` for storage efficiency.
- Protects operations with `std::mutex`; suitable for general-purpose use. If you need a lock-free or specialized queue, implement the required concepts.
- All public methods are `constexpr` where possible and marked `noexcept` according to operations they perform.

## Custom queue example
```cpp
struct CustomQueue {
  thread_pool::DefaultQueue<SimpleTask> impl;
  bool enqueue(SimpleTask&& t) noexcept { return impl.enqueue(std::move(t)); }
  bool dequeue(SimpleTask& t) noexcept { return impl.dequeue(t); }
  bool empty() const noexcept { return impl.empty(); }
};
thread_pool::ThreadPool<SimpleTask, CustomQueue> pool;
```
The above `CustomQueue` wraps `DefaultQueue<SimpleTask>` and satisfies the `task_queue` concept for [`SimpleTask`](task.md#examples).

## Bulk enqueue
When your queue supports `enqueue_bulk` and it is `noexcept`, `ThreadPool::submit` will use the bulk path for multiple arguments (faster and does one atomic enqueue).