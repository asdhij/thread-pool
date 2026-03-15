# thread-pool (C++23)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](../../LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)
![Star Badge](https://img.shields.io/github/stars/asdhij/thread-pool)

[English](../../README.md)

## 概述

thread-pool 是一个紧凑、跨平台的线程池库，使用 C++23 的模块和依赖注入编写。设计强调灵活性、尽可能的零成本抽象以及安全明确的语义。您可以通过以下方式自定义线程池：

- 自定义 `Task` 类型（任何满足库的 `task` 概念的可调用对象）
- 自定义 `TaskQueue` 实现（自定义队列/存储策略）
- 自定义 `Policy` 钩子（在不同生命周期点调用的可选钩子）
- 自定义 `ThreadAllocator` 类型

### 主要目标
- 最小运行时开销：主要成本来自 `Task` 和 `TaskQueue` 的实现。
- 通过概念和模板高度可定制。
- 可选钩子（无需实现所有策略方法）。
- 动态线程管理：运行时增加/减少线程数量。
- 尽可能设计为 `noexcept` 使用，使行为可预测。

实现优先考虑 `noexcept` 操作，并明确定义钩子和内存分配可能发生的位置。它使用现代 C++ 特性（模块、概念、`std::jthread`、`std::stop_token`/`stop_source` 和 `std::expected`）并提供了合理的默认实现：

- `DefaultTask` — 类型擦除的可调用包装器，适用于 `noexcept` 可调用对象（构造时可能分配内存）。
- `DefaultQueue<T>` — 使用 `std::pmr::unsynchronized_pool_resource` 的互斥锁保护队列。
- `DefaultPolicy` — 具有合理行为的默认策略（在适用时调用入队失败的任务）。

### 为什么使用这个库？
- 您需要一个灵活的线程池，可以适应不同的任务和队列类型。
- 您希望利用 C++23 特性获得更好的并发性和资源管理。
- 您希望避免从头构建线程池的复杂性，同时保留对其行为的控制。
- 您希望通过依赖注入尝试不同的调度和队列策略。
- 您需要一个可以轻松集成到使用模块和 CMake 的现代 C++ 项目中的线程池。

### 为什么不使用这个库？
- 您需要一个开箱即用且性能极高的线程池，具有无锁队列和最小开销；请考虑专门的库。
- 您使用的是较旧的 C++ 标准（C++23 之前），不支持所需特性。

## 目录
- [快速集成](#快速集成)
  - [先决条件](#先决条件)
  - [集成方法](#集成方法)
  - [卸载](#卸载)
- [快速示例](#快速示例)
- [测试](#测试)
- [文档](#文档)
  - [Doxygen](#doxygen)
  - [API 参考](#api-参考)
- [注意事项](#注意事项)
- [贡献](#贡献)

## 快速集成

### 先决条件

- C++23 兼容编译器（GCC 13+、Clang 16+、MSVC 2022 17.8+）。
- CMake 3.28+（用于构建）。
- 支持 C++20 模块的 CMake 生成器，例如 **Ninja**（Visual Studio 和 Unix Makefiles 在某些版本中可能存在模块依赖性问题）。

### 集成方法

#### 1. 将仓库添加为子模块：
```bash
git submodule add https://github.com/asdhij/thread-pool.git external/thread-pool
```
在您的 `CMakeLists.txt` 中：
```cmake
add_subdirectory(external/thread-pool)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 2. 安装并使用 `find_package`
- 将库安装到您的系统或打包前缀（在项目中提供 CMake 安装步骤）。
```bash
git clone https://github.com/asdhij/thread-pool.git
cd thread-pool
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr/local -G Ninja
cmake --install build  # 注意：对于系统目录可能需要 `sudo`
```
- 在您的 `CMakeLists.txt` 中：
```cmake
find_package(thread-pool REQUIRED)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 3. 使用 `FetchContent`
在您的 `CMakeLists.txt` 中：
```cmake
include(FetchContent)
FetchContent_Declare(
  thread-pool
  GIT_REPOSITORY https://github.com/asdhij/thread-pool.git
  GIT_TAG        main  # 这将始终从 `main` 分支拉取最新代码。您也可以使用特定的发布版本或标签
)
FetchContent_MakeAvailable(thread-pool)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```

#### 4. CPM
在您的 `CMakeLists.txt` 中：
```cmake
include(CPM.cmake)  # 确保包含了 CPM.cmake
CPMAddPackage(
  NAME thread-pool
  GITHUB_REPOSITORY asdhij/thread-pool
  GIT_TAG           main  # 使用特定的标签或提交以保证稳定性
)
target_link_libraries(your-target PRIVATE thread-pool::thread-pool)
```
有关如何将 CPM 添加到项目的更多信息，请参阅 [CPM 文档](https://github.com/cpm-cmake/CPM.cmake#adding-cpm)。

### 卸载
要卸载该库，您可以手动删除已安装的文件，或使用以下脚本：
```bash
cmake --build build --target uninstall
```

## 快速示例
```cpp
import thread_pool;
#include <cstdio>

int main() {
  // 使用默认值：DefaultTask, DefaultQueue<DefaultTask>, DefaultPolicy, 默认分配器。
  thread_pool::ThreadPool<> pool;
  pool.set_thread_count(4); // 设置工作线程数量

  pool.submit([] noexcept {
    // 一些工作。可调用对象必须是 `noexcept` 并返回 `void` 以满足 task 概念。
    std::printf("Hello from worker\n");
  });

  pool.stop().wait_for_tasks_completion(); // 请求停止并等待队列中的任务处理完毕
  return 0;
}
```

## 测试
项目包含使用 **GTest** 的测试套件。要构建并运行测试：
```bash
cmake -B build -S . -G Ninja -DTHREAD_POOL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```
> 请确保已安装 [GTest](https://github.com/google/googletest)。

## 文档
- [ThreadPool 类 (API)](thread_pool.md)
- [AffinityThreadPool (API)](affinity_thread_pool.md)
- [Task 概念 & DefaultTask](task.md)
- [TaskQueue 概念 & DefaultQueue](task_queue.md)
- [Policy 概念 & DefaultPolicy](policy.md)

### Doxygen
项目包含 API 的 Doxygen 文档。如果已安装 Doxygen，您可以使用以下命令构建文档：
```bash
cmake --build build --target thread-pool-docs
```
生成的文档将位于 `build/docs/html/index.html`。在您的网络浏览器中打开此文件以查看 API 参考。
> 如果未找到 Doxygen，将跳过文档目标。

### API 参考
- [ThreadPool](thread_pool.md) (模板)
  - [构造函数](thread_pool.md#构造函数)
  - [主要方法](thread_pool.md#主要方法)
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
  - [析构函数](thread_pool.md#析构函数)

- [AffinityThreadPool](affinity_thread_pool.md)
  - [构造函数](affinity_thread_pool.md#构造函数)
  - [主要 API](affinity_thread_pool.md#主要-api)
    - `start`
    - `get_thread_reference`
    - `join_all_threads`
    - `max_thread_count`
    - `thread_count`
  - [析构函数](affinity_thread_pool.md#析构函数)

## 注意事项
- 该库依赖于 `noexcept` 保证和原子 `wait`/`notify`（C++20/C++23 原语）。
- `DefaultQueue` 使用 `std::pmr::unsynchronized_pool_resource` 以提高内存效率 — 如果需要，您可以换入无锁队列。
- `Policy` 钩子是可选的；线程池通过概念检查它们的存在，并仅在可用时调用它们。

## 贡献
欢迎贡献、问题报告和功能请求！请注意：

### 对于代码修改：
- ✅ 保持线程安全
- ✅ 遵循现有的代码风格和格式
- ✅ 为你的修改添加测试
- ✅ 确保所有测试通过

### 流程：
1. 对于主要功能，请先创建 issue 进行讨论
2. 根据需要更新文档

### 有问题？
欢迎随时创建 issue 咨询任何问题！