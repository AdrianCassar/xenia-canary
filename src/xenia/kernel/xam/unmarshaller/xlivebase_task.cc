/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xlivebase_task.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace xam {

XLivebaseAsyncTask::XLivebaseAsyncTask(KernelState* kernel_state,
                                       uint32_t async_task_address)
    : kernel_state_(kernel_state),
      memory_(kernel_state->memory()),
      schema(kernel_state) {
  if (!async_task_address) {
    return;
  }

  xlive_async_task_ptr_ =
      memory_->TranslateVirtual<XLIVE_ASYNC_TASK*>(async_task_address);

  schema.BindToSchema(xlive_async_task_ptr_->schema_data_ptr);

  uint8_t* data_request_ptr = memory_->TranslateVirtual<uint8_t*>(
      xlive_async_task_ptr_->marshalled_request_ptr);

  data_ptr_ = std::span<uint8_t>(
      data_request_ptr, xlive_async_task_ptr_->marshalled_request_size);

  url_ = schema.GetTaskUrl(xlive_async_task_ptr_->schema_index);

  PrintTaskInfo();
}

void XLivebaseAsyncTask::PrintTaskInfo() const {
  XELOGD(
      "\n***************** XLiveBase Task Info *****************\n"
      "SchemaVersionMajor: {}\n"
      "SchemaVersionMinor: {}\n"
      "ToolVersion: {:08X}\n"
      "TaskFlags: {:08X}\n"
      "SchemaTableEntries: {}\n"
      "OrdinalToIndexPtr: {:08X}\n"
      "SchemaIndex: {:04X}\n"
      "MarshalledRequestPtr: {:08X}\n"
      "MarshalledRequestSize: {}\n"
      "ResultsPtr: {:08X}\n"
      "RequestsSize: {}\n"
      "URL: {}\n",
      schema.Header.SchemaVersionMajor.get(),
      schema.Header.SchemaVersionMinor.get(), schema.Header.ToolVersion.get(),
      xlive_async_task_ptr_->task_flags.get(),
      schema.Header.SchemaTableEntries.get(), schema.OrdinalToIndexPtr.get(),
      xlive_async_task_ptr_->schema_index.get(),
      xlive_async_task_ptr_->marshalled_request_ptr.get(),
      xlive_async_task_ptr_->marshalled_request_size.get(),
      xlive_async_task_ptr_->results_ptr.get(),
      xlive_async_task_ptr_->results_size.get(), url_);
}

std::string XLivebaseAsyncTask::GetTaskUrl() const { return url_; }

XLIVE_ASYNC_TASK* XLivebaseAsyncTask::GetXLiveAsyncTask() const {
  return xlive_async_task_ptr_;
}

SchemaInMemory& XLivebaseAsyncTask::GetTitleSchema() { return schema; }

}  // namespace xam
}  // namespace kernel
}  // namespace xe
