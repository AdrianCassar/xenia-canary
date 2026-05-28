/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/unmarshaller/schema_in_memory.h"

namespace xe {
namespace kernel {
namespace xam {

class XLivebaseAsyncTask {
 public:
  XLivebaseAsyncTask(uint32_t async_task_address);

  void PrintTaskInfo() const;

  std::string GetTaskUrl() const;

  XLIVE_ASYNC_TASK* GetXLiveAsyncTask() const;

  SchemaInMemory& GetTitleSchema();

  template <typename T>
  T* DeserializeReinterpret() {
    if (!xlive_async_task_ptr_ ||
        !xlive_async_task_ptr_->marshalled_request_ptr) {
      return nullptr;
    }

    assert_false(sizeof(T) != xlive_async_task_ptr_->marshalled_request_size);

    return kernel_state()->memory()->TranslateVirtual<T*>(
        xlive_async_task_ptr_->marshalled_request_ptr);
  };

  template <typename T>
  T* Results() const {
    if (!xlive_async_task_ptr_ || !xlive_async_task_ptr_->results_ptr) {
      return nullptr;
    }

    return kernel_state()->memory()->TranslateVirtual<T*>(
        xlive_async_task_ptr_->results_ptr);
  };

  bool ZeroResults() const {
    if (!xlive_async_task_ptr_ || !xlive_async_task_ptr_->results_ptr) {
      return false;
    }

    uint8_t* results_ptr = kernel_state()->memory()->TranslateVirtual<uint8_t*>(
        xlive_async_task_ptr_->results_ptr);

    std::fill_n(results_ptr, xlive_async_task_ptr_->results_size, 0);

    return true;
  };

  XLIVE_ASYNC_TASK* xlive_async_task_ptr_ = nullptr;
  SchemaInMemory schema;
  std::string url_ = "";
  std::span<uint8_t> data_ptr_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_
