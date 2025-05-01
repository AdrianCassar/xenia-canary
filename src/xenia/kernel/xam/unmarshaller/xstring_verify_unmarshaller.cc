/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xstring_verify_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XStringVerifyUnmarshaller::XStringVerifyUnmarshaller(
    uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      title_id_(0),
      flags_(0),
      locale_size_(0),
      num_strings_(0),
      locale_(""),
      strings_to_verify_({}) {}

X_HRESULT XStringVerifyUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->results_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->results_size) {
    return X_E_INVALIDARG;
  }

  title_id_ = Read<uint32_t>();
  flags_ = Read<uint32_t>();
  locale_size_ = Read<uint16_t>();
  num_strings_ = Read<uint16_t>();
  locale_ = ReadString(locale_size_);

  if (NumStrings() > X_ONLINE_MAX_XSTRING_VERIFY_STRING_DATA) {
    return X_E_INVALIDARG;
  }

  for (uint32_t i = 0; i < num_strings_; i++) {
    uint16_t string_size = Read<uint16_t>();

    // Unicode is represented as UTF-8 array
    std::string input = ReadString(string_size);

    strings_to_verify_.push_back(input);
  }

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  if (LocaleSize() > X_ONLINE_MAX_XSTRING_VERIFY_LOCALE) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
