/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace xam {

Unmarshaller::Unmarshaller(KernelState* kernel_state,
                           uint32_t marshaller_address)
    : kernel_state_(kernel_state),
      memory_(kernel_state->memory()),
      async_task_(kernel_state_, GetAsyncTaskAddress(marshaller_address)) {
  if (!marshaller_address) {
    return;
  }

  xlivebase_async_message_ptr_ =
      memory_->TranslateVirtual<XLIVEBASE_ASYNC_MESSAGE*>(marshaller_address);
}

uint32_t Unmarshaller::GetAsyncTaskAddress(uint32_t marshaller_address) const {
  if (!marshaller_address) {
    return 0;
  }

  XLIVEBASE_ASYNC_MESSAGE* message =
      memory_->TranslateVirtual<XLIVEBASE_ASYNC_MESSAGE*>(marshaller_address);

  return message ? message->xlive_async_task_ptr.get() : 0;
}

std::span<uint8_t> Unmarshaller::Advance(size_t count) {
  if (position_ > async_task_.data_ptr_.size() ||
      count > async_task_.data_ptr_.size() - position_) {
    assert_always(std::format("{}: Out of Bounds Span!", __func__));

    return std::span<uint8_t>();
  }

  const size_t offset = position_ + count;

  std::span<uint8_t> data = async_task_.data_ptr_.subspan(position_, count);

  position_ = offset;

  return data;
}

std::u16string Unmarshaller::ReadSwapUTF16String(uint32_t length) {
  if (length > async_task_.data_ptr_.size() / sizeof(char16_t) ||
      position_ > async_task_.data_ptr_.size() ||
      length > (async_task_.data_ptr_.size() - position_) / sizeof(char16_t)) {
    assert_always(std::format("{}: Out of Bounds UTF-16 String!", __func__));
    return u"";
  }

  std::span<const uint8_t> string_data = Advance(length * sizeof(char16_t));

  if (string_data.empty()) {
    return u"";
  }

  return xe::string_util::read_u16string_and_swap(
      reinterpret_cast<const char16_t*>(string_data.data()));
}

std::string Unmarshaller::ReadString(uint32_t length) {
  std::span<uint8_t> string_data = Advance(length);

  if (string_data.empty()) {
    return "";
  }

  return std::string(reinterpret_cast<char*>(string_data.data()), length);
}

bool Unmarshaller::ZeroResults() const { return async_task_.ZeroResults(); }

XLIVEBASE_ASYNC_MESSAGE* Unmarshaller::GetXLiveBaseAsyncMessage() {
  return xlivebase_async_message_ptr_;
}

XLivebaseAsyncTask Unmarshaller::GetAsyncTask() const { return async_task_; }

size_t Unmarshaller::GetPosition() const { return position_; }

bool Unmarshaller::IsValid() const {
  XLIVE_ASYNC_TASK* task = async_task_.GetXLiveAsyncTask();

  return xlivebase_async_message_ptr_ && task && task->marshalled_request_ptr &&
         async_task_.data_ptr_.size() == task->marshalled_request_size;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
