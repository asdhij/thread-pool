# thread-pool (C++23)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)
![Star Badge](https://img.shields.io/github/stars/asdhij/thread-pool)

[简体中文](docs/zh-CN/README.md)

## Overview

thread-pool is a compact, cross-platform thread pool library written for C++23 using modules and dependency injection. The design emphasizes flexibility, zero-cost abstractions where possible, and safe, explicit semantics. You can customize the pool with:

- Custom `Task` types (any callable satisfying the library's `task` concept)
- Custom `TaskQueue` implementations (custom queue/storage strategies)
- Custom `Policy` hooks (optional lifecycle hooks called at various points)
- Custom `ThreadAllocator` type

### Key goals
- Minimal runtime overhead: most cost comes from `Task` and `TaskQueue` implementations.
- Highly customizable through concepts and templates.
- Optional hooks (no need to implement all policy methods).
- Dynamic thread management: increase/decrease thread count at runtime.
- Designed for `noexcept` usage where possible to make behavior predictable.

The implementation favors `noexcept` operations and clearly defines where hooks and allocations may occur. It uses modern C++ features (modules, concepts, `std::jthread`, `std::stop_token`/`stop_source`, and `std::expected`) and ships with sensible defaults:

- `DefaultTask` — A type-erased callable wrapper intended for `noexcept` callables (construction may allocate).
- `DefaultQueue<T>` — Mutex-protected queue using `std::pmr::unsynchronized_pool_resource`.
- `DefaultPolicy` — Default policy with sensible behavior (invokes failed-to-enqueue tasks when applicable).

### Why use this library?
- You need a flexible thread pool that can be adapted to different task and queue types.
- You want to leverage C++23 features for better concurrency and resource management.
- You want to avoid the complexity of building a thread pool from scratch while retaining control over its behavior.
- You want to experiment with different scheduling and queuing strategies via dependency injection.
- You want a thread pool that can be easily integrated into modern C++ projects using modules and CMake.

### Why not use this library?
- You need a ready-to-use and very high-performance thread pool with lock-free queues and minimal overhead; consider specialized libraries.
- You are using an older C++ standard (pre-C++23) that does not support the required features.

## Contents
- [Quick integration](#quick-integration)
  - [Prerequisites](#prerequisites)
  - [Integration methods](#integration-methods)
  - [Uninstallation](#uninstallation)
- [Quick example](#quick-example)
- [Testing](#testing)
- [Documentation](#documentation)
  - [Doxygen](#doxygen)
  - [API Reference](#api-reference)
- [Notes](#notes)
- [Contributing](#contributing)

## Quick integration

### Prerequisites

- C++23-compatible compiler (GCC 13+, Clang 16+, MSVC 2022 17.8+).
- CMake 3.28+ (for building).
- A CMake generator that supports C++20 modules, such as **Ninja** (Visual Studio and Unix Makefiles may have issues with module dependencies in some versions).

### Integration methods

#### 1. Add the repository as a submodule:
```bash
git submodule add https://github.com/asdhij/thread-pool.git external/thread-pool
```
In your `CMakeLists.txt`:
```cmake
add_subdirectory(external/thread-pool)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 2. Install and use `find_package`
- Install the library to your system or packaging prefix (provide a CMake install step in the project).
```bash
git clone https://github.com/asdhij/thread-pool.git
cd thread-pool
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local -G Ninja
cmake --install build  # Note: may need `sudo` for system directories
```
- In your `CMakeLists.txt`:
```cmake
find_package(thread-pool REQUIRED)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 3. Use `FetchContent`
In your `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
  thread-pool
  GIT_REPOSITORY https://github.com/asdhij/thread-pool.git
  GIT_TAG        main  # This will always pull the latest code from the `main` branch. You may also use a specific release version or tag
)
FetchContent_MakeAvailable(thread-pool)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 4. CPM
In your `CMakeLists.txt`:
```cmake
include(CPM.cmake)  # ensure CPM.cmake is included
CPMAddPackage(
  NAME thread-pool
  GITHUB_REPOSITORY asdhij/thread-pool
  GIT_TAG           main  # use a specific tag or commit for stability
)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```
For more information on how to add CPM to your project, see the [CPM documentation](https://github.com/cpm-cmake/CPM.cmake#adding-cpm).

### Uninstallation
To uninstall the library, you can either manually remove the installed files or use the following script:
```bash
cmake --build build --target uninstall
```

## Quick example
```cpp
import thread_pool;
#include <cstdio>

int main() {
  // Use defaults: DefaultTask, DefaultQueue<DefaultTask>, DefaultPolicy, default allocator.
  thread_pool::ThreadPool<> pool;
  pool.set_thread_count(4); // Set number of worker threads

  pool.submit([] noexcept {
    // Some work. The callable must be `noexcept` and return `void` to satisfy the task concept.
    std::printf("Hello from worker\n");
  });

  pool.stop().wait_for_tasks_completion(); // Request stop and wait for queued tasks to be processed
  return 0;
}
```

## Testing
The project includes a test suite using **GTest**. To build and run the tests:
```bash
cmake -B build -S . -G Ninja -DTHREAD_POOL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```
> Make sure you have [GTest](https://github.com/google/googletest) installed.

## Documentation
- [ThreadPool class (API)](docs/en-US/thread_pool.md)
- [AffinityThreadPool (API)](docs/en-US/affinity_thread_pool.md)
- [Task concept & DefaultTask](docs/en-US/task.md)
- [TaskQueue concepts & DefaultQueue](docs/en-US/task_queue.md)
- [Policy concepts & DefaultPolicy](docs/en-US/policy.md)

### Doxygen
The project includes Doxygen documentation for the API. If Doxygen is installed, you can build the documentation with:
```bash
cmake --build build --target thread-pool-docs
```
The generated documentation will be located at `build/docs/html/index.html`. Open this file in your web browser to view the API reference.
> If Doxygen is not found, the documentation target will be skipped.

### API Reference
- [ThreadPool](docs/en-US/thread_pool.md) (template)
  - [Constructors](docs/en-US/thread_pool.md#constructors)
  - [Primary methods](docs/en-US/thread_pool.md#primary-methods)
    - `submit`
    - `set_thread_count`
    - `thread_count`
    - `status`
    - `stop`
    - `shutdown`
    - `join_all_threads`
    - `wait_for_tasks_completion`
    - `task_queue`
    - `get_thread_allocator`
  - [Destructor](docs/en-US/thread_pool.md#destructor)

- [AffinityThreadPool](docs/en-US/affinity_thread_pool.md)
  - [Constructors](docs/en-US/affinity_thread_pool.md#constructor)
  - [Primary methods](docs/en-US/affinity_thread_pool.md#primary-api)
    - `start`
    - `get_thread_reference`
    - `join_all_threads`
    - `max_thread_count`
    - `thread_count`
  - [Destructor](docs/en-US/affinity_thread_pool.md#destructor)

## Notes
- The library relies on `noexcept` guarantees and atomic `wait`/`notify` (C++20/C++23 primitives).
- The `DefaultQueue` uses `std::pmr::unsynchronized_pool_resource` for memory efficiency — you can swap in a lock-free queue if needed.
- `Policy` hooks are optional; the pool checks for their presence via concepts and only calls them when available.

## Contributing
Contributions, issues, and feature requests are welcome! Please note:

### For Code Changes:
- ✅ Maintain thread safety
- ✅ Follow existing code style and formatting
- ✅ Add tests for your changes
- ✅ Ensure all tests pass

### Process:
1. For major features, please open an issue first to discuss
2. Update documentation as needed

### Questions?
Feel free to open an issue for any questions!