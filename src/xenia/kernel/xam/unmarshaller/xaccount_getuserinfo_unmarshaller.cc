/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xaccount_getuserinfo_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XAccountGetUserInfoUnmarshaller::XAccountGetUserInfoUnmarshaller(
    uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      xuid_(0),
      machine_id_(0),
      title_id_(0) {}

X_HRESULT XAccountGetUserInfoUnmarshaller::Deserialize() {
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

  if (GetAsyncTask()->GetXLiveAsyncTask()->results_size <
      XAccountGetUserInfoResponseSize()) {
    return X_ONLINE_E_ACCOUNTS_USER_GET_ACCOUNT_INFO_ERROR;
  }

  xuid_ = Read<uint64_t>();
  machine_id_ = Read<uint64_t>();
  title_id_ = Read<uint32_t>();

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
