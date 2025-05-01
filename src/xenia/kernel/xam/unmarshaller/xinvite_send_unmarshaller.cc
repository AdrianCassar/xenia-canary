/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xinvite_send_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XInviteSendUnmarshaller::XInviteSendUnmarshaller(uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      user_index_(0),
      num_invitees_(0),
      invitees_({}),
      display_string_size_(0),
      display_string_(u""),
      xmsg_handle_(0) {}

X_HRESULT XInviteSendUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  if (GetAsyncTask()->GetXLiveAsyncTask()->results_ptr ||
      GetAsyncTask()->GetXLiveAsyncTask()->results_size) {
    assert_always(std::format("{} results unexpected!", __func__));
  }

  user_index_ = ReadSwap<uint32_t>();
  num_invitees_ = ReadSwap<uint32_t>();

  if (num_invitees_ > X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  for (uint32_t i = 0; i < num_invitees_; i++) {
    invitees_.push_back(ReadSwap<uint64_t>());
  }

  display_string_size_ = ReadSwap<uint32_t>();
  display_string_ = ReadSwapUTF16String(display_string_size_);

  xmsg_handle_ = ReadSwap<uint32_t>();

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  if (display_string_size_ > X_ONLINE_MAX_XINVITE_DISPLAY_STRING) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
