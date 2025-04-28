/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_APPS_XLIVEBASE_APP_H_
#define XENIA_KERNEL_XAM_APPS_XLIVEBASE_APP_H_

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/app_manager.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

class XLiveBaseApp : public App {
 public:
  explicit XLiveBaseApp(KernelState* kernel_state);

  X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) override;

 private:
  X_HRESULT XPresenceInitialize(uint32_t buffer_length);
  X_HRESULT XPresenceSubscribe(uint32_t buffer_length);
  X_HRESULT XPresenceUnsubscribe(uint32_t buffer_length);
  X_HRESULT XPresenceCreateEnumerator(uint32_t buffer_length);
  X_HRESULT XOnlineQuerySearch(uint32_t buffer_ptr);
  X_HRESULT XOnlineGetServiceInfo(uint32_t service_id, uint32_t service_info);

  X_HRESULT XFriendsCreateEnumerator(uint32_t buffer_ptr);
  void UpdateFriendPresence(const uint32_t user_index);
  void UpdatePresenceXUIDs(const std::vector<uint64_t>& xuids,
                           const uint32_t user_index);
  X_HRESULT GenericMarshalled(uint32_t buffer_ptr);
  X_HRESULT XInviteGetAcceptedInfo(uint32_t buffer_length);
  X_HRESULT XUserMuteListSetState(uint32_t buffer_ptr);
  X_HRESULT XAccountGetUserInfo(uint32_t buffer_ptr);
  X_HRESULT XStorageEnumerate(uint32_t buffer_ptr);
  X_HRESULT XStringVerify(uint32_t buffer_ptr);
  X_HRESULT XStorageDelete(uint32_t buffer_ptr);
  X_HRESULT XStorageDownloadToMemory(uint32_t buffer_ptr);
  X_HRESULT XStorageUploadFromMemory(uint32_t buffer_ptr);
  X_HRESULT XStorageBuildServerPath(uint32_t buffer_ptr);
  X_HRESULT XOnlineGetTaskProgress(uint32_t buffer_ptr);
  X_HRESULT XUserFindUsersUnkn58017(uint32_t buffer_ptr);
  X_HRESULT XUserFindUsers(uint32_t buffer_ptr);
  X_HRESULT XContentGetMarketplaceCounts(uint32_t buffer_ptr);

  std::string ConvertServerPathToXStorageSymlink(
      std::string server_path_string);
  std::string ConvertXStorageSymlinkToServerPath(
      std::string symlink_path_string);
  X_STORAGE_FACILITY GetStorageFacilityTypeFromServerPath(
      std::string server_path);

  X_HRESULT Unkn5008C(uint32_t buffer_ptr);
  X_HRESULT XAccountGetPointsBalance(uint32_t buffer_ptr);
  X_HRESULT Unkn5008B(uint32_t buffer_ptr);
  X_HRESULT Unkn5008F(uint32_t buffer_ptr);
  X_HRESULT Unkn50090(uint32_t buffer_ptr);
  X_HRESULT Unkn50091(uint32_t buffer_ptr);
  X_HRESULT Unkn50097(uint32_t buffer_ptr);

  X_HRESULT Unkn5800A(uint32_t buffer_ptr);

  X_HRESULT Unkn58024(uint32_t buffer_length);
  X_HRESULT Unkn5801C(uint32_t buffer_length);

  static constexpr std::string_view xstorage_symboliclink = "XSTORAGE:";
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_APPS_XLIVEBASE_APP_H_
