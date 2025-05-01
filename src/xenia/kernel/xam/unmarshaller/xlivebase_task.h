/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_

#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace xam {

class XLivebaseAsyncTask {
 public:
  XLivebaseAsyncTask(uint32_t async_task_address);

  ~XLivebaseAsyncTask() {};

  void PrintTaskInfo();

  bool GetSchemaEntry(uint16_t schema_index,
                      SCHEMA_TABLE_ENTRY* schema_entry_ptr);

  bool GetSchemaDataFromEntry(SCHEMA_TABLE_ENTRY* schema_entry_ptr,
                              bool request, std::span<uint8_t>* schema_data);

  bool EndianBufferBind(BASE_ENDIAN_BUFFER* base, std::span<uint8_t>& buffer);

  bool XLookupSchemaIndexFromOrdinal(uint16_t ordinal,
                                     uint16_t* schema_index) const;

  bool LookupUrlFromTable(uint16_t url_index, std::string_view* url_ptr);

  bool LookupConstantFromTable(uint16_t constant_index, uint32_t* value_ptr);

  std::string_view GetTaskUrl();

  void PrettyPrintSchemaTables() const;

  void PrettyPrintUrls();

  void PrettyPrintUrlsWithSchemaIndex();

  void PrettyPrintOrdinalToIndex() const;

  XLIVE_ASYNC_TASK* GetXLiveAsyncTask();

  SCHEMA_DATA* GetSchemaData();

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

    std::fill_n(results_ptr, xlive_async_task_ptr_->results_size.get(), 0);

    return true;
  };

  XLIVE_ASYNC_TASK* xlive_async_task_ptr_;
  SCHEMA_DATA* schema_data_ptr_;
  ORDINAL_TO_INDEX* ordinal_to_index_ptr_;
  SCHEMA_TABLE_ENTRY* schema_table_entry_ptr_;
  xe::be<uint16_t>* url_offsets_ptr;
  char* url_data_ptr;
  xe::be<uint32_t>* constant_list_ptr;
  std::string_view url_ = "";
  std::span<uint8_t> data_ptr_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XLIVEBASETASK_H_