/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_

#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/unmarshaller/xlivebase_task.h"

namespace xe {
namespace kernel {
namespace xam {

class Unmarshaller {
 public:
  std::span<uint8_t> Advance(size_t count);

  template <typename T>
  std::span<uint8_t> AdvanceSizeOf() {
    return Advance(sizeof(T));
  };

  template <typename T>
  T Read() {
    std::span<uint8_t> data = AdvanceSizeOf<T>();

    if (data.empty()) {
      return {};
    }

    return *reinterpret_cast<T*>(data.data());
  };

  template <typename T>
  T ReadSwap() {
    return xe::byte_swap<T>(Read<T>());
  };

  std::u16string ReadSwapUTF16String(uint32_t length);

  std::string ReadString(uint32_t length);

  virtual X_HRESULT Deserialize();

  template <typename T>
  T* DeserializeReinterpret() {
    return async_task_->DeserializeReinterpret<T>();
  };

  template <typename T>
  T* Results() const {
    return async_task_->Results<T>();
  };

  bool ZeroResults() const { return async_task_->ZeroResults(); };

  XLIVEBASE_ASYNC_MESSAGE* GetXLiveBaseAsyncMessage();

  XLivebaseAsyncTask* GetAsyncTask();

  size_t GetPosition() const;

  ~Unmarshaller() {};

 protected:
  Unmarshaller(uint32_t marshaller_buffer);

  XLIVEBASE_ASYNC_MESSAGE* xlivebase_async_message_ptr_;
  XLivebaseAsyncTask* async_task_;

 private:
  size_t position_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_