/**
 * @file task.ixx
 * @brief Task module for the thread pool.
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-10-18
 */

module;
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
export module thread_pool.task;

namespace thread_pool {

/**
 * @brief Concept to check if a type is a valid task
 * @tparam Task The task type to check
 * @note A valid task must be invocable with no parameters, return void,
 *       and be nothrow invocable, nothrow default constructible, and nothrow destructible.
 */
export template <typename Task>
concept task = std::is_nothrow_invocable_r_v<void, Task> && std::is_nothrow_default_constructible_v<Task> && std::is_nothrow_destructible_v<Task>;

// Default task implementation using type erasure
export class DefaultTask {
 private:
  struct Storage {
    virtual ~Storage() noexcept = default;
    virtual void call() noexcept = 0;
  };

  template <typename F> requires (std::is_nothrow_invocable_r_v<void, std::decay_t<F>> && std::constructible_from<std::decay_t<F>, F> && std::is_nothrow_destructible_v<std::decay_t<F>>)
  struct DerivedStorage : Storage {
    std::decay_t<F> f_;
    constexpr DerivedStorage(F&& f) noexcept(std::is_nothrow_constructible_v<std::decay_t<F>, F>) : f_{std::decay_t<F>(std::forward<F>(f))} {}
    ~DerivedStorage() noexcept override = default;
    void call() noexcept override { std::invoke(f_); }
  };

 public:
  /**
   * @brief Constructs a DefaultTask from a callable object.
   * @tparam F The type of the callable object.
   * @param f The callable object to be stored in the task.
   * @exception std::bad_alloc If memory allocation fails.
   * @exception ... Any exception thrown by the callable object's constructor.
   * @note The callable object must be nothrow invocable and nothrow destructible.
   */
  template <typename F>
  constexpr explicit DefaultTask(F&& f) : func_{std::make_unique<DerivedStorage<F>>(std::forward<F>(f))} {}

  constexpr DefaultTask() noexcept = default;
  DefaultTask(const DefaultTask&) = delete;
  DefaultTask(DefaultTask&&) noexcept = default;
  DefaultTask& operator=(const DefaultTask&) = delete;
  DefaultTask& operator=(DefaultTask&&) noexcept = default;

  constexpr void operator()() noexcept {
    if (func_) [[likely]] { func_->call(); }
  }

  constexpr explicit operator bool() const noexcept { return static_cast<bool>(func_); }

  constexpr ~DefaultTask() noexcept = default;

 private:
  std::unique_ptr<Storage> func_;
};

}  // namespace thread_pool