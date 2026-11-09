/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_ASYNC_H_
#define XENIA_BASE_ASYNC_H_

#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace xe {

// Unlike std::async the returned future doesn't block when it's destroyed or
// replaced, so a slow or unreachable API can't stall the UI thread.
// func must not capture anything that may be destroyed before it finishes.
template <typename F>
std::future<std::invoke_result_t<std::decay_t<F>>> RunDetachedAsync(F&& func) {
  using Result = std::invoke_result_t<std::decay_t<F>>;

  auto promise = std::make_shared<std::promise<Result>>();
  auto future = promise->get_future();

  std::thread([promise, func = std::forward<F>(func)]() mutable {
    try {
      if constexpr (std::is_void_v<Result>) {
        func();
        promise->set_value();
      } else {
        promise->set_value(func());
      }
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  }).detach();

  return future;
}

}  // namespace xe

#endif  // XENIA_BASE_ASYNC_H_
