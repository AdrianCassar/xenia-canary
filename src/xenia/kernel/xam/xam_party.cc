/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamPartyGetUserList_entry(
    dword_t caller, pointer_t<X_PARTY_USER_LIST> party_list_ptr) {
  if (!party_list_ptr) {
    return X_PARTY_E_NOT_IN_PARTY;
  }

  if (party_list_ptr) {
    party_list_ptr.Zero();
  }

  // 513107D9 wants user_count >= 2
  party_list_ptr->user_count = 2;

  {
    auto profile = kernel_state()->xam_state()->GetUserProfile((uint32_t)0);

    if (profile) {
      party_list_ptr->users[0].xuid = profile->xuid();

      memcpy(party_list_ptr->users[0].gamertag, profile->name().c_str(),
             sizeof(party_list_ptr->users[0].gamertag));

      party_list_ptr->users[0].user_index = 0;
      party_list_ptr->users[0].nat_type = 1;  // Toggles join sessions
      party_list_ptr->users[0].title_id = kernel_state()->title_id();
      party_list_ptr->users[0].flags =
          X_PARTY_USER_ISLOCAL | X_PARTY_USER_ISINGAMESESSION;
    }
  }

  for (uint32_t i = 1; i <= (party_list_ptr->user_count - 1); i++) {
    party_list_ptr->users[i].xuid = i;

    memcpy(party_list_ptr->users[i].gamertag,
           fmt::format("Party Member {}", i).c_str(),
           sizeof(party_list_ptr->users[i].gamertag));

    party_list_ptr->users[i].user_index = XUserIndexNone;
    party_list_ptr->users[i].nat_type = 1;  // Toggles join sessions
    party_list_ptr->users[i].title_id = kernel_state()->title_id();
    party_list_ptr->users[i].flags =
        X_PARTY_USER_ISLOCAL | X_PARTY_USER_ISINGAMESESSION;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPartyGetUserList, kNone, kStub);

// Show UI to invite party members to a multiplayer session?
dword_result_t XamPartySendGameInvites_entry(
    dword_t caller, dword_t user_index, dword_t unknown,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = X_ERROR_SUCCESS;
    length = 0;

    return X_ERROR_SUCCESS;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = run(extended_error, length);

    return result;
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamPartySendGameInvites, kNone, kStub);

dword_result_t XamPartySetCustomData_entry(
    dword_t caller, dword_t user_index,
    pointer_t<X_PARTY_CUSTOM_DATA> custom_data_ptr) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPartySetCustomData, kNone, kStub);

dword_result_t XamPartyGetBandwidth_entry(dword_t bandwidth_type,
                                          lpqword_t bandwidth_statstic) {
  *bandwidth_statstic = 0;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPartyGetBandwidth, kNone, kStub);

dword_result_t XamPartyGetUserListInternal_entry(
    pointer_t<X_PARTY_USER_LIST_INTERNAL> party_list_ptr) {
  if (party_list_ptr) {
    party_list_ptr.Zero();
  }

  return X_PARTY_E_NOT_IN_PARTY;
}
DECLARE_XAM_EXPORT1(XamPartyGetUserListInternal, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Party);
