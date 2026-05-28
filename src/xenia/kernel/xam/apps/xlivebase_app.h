/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
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

  uint32_t GetDispatchMessageID(uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) const;

  X_HRESULT ExecuteDispatchMessage(uint32_t message, uint32_t buffer_ptr,
                                   uint32_t buffer_length,
                                   uint32_t* extended_error) override;

 private:
  X_HRESULT XPresenceInitialize(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT XPresenceSubscribe(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT XPresenceUnsubscribe(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT XPresenceCreateEnumerator(uint32_t buffer_ptr,
                                      uint32_t buffer_length);
  X_HRESULT XOnlineQuerySearch(uint32_t buffer_ptr);
  X_HRESULT XOnlineGetServiceInfo(uint32_t service_id, uint32_t service_info);

  X_HRESULT XFriendsCreateEnumerator(uint32_t buffer_ptr,
                                     uint32_t buffer_length);
  X_HRESULT XInviteSend(uint32_t buffer_ptr);
  X_HRESULT XInviteGetAcceptedInfo(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT GenericMarshalled(uint32_t buffer_ptr);
  X_HRESULT XUserMuteListQuery(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT XUserValidateAvatarManifest(uint32_t buffer_ptr);
  X_HRESULT XUserMuteListAdd(uint32_t buffer_ptr);
  X_HRESULT XUserMuteListRemove(uint32_t buffer_ptr);
  X_HRESULT XAccountGetUserInfo(uint32_t buffer_ptr);
  X_HRESULT XStorageEnumerate(uint32_t buffer_ptr);
  X_HRESULT XStringVerify(uint32_t buffer_ptr);
  X_HRESULT XUserEstimateRankForRating(uint32_t buffer_ptr);
  X_HRESULT XStorageDelete(uint32_t buffer_ptr);
  X_HRESULT XStorageDownloadToMemory(uint32_t buffer_ptr,
                                     uint32_t* extended_error);
  X_HRESULT XStorageUploadFromMemory(uint32_t buffer_ptr);
  X_HRESULT XStorageBuildServerPath(uint32_t buffer_ptr);
  X_HRESULT XOnlineGetTaskProgress(uint32_t buffer_ptr);
  X_HRESULT GetNextSequenceMessage(uint32_t buffer_ptr);
  X_HRESULT XUserFindUsers(uint32_t buffer_ptr);
  X_HRESULT XContentGetMarketplaceCounts(uint32_t buffer_ptr);

  std::string ConvertServerPathToXStorageSymlink(
      std::string server_path_string);
  std::string ConvertXStorageSymlinkToServerPath(
      std::string symlink_path_string);
  X_STORAGE_FACILITY GetStorageFacilityTypeFromServerPath(
      std::string server_path);

  X_HRESULT XGetBannerList(uint32_t buffer_ptr);
  X_HRESULT XAccountGetPointsBalance(uint32_t buffer_ptr);
  X_HRESULT XGetBannerListHot(uint32_t buffer_ptr);
  X_HRESULT XOfferingContentEnumerate(uint32_t buffer_ptr);
  X_HRESULT XGenresEnumerate(uint32_t buffer_ptr);
  X_HRESULT XEnumerateTitlesByFilter(uint32_t buffer_ptr);
  X_HRESULT XOfferingSubscriptionEnumerate(uint32_t buffer_ptr);

  X_HRESULT XUpdateAccessTimes(uint32_t buffer_ptr);

  X_HRESULT XMessageEnumerate(uint32_t buffer_ptr, uint32_t buffer_length);
  X_HRESULT XPresenceGetState(uint32_t buffer_ptr, uint32_t buffer_length);

  const uint32_t kAsyncSchemaIndexMask = 0x50000;

  static constexpr std::string_view xstorage_symboliclink = "XSTORAGE:";
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_APPS_XLIVEBASE_APP_H_
