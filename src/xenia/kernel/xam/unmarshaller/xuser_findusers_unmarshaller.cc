/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xuser_findusers_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XUserFindUsersUnmarshaller::XUserFindUsersUnmarshaller(
    uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      msg_header_({}),
      xuid_issuer_(0),
      num_users_(0) {}

X_HRESULT XUserFindUsersUnmarshaller::Deserialize() {
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

  msg_header_ = Read<BASE_MSG_HEADER>();
  xuid_issuer_ = Read<uint64_t>();
  num_users_ = Read<uint32_t>();

  for (uint32_t i = 0; i < num_users_; i++) {
    users_.push_back(Read<FIND_USER_INFO>());
  }

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
