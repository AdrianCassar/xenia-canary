/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/xlivebase_task.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

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

  virtual X_HRESULT Deserialize() = 0;

  template <typename T>
  T* DeserializeReinterpret() {
    return async_task_.DeserializeReinterpret<T>();
  };

  template <typename T>
  T* Results() const {
    return async_task_.Results<T>();
  };

  bool ZeroResults() const;

  XLIVEBASE_ASYNC_MESSAGE* GetXLiveBaseAsyncMessage();

  XLivebaseAsyncTask GetAsyncTask() const;

  size_t GetPosition() const;

 protected:
  Unmarshaller(KernelState* kernel_state, uint32_t marshaller_buffer);
  ~Unmarshaller() = default;

  KernelState* kernel_state_ = nullptr;
  Memory* memory_ = nullptr;

  XLIVEBASE_ASYNC_MESSAGE* xlivebase_async_message_ptr_ = nullptr;
  XLivebaseAsyncTask async_task_;

 private:
  uint32_t GetAsyncTaskAddress(uint32_t marshaller_address) const;

  size_t position_ = 0;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_UNMARSHALLER_H_
