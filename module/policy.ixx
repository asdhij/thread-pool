/**
 * @file policy.ixx
 * @brief Defines concepts and a default implementation for thread pool policies
 *
 * @copyright Copyright (c) 2025 asdhij (169761929+asdhij@users.noreply.github.com)
 * SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-10-31
 */

module;
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
export module thread_pool.policy;

namespace thread_pool {

namespace Policy {

/**
 * @brief Concept to check if Policy has on_thread_start method
 * @tparam Policy The policy type to check
 * @note This concept checks for the existence of a method named on_thread_start
 *       that takes no parameters and returns void. It also ensures that the method
 *       is marked as noexcept.
 * @note It's optional.
 *       If the method exists, it will be called at the start of each thread in the pool.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy>
concept has_on_thread_start = requires(Policy policy) {
  { policy.on_thread_start() } noexcept -> std::same_as<void>;
};

/**
 * @brief Concept to check if Policy has on_thread_exit method
 * @tparam Policy The policy type to check
 * @note This concept checks for the existence of a method named on_thread_exit
 *       that takes no parameters and returns void. It also ensures that the method
 *       is marked as noexcept.
 * @note It's optional.
 *       If the method exists, it will be called at the exit of each thread in the pool.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy>
concept has_on_thread_exit = requires(Policy policy) {
  { policy.on_thread_exit() } noexcept -> std::same_as<void>;
};

/**
 * @brief Concept to check if Policy has on_pool_destroy method
 * @tparam Policy The policy type to check
 * @note This concept checks for the existence of a method named on_pool_destroy
 *       that takes no parameters and returns void. It also ensures that the method
 *       is marked as noexcept.
 * @note It's optional.
 *       If the method exists, it will be called at the destruction of the thread pool.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy>
concept has_on_pool_destroy = requires(Policy policy) {
  { policy.on_pool_destroy() } noexcept -> std::same_as<void>;
};

/**
 * @brief Concept to check if Policy has on_pool_stop method
 * @tparam Policy The policy type to check
 * @note This concept checks for the existence of a method named on_pool_stop
 *       that takes no parameters and returns void.
 * @note It's optional.
 *       If the method exists, it will be called when the thread pool will be stopped.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy>
concept has_on_pool_stop = requires(Policy policy) {
  { policy.on_pool_stop() } -> std::same_as<void>;
};

/**
 * @brief Concept to check if Policy has on_pool_stop method that is noexcept
 * @tparam Policy The policy type to check
 * @note Same as has_on_pool_stop but checks if the method is noexcept.
 */
export template <typename Policy>
concept has_on_pool_stop_nothrow = has_on_pool_stop<Policy> && requires(Policy policy) {
  { policy.on_pool_stop() } noexcept;
};

/**
 * @brief Concept to check if Policy has on_pool_shutdown method
 * @tparam Policy The policy type to check
 * @note This concept checks for the existence of a method named on_pool_shutdown
 *       that takes no parameters and returns void.
 * @note It's optional.
 *       If the method exists, it will be called when the thread pool will be shutdown.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy>
concept has_on_pool_shutdown = requires(Policy policy) {
  { policy.on_pool_shutdown() } -> std::same_as<void>;
};

/**
 * @brief Concept to check if Policy has on_pool_shutdown method that is noexcept
 * @tparam Policy The policy type to check
 * @note Same as has_on_pool_shutdown but checks if the method is noexcept.
 */
export template <typename Policy>
concept has_on_pool_shutdown_nothrow = has_on_pool_shutdown<Policy> && requires(Policy policy) {
  { policy.on_pool_shutdown() } noexcept;
};

/**
 * @brief Concept to check if Policy has on_task_enqueue_failed method
 * @tparam Policy The policy type to check
 * @tparam Task The task type(s) to check
 * @note This concept checks for the existence of a method named on_task_enqueue_failed
 *       that takes Task parameter(s) and returns void. It also ensures that the method
 *       is marked as noexcept.
 * @note It's optional.
 *       If the method exists, it will be called when a task fails to be enqueued.
 *       If the Policy does not have this method, the thread pool will simply not call it.
 */
export template <typename Policy, typename... Task>
concept has_on_task_enqueue_failed = requires(Policy policy, Task&&... task) {
  { policy.on_task_enqueue_failed(std::forward<Task>(task)...) } noexcept -> std::same_as<void>;
};

}  // namespace Policy

/**
 * @brief Default policy implementation
 * @details This policy provides default implementations for all optional hooks.
 *          The on_task_enqueue_failed method simply invokes the provided tasks.
 */
export class DefaultPolicy {
 public:
  constexpr DefaultPolicy() noexcept = default;

  /**
   * @brief Called when a task fails to be enqueued. It invokes the provided tasks immediately.
   * @tparam Task The type(s) of the task(s) that failed to be enqueued.
   */
  template <typename... Task> requires (std::is_nothrow_invocable_r_v<void, Task> && ...)
  static void on_task_enqueue_failed(Task&&... task) noexcept { (std::invoke(std::forward<Task>(task)), ...); }

  constexpr ~DefaultPolicy() noexcept = default;
};

}  // namespace thread_pool