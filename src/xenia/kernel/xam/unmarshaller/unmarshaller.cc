/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

Unmarshaller::Unmarshaller(uint32_t marshaller_address)
    : xlivebase_async_message_ptr_(nullptr),
      async_task_(nullptr),
      position_(0) {
  if (!marshaller_address) {
    return;
  }

  xlivebase_async_message_ptr_ =
      kernel_state()->memory()->TranslateVirtual<XLIVEBASE_ASYNC_MESSAGE*>(
          marshaller_address);

  async_task_ = new XLivebaseAsyncTask(
      xlivebase_async_message_ptr_->xlive_async_task_ptr);
}

std::span<uint8_t> Unmarshaller::Advance(size_t count) {
  const size_t offset = position_ + count;

  if (offset > async_task_->data_ptr_.size()) {
    assert_always(std::format("{}: Out of Bounds Span!", __func__));

    return std::span<uint8_t>();
  }

  std::span<uint8_t> data = async_task_->data_ptr_.subspan(position_, count);

  position_ = offset;

  return data;
}

std::u16string Unmarshaller::ReadSwapUTF16String(uint32_t length) {
  // Excludes null terminator
  std::u16string server_path = xe::load_and_swap<std::u16string>(
      reinterpret_cast<char16_t*>(async_task_->data_ptr_.data() + position_));

  std::span<uint8_t> string_data =
      Advance(xe::string_util::size_in_bytes(server_path, true));

  assert_false(length != server_path.length() + 1);

  if (string_data.empty()) {
    return u"";
  }

  return server_path;
}

std::string Unmarshaller::ReadString(uint32_t length) {
  std::span<uint8_t> string_data = Advance(length);

  if (string_data.empty()) {
    return "";
  }

  return std::string(reinterpret_cast<char*>(string_data.data()), length);
}

X_HRESULT Unmarshaller::Deserialize() { return X_E_FAIL; }

XLIVEBASE_ASYNC_MESSAGE* Unmarshaller::GetXLiveBaseAsyncMessage() {
  return xlivebase_async_message_ptr_;
}

XLivebaseAsyncTask* Unmarshaller::GetAsyncTask() { return async_task_; }

size_t Unmarshaller::GetPosition() const { return position_; }

}  // namespace xam
}  // namespace kernel
}  // namespace xe
