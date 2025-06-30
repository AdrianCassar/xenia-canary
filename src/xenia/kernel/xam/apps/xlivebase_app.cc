/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <span>

#include "xenia/kernel/xam/apps/xlivebase_app.h"

#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/unmarshaller/generic_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xaccount_getuserinfo_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xinvite_send_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xlivebase_task.h"
#include "xenia/kernel/xam/unmarshaller/xonline_query_search_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xstorage_delete_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xstorage_download_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xstorage_enumerate_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xstorage_upload_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xstring_verify_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xuser_estimate_rank_for_ratings_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/xuser_findusers_unmarshaller.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/ui/imgui_host_notification.h"

DEFINE_bool(stub_xlivebase, false,
            "Return success for all unimplemented XLiveBase calls.", "Live");

DECLARE_bool(xstorage_backend);

DECLARE_bool(xstorage_user_data_backend);

DECLARE_int32(user_country);

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

// TODO(Adrian): Determine the correct function during runtime, the schema index
// can change depending on schema data version.
XLiveBaseApp::XLiveBaseApp(KernelState* kernel_state)
    : App(kernel_state, 0xFC) {}

/// <param name="buffer_ptr"> - Generic param1 could be anything.</param>
/// <param name="buffer_length"> - Generic param2 could be anything.</param>
X_HRESULT XLiveBaseApp::DispatchMessageSync(uint32_t message,
                                            uint32_t buffer_ptr,
                                            uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  uint8_t* buffer = memory_->TranslateVirtual<uint8_t*>(buffer_ptr);

  switch (message) {
    case 0x00050002: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("XInviteSend({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XInviteSend(buffer_ptr);
    }
    case 0x00050008: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // Default to XStorageDownloadToMemory instead of XStorageDelete since
      // it's used more.

      // 534507D4, 555307D7, 545107D1 - XStorageDownloadToMemory
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);

      // XELOGD("XStorageDelete({:08X}, {:08X})", buffer_ptr, buffer_length);
      // return XStorageDelete(buffer_ptr);
    }
    case 0x00050009: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 555307D7 - XStorageEnumerate
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);
    }
    case 0x0005000A: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 534507D4 - XStorageUploadFromMemory
      // 4D5307D3, 415607F7, 584108F0, 5454082B, 545407F8, 575207FD
      XELOGD("XStorageEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XStorageEnumerate(buffer_ptr);
    }
    case 0x0005000B: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 43430821
      XELOGD("XStorageUploadFromMemory({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageUploadFromMemory(buffer_ptr);
    }
    case 0x0005000C: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 454107F1 - XAccountGetUserInfo
      // 4E4D07D3 - XStorageUploadFromMemory (-1 schema index offset)
      // 57520829, 4156081C - XStringVerify (+1 schema index offset)
      XELOGD("GenericTask({:08X} {:08X})", buffer_ptr, buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x0005000D: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // Usually XStringVerify
      // 4D5307EA, 58410889 - XUserEstimateRankForRating
      XELOGD("GenericTask({:08X}, {:08X})", buffer_ptr, buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x0005000E: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("XUserFindUsers({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserFindUsers(buffer_ptr);
    }
    case 0x0005000F: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("XAccountGetUserInfo({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x00050010: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("XAccountGetUserInfo({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x00050036: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 534507D4
      XELOGD("XOnlineQuerySearch({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XOnlineQuerySearch(buffer_ptr);
    }
    case 0x00050038: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 4D5307D3, 4D5307D1, 545407E2, 545407E3, 545407D2, 545407D3
      XELOGD("XOnlineQuerySearch({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XOnlineQuerySearch(buffer_ptr);
    }
    case 0x00050077: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("XAccountGetPointsBalance({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XAccountGetPointsBalance(buffer_ptr);
    }
    case 0x00050079: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // 454107DB
      XELOGD("XAccountGetUserInfo({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x0005008B: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("GetBannerListHot({:08X}, {:08X})", buffer_ptr, buffer_length);
      return GetBannerListHot(buffer_ptr);
    }
    case 0x0005008C: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("GetBannerList({:08X}, {:08X})", buffer_ptr, buffer_length);
      return GetBannerList(buffer_ptr);
    }
    case 0x0005008F: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("ContentEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return ContentEnumerate(buffer_ptr);
    }
    case 0x00050090: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("GenresEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return GenresEnumerate(buffer_ptr);
    }
    case 0x00050091: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("EnumerateTitlesByFilter({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return EnumerateTitlesByFilter(buffer_ptr);
    }
    case 0x00050094: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      // Called on startup of blades dashboard v4532 to v4552
      XELOGD("XLiveBaseUnk50094({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x00050097: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_ASYNC_MESSAGE));
      XELOGD("SubscriptionEnumerate({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return SubscriptionEnumerate(buffer_ptr);
    }
    case 0x00058003: {
      assert_true(!buffer_ptr || !buffer_length);
      // Called on startup of dashboard
      XELOGD("XLiveBaseLogonGetHR({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED;
    }
    case 0x00058004: {
      assert_true(!buffer_length || buffer_length == sizeof(uint32_t));
      XELOGD("XOnlineGetLogonID({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer, 1);
      return X_E_SUCCESS;
    }
    case 0x00058006: {
      assert_true(!buffer_length || buffer_length == sizeof(uint32_t));
      XELOGD("XOnlineGetNatType({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer, X_NAT_TYPE::NAT_OPEN);
      return X_E_SUCCESS;
    }
    case 0x00058007: {
      XELOGD("XOnlineGetServiceInfo({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XOnlineGetServiceInfo(buffer_ptr, buffer_length);
    }
    case 0x00058009: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(X_CONTENT_GET_MARKETPLACE_COUNTS));
      XELOGD("XContentGetMarketplaceCounts({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XContentGetMarketplaceCounts(buffer_ptr);
    }
    case 0x0005800A: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_UPDATE_ACCESS_TIMES));
      XELOGD("XUpdateAccessTimes({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUpdateAccessTimes(buffer_ptr);
    }
    case 0x0005800C: {
      assert_true(!buffer_length);
      // 464F0800
      XELOGD("XUserMuteListAdd({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserMuteListAdd(buffer_ptr);
    }
    case 0x0005800D: {
      assert_true(!buffer_length);
      // 464F0800
      XELOGD("XUserMuteListRemove({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserMuteListRemove(buffer_ptr);
    }
    case 0x0005800E: {
      // 513107D9
      XELOGD("XUserMuteListQuery({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserMuteListQuery(buffer_ptr, buffer_length);
    }
    case 0x00058017: {
      assert_true(!buffer_length);
      XELOGD("GetNextSequenceMessage({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return GetNextSequenceMessage(buffer_ptr);
    }
    case 0x00058019: {
      // 54510846
      XELOGD("XPresenceCreateEnumerator({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceCreateEnumerator(buffer_ptr, buffer_length);
    }
    case 0x0005801C: {
      XELOGD("XPresenceGetState({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceGetState(buffer_ptr, buffer_length);
    }
    case 0x0005801E: {
      // 54510846
      XELOGD("XPresenceSubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceSubscribe(buffer_ptr, buffer_length);
    }
    case 0x0005801F: {
      // 545107D1
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceUnsubscribe(buffer_ptr, buffer_length);
    }
    case 0x00058020: {
      XELOGD("XFriendsCreateEnumerator({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XFriendsCreateEnumerator(buffer_ptr, buffer_length);
    }
    case 0x00058023: {
      // 584107D7
      // 5841091C expects xuid_invitee
      XELOGD("XInviteGetAcceptedInfo({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XInviteGetAcceptedInfo(buffer_ptr, buffer_length);
    }
    case 0x00058024: {
      XELOGD("XMessageEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XMessageEnumerate(buffer_ptr, buffer_length);
    }
    case 0x00058032: {
      assert_true(!buffer_length);
      XELOGD("XOnlineGetTaskProgress({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XOnlineGetTaskProgress(buffer_ptr);
    }
    case 0x00058035: {
      assert_true(!buffer_length);
      XELOGD("XStorageBuildServerPath({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      // 4D5307EA, 4E4D07D3 (Builds Clip Path)
      return XStorageBuildServerPath(buffer_ptr);
    }
    case 0x00058037: {
      // Used in older games such as Crackdown, FM2, Saints Row 1
      XELOGD("XPresenceInitializeLegacy({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceInitialize(buffer_ptr, buffer_length);
    }
    case 0x00058044: {
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceUnsubscribe(buffer_ptr, buffer_length);
    }
    case 0x00058046: {
      // Used in newer games such as Forza 4, MW3, FH2
      //
      // Required to be successful for 4D530910 to detect signed-in profile
      XELOGD("XPresenceInitialize({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceInitialize(buffer_ptr, buffer_length);
    }
  }

  auto xlivebase_log = fmt::format(
      "{} XLIVEBASE message app={:08X}, msg={:08X}, buffer_ptr={:08X}, "
      "buffer_length={:08X}",
      cvars::stub_xlivebase ? "Stubbed" : "Unimplemented", app_id(), message,
      buffer_ptr, buffer_length);

  XELOGE("{}", xlivebase_log);

  return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
}

uint32_t MAX_TITLE_SUBSCRIPTIONS = 0;
uint32_t ACTIVE_TITLE_SUBSCRIPTIONS = 0;

X_HRESULT XLiveBaseApp::XPresenceInitialize(uint32_t buffer_ptr,
                                            uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 1) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const X_PRESENCE_INITIALIZE* initialize =
      memory->TranslateVirtual<X_PRESENCE_INITIALIZE*>(buffer_length);

  const uint32_t max_peer_subscriptions = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          initialize->max_peer_subscriptions.argument_value_ptr)));

  if (max_peer_subscriptions > X_ONLINE_PEER_SUBSCRIPTIONS) {
    return X_E_INVALIDARG;
  }

  MAX_TITLE_SUBSCRIPTIONS = max_peer_subscriptions;

  return X_E_SUCCESS;
}

// Presence information for peers will be registered if they're not friends and
// will be returned in XPresenceCreateEnumerator.
X_HRESULT XLiveBaseApp::XPresenceSubscribe(uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 3) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const X_PRESENCE_SUBSCRIBE* subscribe_args =
      memory->TranslateVirtual<X_PRESENCE_SUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          subscribe_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(
          static_cast<uint32_t>(subscribe_args->peers.argument_value_ptr)));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  if (num_peers <= 0) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address =
      static_cast<uint32_t>(subscribe_args->peer_xuids_ptr.argument_value_ptr);

  if (!xuid_address) {
    return X_E_INVALIDARG;
  }

  const xe::be<uint64_t>* peer_xuids =
      memory->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  for (uint32_t i = 0; i < num_peers; i++) {
    const xe::be<uint64_t> xuid = peer_xuids[i];

    if (!xuid) {
      continue;
    }

    if (profile->IsFriend(xuid)) {
      continue;
    }

    if (ACTIVE_TITLE_SUBSCRIPTIONS <= MAX_TITLE_SUBSCRIPTIONS) {
      ACTIVE_TITLE_SUBSCRIPTIONS++;

      profile->SubscribeFromXUID(xuid);
    } else {
      XELOGI("Max subscriptions reached");
    }
  }

  return X_E_SUCCESS;
}

// Presence information for peers will not longer be returned in
// XPresenceCreateEnumerator unless they're friends.
X_HRESULT XLiveBaseApp::XPresenceUnsubscribe(uint32_t buffer_ptr,
                                             uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 3) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const X_PRESENCE_UNSUBSCRIBE* unsubscribe_args =
      memory->TranslateVirtual<X_PRESENCE_UNSUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          unsubscribe_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(
          static_cast<uint32_t>(unsubscribe_args->peers.argument_value_ptr)));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  if (num_peers <= 0) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address = static_cast<uint32_t>(
      unsubscribe_args->peer_xuids_ptr.argument_value_ptr);

  if (!xuid_address) {
    return X_E_INVALIDARG;
  }

  const xe::be<uint64_t>* peer_xuids =
      memory->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  for (uint32_t i = 0; i < num_peers; i++) {
    const xe::be<uint64_t> xuid = peer_xuids[i];

    if (!xuid) {
      continue;
    }

    if (profile->IsFriend(xuid)) {
      continue;
    }

    if (ACTIVE_TITLE_SUBSCRIPTIONS > 0) {
      ACTIVE_TITLE_SUBSCRIPTIONS--;

      profile->UnsubscribeFromXUID(xuid);
    }
  }

  return X_E_SUCCESS;
}

// Return presence information for a user's friends and subscribed peers.
X_HRESULT XLiveBaseApp::XPresenceCreateEnumerator(uint32_t buffer_ptr,
                                                  uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 7) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const X_PRESENCE_CREATE* create_args = reinterpret_cast<X_PRESENCE_CREATE*>(
      memory->TranslateVirtual(buffer_length));

  const uint32_t user_index =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(
          static_cast<uint32_t>(create_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(
          static_cast<uint32_t>(create_args->num_peers.argument_value_ptr)));
  const uint32_t max_peers =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(
          static_cast<uint32_t>(create_args->max_peers.argument_value_ptr)));
  const uint32_t starting_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          create_args->starting_index.argument_value_ptr)));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  if (num_peers <= 0) {
    return X_E_INVALIDARG;
  }

  if (max_peers > X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  if (starting_index > num_peers) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address =
      static_cast<uint32_t>(create_args->peer_xuids_ptr.argument_value_ptr);
  const uint32_t buffer_address =
      static_cast<uint32_t>(create_args->buffer_length_ptr.argument_value_ptr);
  const uint32_t handle_address = static_cast<uint32_t>(
      create_args->enumerator_handle_ptr.argument_value_ptr);

  if (!xuid_address) {
    return X_E_INVALIDARG;
  }

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  auto e = make_object<XStaticEnumerator<X_ONLINE_PRESENCE>>(kernel_state_,
                                                             num_peers);
  auto result = e->Initialize(user_index, app_id(), 0x5801A, 0x5801B, 0);

  if (XFAILED(result)) {
    return result;
  }

  const xe::be<uint64_t>* peer_xuids_ptr =
      memory->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);

  const auto peer_xuids =
      std::vector<uint64_t>(peer_xuids_ptr, peer_xuids_ptr + num_peers);

  UpdatePresenceXUIDs(peer_xuids, user_index);

  for (auto i = starting_index; i < e->items_per_enumerate(); i++) {
    const xe::be<uint64_t> xuid = peer_xuids[i];

    if (!xuid) {
      continue;
    }

    if (profile->IsFriend(xuid)) {
      auto item = e->AppendItem();

      profile->GetFriendPresenceFromXUID(xuid, item);
    } else if (profile->IsSubscribed(xuid)) {
      auto item = e->AppendItem();

      profile->GetSubscriptionFromXUID(xuid, item);
    }
  }

  uint32_t* buffer_size_ptr =
      memory->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory->TranslateVirtual<uint32_t*>(handle_address);

  const uint32_t presence_buffer_size =
      static_cast<uint32_t>(e->items_per_enumerate() * e->item_size());

  *buffer_size_ptr = xe::byte_swap<uint32_t>(presence_buffer_size);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());

  return X_E_SUCCESS;
}

// Backwards compatible XLSP
X_HRESULT XLiveBaseApp::XOnlineQuerySearch(uint32_t buffer_ptr) {
  // Usually called after success returned from XOnlineGetServiceInfo.

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XQuerySearchUnmarshaller* unmarshaller =
      new XQuerySearchUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller->PrettyPrintAttributesSpec();

  unmarshaller->ZeroResults();

  QUERY_SEARCH_RESULT* results_ptr =
      unmarshaller->Results<QUERY_SEARCH_RESULT>();

  const auto services = XLiveAPI::GetServices();

  results_ptr->total_results =
      static_cast<uint32_t>(services->QuerySearchResults().size());
  results_ptr->returned_results =
      static_cast<uint32_t>(services->QuerySearchResults().size());
  results_ptr->num_result_attributes = static_cast<uint32_t>(
      unmarshaller->SpecAttributes().size() * results_ptr->returned_results);

  X_ONLINE_QUERY_ATTRIBUTE* attributes_ptr =
      reinterpret_cast<X_ONLINE_QUERY_ATTRIBUTE*>(results_ptr + 1);

  uint32_t attributes_address = kernel_state_->memory()->HostToGuestVirtual(
      std::to_address(attributes_ptr));

  results_ptr->attributes_ptr = attributes_address;

  for (uint32_t gateway_index = 0;
       const auto& gateway : services->QuerySearchResults()) {
    uint32_t attribute_index = static_cast<uint32_t>(
        (gateway_index * unmarshaller->SpecAttributes().size()));

    uint8_t* binary_alloc_ptr = reinterpret_cast<uint8_t*>(
        attributes_ptr + results_ptr->num_result_attributes);

    for (auto const& attribute : unmarshaller->SpecAttributes()) {
      switch (attribute.type) {
        case X_ONLINE_LSP_ATTRIBUTE_TSADDR: {
          assert_false(attribute.length != sizeof(TSADDR));

          // TODO(Adrian): Instead of allocating use title allocated buffer
          // results_ptr.
          uint32_t TSADDR_adderess =
              kernel_state()->memory()->SystemHeapAlloc(sizeof(TSADDR));

          TSADDR* TSADDR_ptr =
              kernel_state()->memory()->TranslateVirtual<TSADDR*>(
                  TSADDR_adderess);

          *TSADDR_ptr = gateway;

          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_TSADDR;
          attributes_ptr[attribute_index].info.blob.length = sizeof(TSADDR);
          attributes_ptr[attribute_index].info.blob.value_ptr = TSADDR_adderess;
        } break;
        case X_ONLINE_LSP_ATTRIBUTE_XNKID: {
          assert_false(attribute.length != sizeof(XNKID));

          uint32_t XNKID_adderess =
              kernel_state()->memory()->SystemHeapAlloc(sizeof(XNKID));

          XNKID* XNKID_ptr = kernel_state()->memory()->TranslateVirtual<XNKID*>(
              XNKID_adderess);

          xe::be<uint64_t> session_id = GenerateSessionId(XNKID_SERVER);

          std::memcpy(XNKID_ptr, &session_id, sizeof(XNKID));

          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_XNKID;
          attributes_ptr[attribute_index].info.blob.length = sizeof(XNKID);
          attributes_ptr[attribute_index].info.blob.value_ptr = XNKID_adderess;
        } break;
        case X_ONLINE_LSP_ATTRIBUTE_KEY: {
          assert_false(attribute.length != sizeof(XNKEY));

          uint32_t XNKEY_adderess =
              kernel_state()->memory()->SystemHeapAlloc(sizeof(XNKEY));

          XNKEY* XNKEY_ptr = kernel_state()->memory()->TranslateVirtual<XNKEY*>(
              XNKEY_adderess);

          GenerateIdentityExchangeKey(XNKEY_ptr);

          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_KEY;
          attributes_ptr[attribute_index].info.blob.length = sizeof(XNKEY);
          attributes_ptr[attribute_index].info.blob.value_ptr = XNKEY_adderess;
        } break;
        case X_ONLINE_LSP_ATTRIBUTE_USER:
          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_USER;
          break;
        case X_ONLINE_LSP_ATTRIBUTE_PARAM_USER:
          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_PARAM_USER;
          break;
        case X_ATTRIBUTE_DATATYPE_INTEGER:
          attributes_ptr[attribute_index].attribute_id =
              X_ATTRIBUTE_DATATYPE_INTEGER;
          attributes_ptr[attribute_index].info.integer.length = 0;
          attributes_ptr[attribute_index].info.integer.value = 0;
          break;
        case X_ATTRIBUTE_DATATYPE_STRING: {
          // PGR3 & PDZ use X_ATTRIBUTE_DATATYPE_STRING

          // PGR3
          std::u16string filter = u"VINCE";

          uint32_t size = static_cast<uint32_t>(
              xe::string_util::size_in_bytes(filter, true));

          // LSP Filter?
          uint32_t string_adderess =
              kernel_state()->memory()->SystemHeapAlloc(size);

          char16_t* string_ptr =
              kernel_state()->memory()->TranslateVirtual<char16_t*>(
                  string_adderess);

          xe::string_util::copy_and_swap_truncating(string_ptr, filter.c_str(),
                                                    filter.size() + 1);

          attributes_ptr[attribute_index].attribute_id =
              X_ATTRIBUTE_DATATYPE_STRING;
          attributes_ptr[attribute_index].info.string.length =
              static_cast<uint32_t>(filter.size() + 1);
          attributes_ptr[attribute_index].info.string.value_ptr =
              string_adderess;
        } break;
        case X_ATTRIBUTE_DATATYPE_BLOB:
          attributes_ptr[attribute_index].attribute_id =
              X_ATTRIBUTE_DATATYPE_BLOB;
          break;
        default:
          assert_always();
          break;
      }

      attribute_index++;
    }

    gateway_index++;
  }

  XELOGI("{}: Total Gateways: {}, Returned Gateways: {}, Attributes: {}",
         __func__, results_ptr->total_results.get(),
         results_ptr->returned_results.get(), unmarshaller->NumAttributes());

  return X_E_SUCCESS;
}

// Check whether XLSP services are available
X_HRESULT XLiveBaseApp::XOnlineGetServiceInfo(uint32_t serviceid,
                                              uint32_t serviceinfo) {
  if (!XLiveAPI::IsConnectedToServer()) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  if (!serviceinfo) {
    return X_E_SUCCESS;
  }

  X_ONLINE_SERVICE_INFO* service_info_ptr =
      memory_->TranslateVirtual<X_ONLINE_SERVICE_INFO*>(serviceinfo);

  std::memset(service_info_ptr, 0, sizeof(X_ONLINE_SERVICE_INFO));

  const auto services = XLiveAPI::GetServices();

  for (const auto& service_info : services->ServicesResults()) {
    if (service_info.id == serviceid) {
      std::memcpy(service_info_ptr, &service_info,
                  sizeof(X_ONLINE_SERVICE_INFO));
    }
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XFriendsCreateEnumerator(uint32_t buffer_ptr,
                                                 uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 5) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  X_CREATE_FRIENDS_ENUMERATOR* friends_enumerator =
      memory->TranslateVirtual<X_CREATE_FRIENDS_ENUMERATOR*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->user_index.argument_value_ptr)));
  const uint32_t friends_starting_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->friends_starting_index.argument_value_ptr)));
  const uint32_t friends_amount = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->friends_amount.argument_value_ptr)));

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (friends_starting_index >= X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  if (friends_amount > X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  const uint32_t buffer_address =
      static_cast<uint32_t>(friends_enumerator->buffer_ptr.argument_value_ptr);
  const uint32_t handle_address =
      static_cast<uint32_t>(friends_enumerator->handle_ptr.argument_value_ptr);

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  uint32_t* buffer_size_ptr =
      memory->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory->TranslateVirtual<uint32_t*>(handle_address);

  *buffer_size_ptr = 0;
  *handle_ptr = X_INVALID_HANDLE_VALUE;

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  auto const profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  auto e = make_object<XStaticEnumerator<X_ONLINE_FRIEND>>(kernel_state_,
                                                           friends_amount);
  auto result = e->Initialize(-1, app_id(), 0x58021, 0x58022, 0);

  if (XFAILED(result)) {
    return result;
  }

  UpdateFriendPresence(user_index);

  for (auto i = friends_starting_index; i < e->items_per_enumerate(); i++) {
    X_ONLINE_FRIEND peer = {};

    const bool is_friend = profile->GetFriendFromIndex(i, &peer);

    if (is_friend) {
      auto item = e->AppendItem();

      memcpy(item, &peer, sizeof(X_ONLINE_FRIEND));
    }
  }

  const uint32_t friends_buffer_size =
      static_cast<uint32_t>(e->items_per_enumerate() * e->item_size());

  *buffer_size_ptr = xe::byte_swap<uint32_t>(friends_buffer_size);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());
  return X_E_SUCCESS;
}

void XLiveBaseApp::UpdateFriendPresence(const uint32_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return;
  }

  auto const profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  const std::vector<uint64_t> peer_xuids = profile->GetFriendsXUIDs();

  UpdatePresenceXUIDs(peer_xuids, user_index);
}

void XLiveBaseApp::UpdatePresenceXUIDs(const std::vector<uint64_t>& xuids,
                                       const uint32_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return;
  }

  auto const profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  const auto presences = XLiveAPI::GetFriendsPresence(xuids);

  for (const auto& player : presences->PlayersPresence()) {
    const uint64_t xuid = player.XUID();

    if (!profile->IsFriend(xuid) && !profile->IsSubscribed(xuid)) {
      XELOGI("Requested unknown peer presence: {} - {:016X}", player.Gamertag(),
             xuid);
      continue;
    }

    if (profile->IsFriend(xuid)) {
      X_ONLINE_FRIEND peer = player.GetFriendPresence();

      profile->SetFriend(peer);
    } else if (profile->IsSubscribed(xuid)) {
      X_ONLINE_PRESENCE presence = player.ToOnlineRichPresence();

      profile->SetSubscriptionFromXUID(xuid, &presence);
    }
  }
}

X_HRESULT XLiveBaseApp::XInviteSend(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  // Current session must have PRESENCE flag.

  XInviteSendUnmarshaller* unmarshaller =
      new XInviteSendUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  new xe::ui::HostNotificationWindow(
      kernel_state()->emulator()->imgui_drawer(), "Invites aren't supported!",
      xe::to_utf8(unmarshaller->DisplayString()), 0);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XInviteGetAcceptedInfo(uint32_t buffer_ptr,
                                               uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 2) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const X_INVITE_GET_ACCEPTED_INFO* accepted_info =
      memory->TranslateVirtual<X_INVITE_GET_ACCEPTED_INFO*>(buffer_length);

  const uint32_t user_index =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(accepted_info->user_index.argument_value_ptr)));

  X_INVITE_INFO* invite_info = reinterpret_cast<X_INVITE_INFO*>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          accepted_info->invite_info.argument_value_ptr)));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_FAIL;
  }

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  memcpy(invite_info, user_profile->GetSelfInvite(), sizeof(X_INVITE_INFO));
  memset(user_profile->GetSelfInvite(), 0, sizeof(X_INVITE_INFO));

  const std::vector<uint64_t> xuids = {invite_info->xuid_inviter};

  const auto presence = XLiveAPI::GetFriendsPresence(xuids);

  uint64_t session_id = 0;

  if (!presence->PlayersPresence().empty()) {
    session_id = presence->PlayersPresence().front().SessionID();
  }

  if (!session_id) {
    new xe::ui::HostNotificationWindow(
        kernel_state()->emulator()->imgui_drawer(), "Joining Session",
        "Unable to join session", 0);

    return X_ONLINE_E_SESSION_NOT_FOUND;
  }

  const auto session = XLiveAPI::XSessionGet(session_id);

  if (!session->SessionID_UInt()) {
    new xe::ui::HostNotificationWindow(
        kernel_state()->emulator()->imgui_drawer(), "Joining Session",
        "Unable to join session", 0);

    return X_ONLINE_E_SESSION_NOT_FOUND;
  }

  std::set<uint64_t> local_members = {};

  for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
    const auto profile = kernel_state()->xam_state()->GetUserProfile(i);

    if (profile && profile->IsLiveEnabled()) {
      local_members.insert(profile->GetOnlineXUID());
    }
  }

  XLiveAPI::SessionPreJoin(session_id, local_members);

  // Use GetXnAddrFromSessionObject
  Uint64toXNKID(session->SessionID_UInt(), &invite_info->host_info.sessionID);
  GenerateIdentityExchangeKey(&invite_info->host_info.keyExchangeKey);

  memset(&invite_info->host_info.hostAddress, 0, sizeof(XNADDR));

  invite_info->host_info.hostAddress.inaOnline =
      ip_to_in_addr(session->HostAddress());
  invite_info->host_info.hostAddress.ina =
      ip_to_in_addr(session->HostAddress());

  const MacAddress mac = MacAddress(session->MacAddress());

  memcpy(&invite_info->host_info.hostAddress.abEnet, mac.raw(),
         sizeof(MacAddress));

  invite_info->host_info.hostAddress.wPortOnline = session->Port();

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GenericMarshalled(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  const std::string_view task_url = unmarshaller->GetAsyncTask()->GetTaskUrl();

  // Determine function via it's URL, some functions don't have a URL if the url
  // index is 0.
  const std::string_view string_verify_url = "/msgserver/vetstring2.ashx";
  const std::string_view account_get_user_info_url =
      "/xuacs/XeGetUserInfo.ashx";
  const std::string_view estimate_rank_for_rating_url =
      "/xstats/xstatestimaterankforratings.ashx";

  if (string_verify_url == task_url) {
    return XStringVerify(buffer_ptr);
  }

  if (account_get_user_info_url == task_url) {
    return XAccountGetUserInfo(buffer_ptr);
  }

  if (estimate_rank_for_rating_url == task_url) {
    return XUserEstimateRankForRating(buffer_ptr);
  }

  XELOGI("{}:: URL: {}", __func__, task_url);

  uint8_t* args_ptr = unmarshaller->DeserializeReinterpret<uint8_t>();

  if (!args_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  uint8_t* results_ptr = unmarshaller->Results<uint8_t>();

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserMuteListQuery(uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  X_MUTE_SET_STATE* remote_player_ptr =
      memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

  if (remote_player_ptr->user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile = kernel_state()->xam_state()->GetUserProfile(
      remote_player_ptr->user_index);

  xe::be<uint32_t>* mute_list_ptr =
      memory_->TranslateVirtual<xe::be<uint32_t>*>(buffer_length);

  *mute_list_ptr = user_profile->IsPlayerMuted(remote_player_ptr->remote_xuid);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserMuteListAdd(uint32_t buffer_ptr) {
  X_MUTE_SET_STATE* remote_player_ptr =
      memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

  if (remote_player_ptr->user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile = kernel_state()->xam_state()->GetUserProfile(
      remote_player_ptr->user_index);

  bool muted = user_profile->MutePlayer(remote_player_ptr->remote_xuid);

  if (muted) {
    kernel_state()->BroadcastNotification(kXNotificationSystemMuteListChanged,
                                          0);
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserMuteListRemove(uint32_t buffer_ptr) {
  X_MUTE_SET_STATE* remote_player_ptr =
      memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

  if (remote_player_ptr->user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile = kernel_state()->xam_state()->GetUserProfile(
      remote_player_ptr->user_index);

  bool unmuted = user_profile->UnmutePlayer(remote_player_ptr->remote_xuid);

  if (unmuted) {
    kernel_state()->BroadcastNotification(kXNotificationSystemMuteListChanged,
                                          0);
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XAccountGetUserInfo(uint32_t buffer_ptr) {
  // Requires privilege XEX_SYSTEM_ACCESS_PII (Personally Identifiable
  // Information)
  //
  // 41560855 (TU 7+), 4D530AA5

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XAccountGetUserInfoUnmarshaller* unmarshaller =
      new XAccountGetUserInfoUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  X_GET_USER_INFO_RESPONSE* user_info_response_ptr =
      unmarshaller->Results<X_GET_USER_INFO_RESPONSE>();

  unmarshaller->ZeroResults();

  // Example usage
  std::u16string first_name = u"First Name";
  std::u16string last_name = u"Last Name";

  char16_t* first_name_ptr =
      reinterpret_cast<char16_t*>(user_info_response_ptr + 1);
  string_util::copy_and_swap_truncating(first_name_ptr, first_name.data(),
                                        MAX_FIRSTNAME_SIZE);

  char16_t* last_name_ptr =
      reinterpret_cast<char16_t*>(first_name_ptr + MAX_FIRSTNAME_SIZE);
  string_util::copy_and_swap_truncating(last_name_ptr, last_name.data(),
                                        MAX_LASTNAME_SIZE);

  user_info_response_ptr->first_name_length =
      static_cast<uint32_t>(first_name.size());
  user_info_response_ptr->first_name =
      kernel_state()->memory()->HostToGuestVirtual(
          std::to_address(first_name_ptr));

  user_info_response_ptr->last_name_length =
      static_cast<uint32_t>(last_name.size());
  user_info_response_ptr->last_name =
      kernel_state()->memory()->HostToGuestVirtual(
          std::to_address(last_name_ptr));

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageEnumerate(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageEnumerateUnmarshaller* unmarshaller =
      new XStorageEnumerateUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  // Fixed 415607F7 from crashing.
  unmarshaller->ZeroResults();

  X_STORAGE_ENUMERATE_RESULTS* results_ptr =
      unmarshaller->Results<X_STORAGE_ENUMERATE_RESULTS>();

  auto user_profle =
      kernel_state()->xam_state()->GetUserProfile(unmarshaller->UserIndex());

  const std::string enumeration_path = xe::to_utf8(unmarshaller->ServerPath());

  const uint32_t available_to_return_items =
      static_cast<uint32_t>(std::floor<uint32_t>(static_cast<uint32_t>(
          (unmarshaller->GetAsyncTask()->GetXLiveAsyncTask()->results_size -
           sizeof(X_STORAGE_ENUMERATE_RESULTS)) /
          (sizeof(X_STORAGE_FILE_INFO) +
           (X_ONLINE_MAX_PATHNAME_LENGTH * sizeof(char16_t))))));

  X_STORAGE_FILE_INFO* items_array_ptr =
      reinterpret_cast<X_STORAGE_FILE_INFO*>(results_ptr + 1);

  uint32_t items_address = kernel_state_->memory()->HostToGuestVirtual(
      std::to_address(items_array_ptr));

  results_ptr->items_ptr = items_address;

  char16_t* items_path_name_array_ptr =
      reinterpret_cast<char16_t*>(items_array_ptr + available_to_return_items);

  X_STATUS result = X_E_SUCCESS;

  if (!available_to_return_items) {
    return X_E_INVALIDARG;
  }

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromServerPath(enumeration_path);

  if (facility_type != X_STORAGE_FACILITY::FACILITY_PER_TITLE) {
    XELOGI("{}: Unsupported Storage Facility: {}", __func__,
           static_cast<uint32_t>(facility_type));
    return X_E_FAIL;
  }

  XContentType content_type =
      facility_type == X_STORAGE_FACILITY::FACILITY_PER_TITLE
          ? XContentType::kPublisher
          : XContentType::kSavedGame;

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  std::string final_enumeration_path = enumeration_path;
  bool enumerated_backend = false;

  if (route_backend) {
    const uint32_t max_items = std::min<uint32_t>(
        unmarshaller->MaxResultsToReturn(), available_to_return_items);

    const auto enumeration_result =
        XLiveAPI::XStorageEnumerate(enumeration_path, max_items);

    const auto& enumerated_files = enumeration_result.first;
    enumerated_backend = enumeration_result.second;

    for (uint32_t item_index = unmarshaller->StartingIndex();
         const auto& entry : enumerated_files->Items()) {
      std::string filename = utf8::find_name_from_path(entry.FilePath(), '/');

      // Path must use /
      std::u16string backend_item_path = xe::to_utf16(
          std::format("{}{}", XLiveAPI::GetApiAddress(), entry.FilePath()));

      char16_t* item_path_ptr =
          std::to_address(items_path_name_array_ptr +
                          (item_index * (X_ONLINE_MAX_PATHNAME_LENGTH)));

      uint32_t item_path_address =
          kernel_state_->memory()->HostToGuestVirtual(item_path_ptr);

      xe::string_util::copy_and_swap_truncating(
          item_path_ptr, backend_item_path, X_ONLINE_MAX_PATHNAME_LENGTH);

      items_array_ptr[item_index].path_name =
          static_cast<uint32_t>(backend_item_path.size());
      items_array_ptr[item_index].path_name_ptr = item_path_address;

      items_array_ptr[item_index].title_id = entry.TitleID();
      items_array_ptr[item_index].title_version = entry.TitleVersion();
      items_array_ptr[item_index].owner_puid = entry.OwnerPUID();
      items_array_ptr[item_index].country_id = entry.CountryID();
      items_array_ptr[item_index].content_type = entry.ContentType();
      items_array_ptr[item_index].storage_size =
          entry.StorageSize();  // XStorageDownloadToMemory -> buffer_size
                                // 464F07ED
      items_array_ptr[item_index].installed_size =
          entry.InstalledSize();  // XStorageDownloadToMemory -> buffer_size
                                  // 45410914
      items_array_ptr[item_index].ft_created =
          static_cast<uint64_t>(entry.Created());
      items_array_ptr[item_index].ft_last_modified =
          static_cast<uint64_t>(entry.LastModified());

      results_ptr->num_items_returned += 1;

      XELOGI("{}: Added storage item: {}", __func__, filename);

      item_index++;

      if (item_index >= available_to_return_items) {
        break;
      }
    }

    if (enumerated_backend) {
      results_ptr->total_num_items = enumerated_files->TotalNumItems();
      result = X_E_SUCCESS;
    }
  }

  if (!route_backend || !enumerated_backend) {
    final_enumeration_path =
        ConvertServerPathToXStorageSymlink(enumeration_path);

    std::string filename = utf8::find_name_from_path(enumeration_path, '/');

    std::string item_parent =
        std::filesystem::path(final_enumeration_path).parent_path().string();

    // Match Wildcards: /*file.cfg and filename literal file.cfg
    std::string wildcard_item_filename = filename;
    std::string item_filename_literal = filename;

    bool has_wildcard = false;

    if (std::find(wildcard_item_filename.begin(), wildcard_item_filename.end(),
                  '*') != wildcard_item_filename.end()) {
      has_wildcard = true;

      std::replace(wildcard_item_filename.begin(), wildcard_item_filename.end(),
                   '*', '?');
      std::erase(item_filename_literal, '*');
    }

    vfs::Entry* folder =
        kernel_state()->file_system()->ResolvePath(item_parent);

    uint32_t total_num_items = 0;

    if (folder) {
      for (const auto& child : folder->children()) {
        if (!(child->attributes() & FILE_ATTRIBUTE_DIRECTORY)) {
          total_num_items += 1;
        }
      }
    }

    xe::filesystem::WildcardEngine enumeration_engine;
    enumeration_engine.SetRule(wildcard_item_filename);

    size_t itr_index = 0;
    std::vector<vfs::Entry*> entries = {};

    vfs::Entry* entry = nullptr;

    if (folder) {
      do {
        entry = folder->IterateChildren(enumeration_engine, &itr_index);

        if (entry) {
          if (!(entry->attributes() & FILE_ATTRIBUTE_DIRECTORY)) {
            entries.push_back(entry);
          }
        }
      } while (entry && entries.size() < available_to_return_items);
    }

    if (has_wildcard && !item_filename_literal.empty()) {
      entry = folder->GetChild(item_filename_literal);

      if (entry) {
        entries.push_back(entry);
      }
    }

    // Files are returned in order from most to least recently modified.
    std::sort(entries.begin(), entries.end(),
              [](const vfs::Entry* entry_1, const vfs::Entry* entry_2) {
                return entry_1->write_timestamp() > entry_2->write_timestamp();
              });

    for (uint32_t item_index = unmarshaller->StartingIndex();
         const auto entry : entries) {
      std::string symlink_item_path =
          std::format("{}\\{}", item_parent, entry->name());

      // Path must use /
      // Keep server return consistent with XStorageBuildServerPath
      std::u16string backend_storage_item_path =
          xe::to_utf16(ConvertXStorageSymlinkToServerPath(symlink_item_path));

      char16_t* item_path_ptr =
          std::to_address(items_path_name_array_ptr +
                          (item_index * (X_ONLINE_MAX_PATHNAME_LENGTH)));

      uint32_t item_path_address =
          kernel_state_->memory()->HostToGuestVirtual(item_path_ptr);

      xe::string_util::copy_and_swap_truncating(item_path_ptr,
                                                backend_storage_item_path,
                                                X_ONLINE_MAX_PATHNAME_LENGTH);

      items_array_ptr[item_index].path_name =
          static_cast<uint32_t>(backend_storage_item_path.size());
      items_array_ptr[item_index].path_name_ptr = item_path_address;

      const uint32_t size_bytes = static_cast<uint32_t>(entry->size());

      items_array_ptr[item_index].title_id = kernel_state()->title_id();
      items_array_ptr[item_index].title_version = 0;
      items_array_ptr[item_index].owner_puid =
          user_profle ? user_profle->xuid() : 0;
      items_array_ptr[item_index].country_id = cvars::user_country;
      items_array_ptr[item_index].content_type =
          static_cast<uint32_t>(content_type);
      items_array_ptr[item_index].storage_size =
          size_bytes;  // XStorageDownloadToMemory -> buffer_size 464F07ED
      items_array_ptr[item_index].installed_size =
          size_bytes;  // XStorageDownloadToMemory -> buffer_size 45410914
      items_array_ptr[item_index].ft_created = entry->create_timestamp();
      items_array_ptr[item_index].ft_last_modified = entry->write_timestamp();

      results_ptr->num_items_returned += 1;

      XELOGI("{}: Added storage item: {}", __func__, entry->name());

      item_index++;

      if (item_index >= available_to_return_items) {
        break;
      }
    }

    results_ptr->total_num_items = total_num_items;
    result = X_E_SUCCESS;
  }

  XELOGI(
      "{}: Available Items Space: {}, Storage Items: {}, Start Index: {}, Max "
      "Items: {}, Server Path: {}",
      __func__, available_to_return_items,
      results_ptr->num_items_returned.get(), unmarshaller->StartingIndex(),
      unmarshaller->MaxResultsToReturn(), final_enumeration_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStringVerify(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStringVerifyUnmarshaller* unmarshaller =
      new XStringVerifyUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller->ZeroResults();

  STRING_VERIFY_RESPONSE* responses_ptr =
      unmarshaller->Results<STRING_VERIFY_RESPONSE>();

  for (auto const& string_to_verify : unmarshaller->StringToVerify()) {
    XELOGI("{}: {}", __func__, string_to_verify);
  }

  uint32_t response_result_address =
      kernel_state_->memory()->HostToGuestVirtual(
          std::to_address(responses_ptr + 1));

  HRESULT* response_results_ptr =
      kernel_state_->memory()->TranslateVirtual<HRESULT*>(
          response_result_address);

  for (uint32_t i = 0; i < unmarshaller->NumStrings(); i++) {
    response_results_ptr[i] = X_E_SUCCESS;
  }

  responses_ptr->num_strings = unmarshaller->NumStrings();
  responses_ptr->string_result_ptr = response_result_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserEstimateRankForRating(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XUserEstimateRankForRatingUnmarshaller* unmarshaller =
      new XUserEstimateRankForRatingUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller->ZeroResults();

  const uint32_t max_num_ranks_results =
      (unmarshaller->GetAsyncTask()->GetXLiveAsyncTask()->results_size -
       sizeof(X_USER_ESTIMATE_RANK_RESULTS)) /
      sizeof(uint32_t);

  X_USER_ESTIMATE_RANK_RESULTS* estimate_rank_results_ptr =
      unmarshaller->Results<X_USER_ESTIMATE_RANK_RESULTS>();

  xe::be<uint32_t>* ranks =
      reinterpret_cast<xe::be<uint32_t>*>(estimate_rank_results_ptr + 1);

  // 58410889 expects ptr even if num_ranks is 0.
  estimate_rank_results_ptr->ranks_ptr =
      kernel_state()->memory()->HostToGuestVirtual(std::to_address(ranks));

  // XPROPERTY_ATTACHMENT_SIZE?

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageDelete(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageDeleteUnmarshaller* unmarshaller =
      new XStorageDeleteUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  std::string item_path = xe::to_utf8(unmarshaller->ServerPath());

  X_STATUS result = X_E_FAIL;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromServerPath(item_path);

  if (facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE) {
    XELOGI("{}: Unsupported Storage Facility: {}", __func__,
           static_cast<uint32_t>(facility_type));
    return X_ONLINE_E_STORAGE_INVALID_FACILITY;
  }

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    bool deleted = XLiveAPI::XStorageDelete(item_path);
    result = deleted ? X_E_SUCCESS : X_E_FAIL;
  }

  if (!route_backend || result) {
    item_path = ConvertServerPathToXStorageSymlink(item_path);

    vfs::Entry* storage_item =
        kernel_state()->file_system()->ResolvePath(item_path);

    if (storage_item) {
      result = storage_item->Delete() ? X_E_SUCCESS : X_E_FAIL;
    } else {
      result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
    }
  }

  XELOGI("{}: {}", __func__, item_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageDownloadToMemory(uint32_t buffer_ptr) {
  // 41560817, 513107D5, 513107D9, 415607DD, 415607DD

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageDownloadToMemoryUnmarshaller* unmarshaller =
      new XStorageDownloadToMemoryUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller->ZeroResults();

  X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS* download_results_ptr =
      unmarshaller->Results<X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS>();

  std::span<uint8_t> download_buffer = unmarshaller->GetDownloadBuffer();

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(unmarshaller->UserIndex());

  uint64_t xuid_owner = 0;

  if (user_profile) {
    xuid_owner = user_profile->GetOnlineXUID();
  }

  std::string item_to_download = xe::to_utf8(unmarshaller->ServerPath());

  X_STATUS result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromServerPath(item_to_download);

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    std::span<uint8_t> buffer = XLiveAPI::XStorageDownload(item_to_download);

    if (!buffer.empty()) {
      if (buffer.size_bytes() > download_buffer.size_bytes()) {
        XELOGI("{}: Provided file size {}b is larger than expected {}b",
               __func__, buffer.size_bytes(), download_buffer.size_bytes());
        return X_E_INSUFFICIENT_BUFFER;
      }

      memcpy(download_buffer.data(), buffer.data(), buffer.size_bytes());

      // Possible solution: Use custom HTTP header or encode binary in base64
      // with metadata
      download_results_ptr->xuid_owner = xuid_owner;
      download_results_ptr->bytes_total =
          static_cast<uint32_t>(buffer.size_bytes());
      download_results_ptr->ft_created = time(0);

      result = X_E_SUCCESS;
    } else {
      result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
    }
  }

  if (!route_backend || result) {
    std::string filename = utf8::find_name_from_path(item_to_download, '/');
    item_to_download = ConvertServerPathToXStorageSymlink(item_to_download);

    xe::vfs::File* output_file;
    xe::vfs::FileAction action = {};

    X_STATUS open_result = kernel_state_->file_system()->OpenFile(
        nullptr, item_to_download, xe::vfs::FileDisposition::kOpen,
        xe::vfs::FileAccess::kFileReadData, false, true, &output_file, &action);

    if (!open_result) {
      std::vector<uint8_t> file_data =
          std::vector<uint8_t>(output_file->entry()->size());

      size_t bytes_read = 0;
      open_result = output_file->ReadSync(
          {file_data.data(), output_file->entry()->size()}, 0, &bytes_read);

      output_file->Destroy();

      if (!open_result) {
        if (bytes_read > unmarshaller->BufferSize()) {
          XELOGI("{}: Provided file size {}b is larger than expected {}b",
                 __func__, bytes_read, unmarshaller->BufferSize());
          return X_E_INSUFFICIENT_BUFFER;
        }

        memcpy(download_buffer.data(), file_data.data(), bytes_read);

        download_results_ptr->xuid_owner = xuid_owner;
        download_results_ptr->bytes_total = static_cast<uint32_t>(bytes_read);
        download_results_ptr->ft_created =
            output_file->entry()->create_timestamp();

        result = X_E_SUCCESS;
      } else {
        XELOGI("{}: Failed to download: {}", __func__, filename);
        result = X_E_FAIL;
      }
    } else {
      XELOGI("{}: {} doesn't exist!", __func__, filename);
      result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
    }
  }

  XELOGI("{}: Downloaded Bytes: {}b, Buffer Size: {}b, Server Path: {}",
         __func__, download_results_ptr->bytes_total.get(),
         unmarshaller->BufferSize(), item_to_download);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageUploadFromMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageUploadToMemoryUnmarshaller* unmarshaller =
      new XStorageUploadToMemoryUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  std::span<uint8_t> upload_buffer = unmarshaller->GetUploadBuffer();

  std::string upload_file_path = xe::to_utf8(unmarshaller->ServerPath());
  std::string filename = utf8::find_name_from_path(upload_file_path, '/');

  X_STATUS result = X_E_FAIL;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromServerPath(upload_file_path);

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    X_STORAGE_UPLOAD_RESULT uploaded_result =
        XLiveAPI::XStorageUpload(upload_file_path, upload_buffer);

    switch (uploaded_result) {
      case X_STORAGE_UPLOAD_RESULT::UPLOADED:
        result = X_E_SUCCESS;
        break;
      case X_STORAGE_UPLOAD_RESULT::NOT_MODIFIED:
        result = X_ONLINE_S_STORAGE_FILE_NOT_MODIFIED;
        break;
      case X_STORAGE_UPLOAD_RESULT::PAYLOAD_TOO_LARGE:
        result = X_ONLINE_E_STORAGE_FILE_IS_TOO_BIG;
        break;
      case X_STORAGE_UPLOAD_RESULT::UPLOAD_ERROR:
      default:
        result = X_E_FAIL;
        break;
    }
  }

  if (!route_backend || result == X_E_FAIL) {
    upload_file_path = ConvertServerPathToXStorageSymlink(upload_file_path);

    std::string upload_file_path_parent =
        std::filesystem::path(upload_file_path).parent_path().string();

    // Check if entry exists
    vfs::Entry* entry =
        kernel_state()->file_system()->ResolvePath(upload_file_path_parent);

    if (entry) {
      vfs::File* upload_file = nullptr;
      vfs::FileAction action;

      result = kernel_state()->file_system()->OpenFile(
          nullptr, upload_file_path, vfs::FileDisposition::kOverwriteIf,
          vfs::FileAccess::kGenericWrite, false, true, &upload_file, &action);

      if (!result) {
        size_t bytes_written = 0;
        result = upload_file->WriteSync(
            {upload_buffer.data(), unmarshaller->BufferSize()}, 0,
            &bytes_written);

        // Update the size of the entry for XStorageDownloadToMemory
        upload_file->entry()->update();

        upload_file->Destroy();
      }
    }
  }

  switch (result) {
    case X_E_SUCCESS:
      XELOGI("{}: Uploaded {}", __func__, filename);
      break;
    case X_ONLINE_S_STORAGE_FILE_NOT_MODIFIED:
      XELOGI("{}: Uploaded {} (Not Modified)", __func__, filename);
      break;
    case X_E_FAIL:
      XELOGI("{}: Uploading {} failed with error {:08X}", __func__, filename,
             result);
      break;
  }

  XELOGI("{}: Size: {}b, Path: {}", __func__, unmarshaller->BufferSize(),
         upload_file_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageBuildServerPath(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  X_STORAGE_BUILD_SERVER_PATH* args =
      kernel_state_->memory()->TranslateVirtual<X_STORAGE_BUILD_SERVER_PATH*>(
          buffer_ptr);

  if (!args->file_name_ptr) {
    return X_E_INVALIDARG;
  }

  if (!args->server_path_length_ptr) {
    return X_E_INVALIDARG;
  }

  if (args->user_index >= XUserMaxUserCount &&
      args->user_index != XUserIndexNone) {
    return X_E_INVALIDARG;
  }

  uint64_t xuid = 0;

  if (args->user_index == XUserIndexNone) {
    xuid = args->xuid;
  }

  bool xuid_reqiured =
      args->storage_location == X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE ||
      args->storage_location == X_STORAGE_FACILITY::FACILITY_GAME_CLIP;

  if (!xuid && xuid_reqiured) {
    xuid = kernel_state()
               ->xam_state()
               ->GetUserProfile(args->user_index.get())
               ->GetOnlineXUID();
  }

  uint8_t* filename_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(args->file_name_ptr);

  const std::u16string filename =
      xe::load_and_swap<std::u16string>(filename_ptr);
  const std::string filename_str = xe::to_utf8(filename);

  std::string backend_server_path_str;
  std::string symlink_path;

  std::string backend_domain_prefix =
      fmt::format("{}xstorage", XLiveAPI::GetApiAddress());

  std::string storage_type;

  switch (args->storage_location) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      X_STORAGE_FACILITY_INFO_GAME_CLIP game_clip_info_ptr = {};

      if (args->storage_location_info_ptr) {
        game_clip_info_ptr =
            *kernel_state_->memory()
                 ->TranslateVirtual<X_STORAGE_FACILITY_INFO_GAME_CLIP*>(
                     args->storage_location_info_ptr);

        XELOGI("{}: Leaderboard ID: {:08X}", __func__,
               game_clip_info_ptr.leaderboard_id.get());
      }

      // Validate ID via XLast
      assert_false(game_clip_info_ptr.leaderboard_id == 0);

      const std::string path = fmt::format(
          "clips/title/{:08X}/{:016X}/{:08X}/{}", kernel_state()->title_id(),
          xuid, game_clip_info_ptr.leaderboard_id.get(), filename_str);

      backend_server_path_str =
          fmt::format("{}/{}", backend_domain_prefix, path);

      symlink_path = fmt::format("{}{}", xstorage_symboliclink, path);
      symlink_path = utf8::fix_guest_path_separators(symlink_path);

      storage_type = "Game Clip";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      const std::string path = fmt::format(
          "title/{:08X}/{}", kernel_state()->title_id(), filename_str);

      backend_server_path_str =
          fmt::format("{}/{}", backend_domain_prefix, path);

      symlink_path = fmt::format("{}{}", xstorage_symboliclink, path);
      symlink_path = utf8::fix_guest_path_separators(symlink_path);

      storage_type = "Per Title";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      const std::string path =
          fmt::format("user/{:016X}/title/{:08X}/{}", xuid,
                      kernel_state()->title_id(), filename_str);

      backend_server_path_str =
          fmt::format("{}/{}", backend_domain_prefix, path);

      symlink_path = fmt::format("{}{}", xstorage_symboliclink, path);
      symlink_path = utf8::fix_guest_path_separators(symlink_path);

      storage_type = "Per User Title";
    } break;
    default:
      return X_ONLINE_E_STORAGE_INVALID_FACILITY;
  }

  const std::u16string backend_server_path =
      xe::to_utf16(backend_server_path_str);

  size_t server_path_length = backend_server_path.size();

  std::vector<char16_t> server_path_buf =
      std::vector<char16_t>(server_path_length);

  size_t size_bytes = server_path_length * sizeof(char16_t);

  memcpy(server_path_buf.data(), backend_server_path.c_str(), size_bytes);

  // Null-terminator
  server_path_buf.push_back(u'\0');

  if (args->server_path_ptr) {
    char16_t* server_path_ptr =
        kernel_state_->memory()->TranslateVirtual<char16_t*>(
            args->server_path_ptr);

    xe::string_util::copy_and_swap_truncating(
        server_path_ptr, server_path_buf.data(), X_ONLINE_MAX_PATHNAME_LENGTH);
  }

  uint32_t* server_path_length_ptr =
      kernel_state_->memory()->TranslateVirtual<uint32_t*>(
          args->server_path_length_ptr);

  *server_path_length_ptr =
      xe::byte_swap(static_cast<uint32_t>(server_path_buf.size()));

  X_STATUS result = X_E_SUCCESS;

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       args->storage_location != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    const std::string create_valid_path =
        std::filesystem::path(backend_server_path_str).parent_path().string();

    X_STORAGE_BUILD_SERVER_PATH_RESULT build_result =
        XLiveAPI::XStorageBuildServerPath(create_valid_path);

    if (build_result == X_STORAGE_BUILD_SERVER_PATH_RESULT::Created ||
        build_result == X_STORAGE_BUILD_SERVER_PATH_RESULT::Found) {
      result = X_E_SUCCESS;
    } else {
      result = X_E_FAIL;
    }
  }

  if (!route_backend || result) {
    symlink_path = std::filesystem::path(symlink_path).parent_path().string();

    // Check if entry exists
    vfs::Entry* entry =
        kernel_state()->file_system()->ResolvePath(symlink_path);

    if (!entry) {
      // Prepare path for splitting
      std::string starting_dir = symlink_path;
      std::replace(starting_dir.begin(), starting_dir.end(), ':',
                   kGuestPathSeparator);

      const auto path_parts = xe::utf8::split_path(starting_dir);

      if (path_parts.size() > 2) {
        starting_dir = path_parts[1];
      }

      // XSTORAGE entry
      vfs::Entry* xstorage_entry =
          kernel_state()->file_system()->ResolvePath(xstorage_symboliclink);

      if (xstorage_entry) {
        // Create root entry
        vfs::Entry* dir_entry = xstorage_entry->CreateEntry(
            starting_dir, xe::vfs::FileAttributeFlags::kFileAttributeDirectory);

        // Create child entries
        vfs::Entry* entries = kernel_state()->file_system()->CreatePath(
            symlink_path, xe::vfs::FileAttributeFlags::kFileAttributeDirectory);

        // Update entry
        entry = kernel_state()->file_system()->ResolvePath(symlink_path);

        if (entry) {
          result = X_E_SUCCESS;
          XELOGI("{}: Created Path: {}", __func__, symlink_path);
        } else {
          result = X_E_FAIL;
          XELOGW("{}: Failed to create path: {}", __func__, symlink_path);
        }
      }
    } else {
      result = X_E_SUCCESS;
      XELOGI("{}: Found Path: {}", __func__, symlink_path);
    }
  }

  XELOGI("{}: Filename: {}, Storage Type: {}", __func__, filename_str,
         storage_type);

  return result;
}

X_HRESULT XLiveBaseApp::XOnlineGetTaskProgress(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  X_GET_TASK_PROGRESS* task_progress =
      kernel_state()->memory()->TranslateVirtual<X_GET_TASK_PROGRESS*>(
          buffer_ptr);

  XAM_OVERLAPPED* overlapped_ptr =
      kernel_state()->memory()->TranslateVirtual<XAM_OVERLAPPED*>(
          task_progress->overlapped_ptr);

  uint32_t* percent_complete_ptr = nullptr;
  uint64_t* numerator_ptr = nullptr;
  uint64_t* denominator_ptr = nullptr;

  if (task_progress->percent_complete_ptr) {
    percent_complete_ptr =
        kernel_state()->memory()->TranslateVirtual<uint32_t*>(
            task_progress->percent_complete_ptr);
  }

  if (task_progress->numerator_ptr) {
    numerator_ptr = kernel_state()->memory()->TranslateVirtual<uint64_t*>(
        task_progress->numerator_ptr);
  }

  if (task_progress->denominator_ptr) {
    denominator_ptr = kernel_state()->memory()->TranslateVirtual<uint64_t*>(
        task_progress->denominator_ptr);
  }

  if (percent_complete_ptr) {
    *percent_complete_ptr = 100;
  }

  if (numerator_ptr) {
    *numerator_ptr = 100;
  }

  if (denominator_ptr) {
    *denominator_ptr = 100;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GetNextSequenceMessage(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XLIVEBASE_GET_SEQUENCE* data_ptr =
      kernel_state_->memory()->TranslateVirtual<XLIVEBASE_GET_SEQUENCE*>(
          buffer_ptr);

  data_ptr->seq_num = 0;

  // Size in bytes of args to deserialize
  data_ptr->msg_length = sizeof(BASE_MSG_HEADER);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserFindUsers(uint32_t buffer_ptr) {
  // 584113E8, 58410B5D

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XUserFindUsersUnmarshaller* unmarshaller =
      new XUserFindUsersUnmarshaller(buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller->Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  // Fixed 58410B5D
  unmarshaller->ZeroResults();

  FIND_USERS_RESPONSE* results_ptr =
      unmarshaller->Results<FIND_USERS_RESPONSE>();

  std::vector<FIND_USER_INFO> find_users = {};
  std::vector<FIND_USER_INFO> resolved_users = {};

  for (auto const& user : unmarshaller->Users()) {
    const uint64_t xuid = xe::byte_swap(user.xuid);

    const auto user_profile =
        kernel_state()->xam_state()->GetUserProfileLive(xuid);

    // Only lookup non-local users
    if (user_profile) {
      FIND_USER_INFO local_user = user;

      local_user.xuid = xuid;
      strcpy(local_user.gamertag, user_profile->name().c_str());

      resolved_users.push_back(local_user);
    } else if (xuid != 0) {
      find_users.push_back(user);
    }
  }

  if (!find_users.empty()) {
    auto resolved = XLiveAPI::GetFindUsers(find_users)->GetResolvedUsers();

    resolved_users.insert(resolved_users.end(), resolved.begin(),
                          resolved.end());
  }

  uint32_t results_size = sizeof(FIND_USERS_RESPONSE) +
                          (unmarshaller->NumUsers() * sizeof(FIND_USER_INFO));

  uint32_t users_address = kernel_state()->memory()->HostToGuestVirtual(
      std::to_address(results_ptr + 1));

  FIND_USER_INFO* user_results_ptr =
      kernel_state()->memory()->TranslateVirtual<FIND_USER_INFO*>(
          users_address);

  for (uint32_t i = 0; const auto& user : resolved_users) {
    memcpy(&user_results_ptr[i], &user, sizeof(FIND_USER_INFO));
    i++;
  }

  results_ptr->results_size = results_size;
  results_ptr->users_address = users_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XContentGetMarketplaceCounts(uint32_t buffer_ptr) {
  // 5454082B

  X_CONTENT_GET_MARKETPLACE_COUNTS* marketplace_counts_ptr =
      kernel_state()
          ->memory()
          ->TranslateVirtual<X_CONTENT_GET_MARKETPLACE_COUNTS*>(buffer_ptr);

  X_OFFERING_CONTENTAVAILABLE_RESULT* results_ptr =
      kernel_state()
          ->memory()
          ->TranslateVirtual<X_OFFERING_CONTENTAVAILABLE_RESULT*>(
              marketplace_counts_ptr->results_ptr);

  memset(results_ptr, 0, sizeof(X_OFFERING_CONTENTAVAILABLE_RESULT));

  const auto user_profile = kernel_state()->xam_state()->GetUserProfile(
      marketplace_counts_ptr->user_index);

  XELOGI("{}: Content Categories: {:08X}", __func__,
         marketplace_counts_ptr->content_categories.get());

  return X_E_SUCCESS;
}

std::string XLiveBaseApp::ConvertServerPathToXStorageSymlink(
    std::string server_path) {
  std::string symlink_path = server_path;

  std::string backend_domain_prefix =
      fmt::format("{}xstorage/", XLiveAPI::GetApiAddress());

  std::string location = symlink_path.substr(backend_domain_prefix.size());

  symlink_path = std::format("{}{}", xstorage_symboliclink, location);
  symlink_path = utf8::fix_guest_path_separators(symlink_path);

  return symlink_path;
}

std::string XLiveBaseApp::ConvertXStorageSymlinkToServerPath(
    std::string symlink_path_string) {
  std::string server_path = symlink_path_string;

  server_path = symlink_path_string.substr(xstorage_symboliclink.size());
  server_path = utf8::fix_path_separators(server_path, '/');

  std::string backend_domain_prefix =
      fmt::format("{}xstorage", XLiveAPI::GetApiAddress());

  server_path = std::format("{}/{}", backend_domain_prefix, server_path);

  return server_path;
}

X_STORAGE_FACILITY XLiveBaseApp::GetStorageFacilityTypeFromServerPath(
    std::string server_path) {
  std::string title_facility = R"(title(/|\\)[0-9a-fA-F]{8}(/|\\))";
  std::string clips_facility = R"(clips(/|\\))";
  std::string user_facility = R"(user(/|\\)[0-9a-fA-F]{16}(/|\\))";

  std::regex title_facility_regex(title_facility);
  std::regex clips_facility_regex(clips_facility);
  std::regex user_facility_regex(
      fmt::format("{}{}", user_facility, title_facility));

  X_STORAGE_FACILITY facility_type = X_STORAGE_FACILITY::FACILITY_INVALID;

  if (facility_type == X_STORAGE_FACILITY::FACILITY_INVALID) {
    if (std::regex_search(server_path, user_facility_regex)) {
      facility_type = X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE;
    }
  }

  if (facility_type == X_STORAGE_FACILITY::FACILITY_INVALID) {
    if (std::regex_search(server_path, clips_facility_regex)) {
      facility_type = X_STORAGE_FACILITY::FACILITY_GAME_CLIP;
    }
  }

  // Check per title last
  if (facility_type == X_STORAGE_FACILITY::FACILITY_INVALID) {
    if (std::regex_search(server_path, title_facility_regex)) {
      facility_type = X_STORAGE_FACILITY::FACILITY_PER_TITLE;
    }
  }

  return facility_type;
}

X_HRESULT XLiveBaseApp::XAccountGetPointsBalance(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Current Balance in sub menus:
  // All New Demos and Trailers
  // More Videos and Downloads
  // Address: 92433368

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  XACCOUNT_GET_POINTS_BALANCE_REQUEST* points_balance_request_ptr =
      unmarshaller
          ->DeserializeReinterpret<XACCOUNT_GET_POINTS_BALANCE_REQUEST>();

  if (!points_balance_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  X_GET_POINTS_BALANCE_RESPONSE* points_balance_results_ptr =
      unmarshaller->Results<X_GET_POINTS_BALANCE_RESPONSE>();

  points_balance_results_ptr->balance = 1000000000;
  points_balance_results_ptr->dmp_account_status =
      DMP_STATUS_TYPE::DMP_STATUS_ACTIVE;
  points_balance_results_ptr->response_flags =
      GET_POINTS_BALANCE_RESPONSE_FLAGS::ABOVE_LOW_BALANCE;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GetBannerList(uint32_t buffer_ptr) {
  // Called on startup of blades dashboard v1888 to v2858
  // Address: 92433DA8

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  GET_BANNER_LIST_REQUEST* banner_list_request_ptr =
      unmarshaller->DeserializeReinterpret<GET_BANNER_LIST_REQUEST>();

  if (!banner_list_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  GET_BANNER_LIST_RESPONSE* banner_list_results_ptr =
      unmarshaller->Results<GET_BANNER_LIST_RESPONSE>();

  banner_list_results_ptr->banner_count_total = 5;
  banner_list_results_ptr->banner_count = 0;
  banner_list_results_ptr->banner_list = 0;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GetBannerListHot(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace Featured Downloads.
  // Address: 92433BB0

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  GET_BANNER_LIST_REQUEST* banner_list_request =
      unmarshaller->DeserializeReinterpret<GET_BANNER_LIST_REQUEST>();

  if (!banner_list_request) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  GET_BANNER_LIST_RESPONSE* banner_list_results_ptr =
      unmarshaller->Results<GET_BANNER_LIST_RESPONSE>();

  banner_list_results_ptr->banner_count_total = 5;
  banner_list_results_ptr->banner_count = 0xFFFF;
  banner_list_results_ptr->banner_list = 0xFF;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::ContentEnumerate(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace sub menus:
  // All New Demos and Trailers
  // More Videos and Downloads
  // Address: 92433FA8

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  CONTENT_ENUMERATE_REQUEST* content_enumerate_request_ptr =
      unmarshaller->DeserializeReinterpret<CONTENT_ENUMERATE_REQUEST>();

  if (!content_enumerate_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  // Results Layout
  // CONTENT_ENUMERATE_RESPONSE[]
  // CONTENT_INFO[]
  // char16_t[] (Content Names)
  CONTENT_ENUMERATE_RESPONSE* content_enumerate_results_ptr =
      unmarshaller->Results<CONTENT_ENUMERATE_RESPONSE>();

  CONTENT_INFO* content_info_ptr =
      reinterpret_cast<CONTENT_INFO*>(content_enumerate_results_ptr + 1);

  uint32_t content_info_address = kernel_state_->memory()->HostToGuestVirtual(
      std::to_address(content_info_ptr));

  const std::vector<std::u16string> contents = {
      u"Content 1", u"Content 2", u"Content 3", u"Content 4", u"Content 5"};

  char16_t* content_names_ptr = reinterpret_cast<char16_t*>(
      content_info_ptr + content_enumerate_request_ptr->max_results);

  const uint32_t end_index = content_enumerate_request_ptr->starting_index +
                             content_enumerate_request_ptr->max_results;

  uint32_t total_content_count = static_cast<uint32_t>(contents.size());
  uint32_t returned_content_count = 0;

  // Paging
  for (uint32_t i = content_enumerate_request_ptr->starting_index;
       i < end_index; i++) {
    if (i >= contents.size()) {
      break;
    }

    const std::u16string content_name = contents[i];

    const uint16_t size = static_cast<uint16_t>(content_name.size() + 1);

    xe::string_util::copy_and_swap_truncating(content_names_ptr,
                                              content_name.c_str(), size);

    content_info_ptr->offer_name = kernel_state_->memory()->HostToGuestVirtual(
        std::to_address(content_names_ptr));
    content_info_ptr->offer_name_length = size;

    content_info_ptr += 1;
    content_names_ptr += size;

    returned_content_count++;
  }

  content_enumerate_results_ptr->content_total = total_content_count;
  content_enumerate_results_ptr->content_returned = returned_content_count;
  content_enumerate_results_ptr->enumerate_content_info_ptr =
      content_info_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GenresEnumerate(uint32_t buffer_ptr) {
  // Fixes accessing marketplace Game Downloads->All Games->Xbox Live Arcade
  // sub menu.
  // Address: 92434218

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  GENRES_ENUMERATE_REQUEST* genre_enumerate_request_ptr =
      unmarshaller->DeserializeReinterpret<GENRES_ENUMERATE_REQUEST>();

  if (!genre_enumerate_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  // Add max string length?
  const uint32_t total_genre_info_size =
      sizeof(GENRE_INFO) * genre_enumerate_request_ptr->max_count;

  assert_true(unmarshaller->GetAsyncTask()->GetXLiveAsyncTask()->results_size >
              total_genre_info_size);

  // Results Layout
  // GENRES_ENUMERATE_RESPONSE[]
  // GENRE_INFO[]
  // char16_t[] (Genres Names)
  GENRES_ENUMERATE_RESPONSE* genre_enumerate_results_ptr =
      unmarshaller->Results<GENRES_ENUMERATE_RESPONSE>();

  GENRE_INFO* genre_info_ptr =
      reinterpret_cast<GENRE_INFO*>(genre_enumerate_results_ptr + 1);

  uint32_t genre_info_address = kernel_state_->memory()->HostToGuestVirtual(
      std::to_address(genre_info_ptr));

  const std::vector<std::u16string> genres = {
      u"Action",  u"Adventure", u"Simulation", u"Strategy",
      u"Shooter", u"Sports",    u"Puzzle",     u"RPG",
  };

  char16_t* genre_names_ptr = reinterpret_cast<char16_t*>(
      genre_info_ptr + genre_enumerate_request_ptr->max_count);

  const uint32_t end_index = genre_enumerate_request_ptr->start_index +
                             genre_enumerate_request_ptr->max_count;

  uint32_t total_genres_count = static_cast<uint32_t>(genres.size());
  uint32_t returned_genres_count = 0;

  // Paging
  for (uint32_t i = genre_enumerate_request_ptr->start_index; i < end_index;
       i++) {
    if (i >= genres.size()) {
      break;
    }

    const std::u16string genre_name = genres[i];

    const uint16_t size = static_cast<uint16_t>(genre_name.size() + 1);

    xe::string_util::copy_and_swap_truncating(genre_names_ptr,
                                              genre_name.c_str(), size);

    genre_info_ptr->localized_genre_name =
        kernel_state_->memory()->HostToGuestVirtual(
            std::to_address(genre_names_ptr));
    genre_info_ptr->localized_genre_length = size;

    genre_info_ptr += 1;
    genre_names_ptr += size;

    returned_genres_count++;
  }

  genre_enumerate_results_ptr->geners_total = total_genres_count;
  genre_enumerate_results_ptr->geners_returned = returned_genres_count;
  genre_enumerate_results_ptr->enumerate_genre_info_ptr = genre_info_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::EnumerateTitlesByFilter(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace Game Downloads.
  // Address: 92434468

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  ENUMERATE_TITLES_BY_FILTER* enumerate_titles_request_ptr =
      unmarshaller->DeserializeReinterpret<ENUMERATE_TITLES_BY_FILTER>();

  if (!enumerate_titles_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  const uint32_t request_type = enumerate_titles_request_ptr->request_flags;

  std::string enumerate_flags = "";

  if (request_type &
      static_cast<uint32_t>(ENUMERATE_TITLES_BY_FILTER_FLAGS::Played)) {
    enumerate_flags.append("Played, ");
  }

  if (request_type &
      static_cast<uint32_t>(ENUMERATE_TITLES_BY_FILTER_FLAGS::New)) {
    enumerate_flags.append("New, ");
  }

  if (request_type == 0) {
    enumerate_flags.append("Alphabetically Sort, ");
  }

  XELOGI("{}:: Requesting: {}", __func__, enumerate_flags);

  // Results Layout
  // ENUMERATE_TITLES_BY_FILTER_RESPONSE[]
  // ENUMERATE_TITLES_INFO[]
  // char16_t[] (Title Names)
  ENUMERATE_TITLES_BY_FILTER_RESPONSE* enumerate_titles_results_ptr =
      unmarshaller->Results<ENUMERATE_TITLES_BY_FILTER_RESPONSE>();

  ENUMERATE_TITLES_INFO* title_info_ptr =
      reinterpret_cast<ENUMERATE_TITLES_INFO*>(enumerate_titles_results_ptr +
                                               1);

  // Add max string length?
  const uint32_t enumerate_titles_info_size =
      sizeof(ENUMERATE_TITLES_INFO) * enumerate_titles_request_ptr->max_count;

  assert_true(unmarshaller->GetAsyncTask()->GetXLiveAsyncTask()->results_size >
              enumerate_titles_info_size);

  uint32_t title_info_address = kernel_state_->memory()->HostToGuestVirtual(
      std::to_address(title_info_ptr));

  uint32_t total_titles_count = 0;
  uint32_t returned_titles_count = 0;

  if (enumerate_titles_request_ptr->request_flags &
      static_cast<uint16_t>(ENUMERATE_TITLES_BY_FILTER_FLAGS::Played)) {
    const std::vector<TitleInfo> played_titles =
        kernel_state()->xam_state()->user_tracker()->GetPlayedTitles(
            enumerate_titles_request_ptr->xuid);

    total_titles_count = static_cast<uint32_t>(played_titles.size());

    char16_t* titles_names_ptr = reinterpret_cast<char16_t*>(
        title_info_ptr + enumerate_titles_request_ptr->max_count);

    const uint32_t end_index = enumerate_titles_request_ptr->start_index +
                               enumerate_titles_request_ptr->max_count;

    // Paging
    for (uint32_t i = enumerate_titles_request_ptr->start_index; i < end_index;
         i++) {
      if (i >= played_titles.size()) {
        break;
      }

      const TitleInfo played_title = played_titles[i];

      const uint16_t size =
          static_cast<uint16_t>(played_title.title_name.size() + 1);

      xe::string_util::copy_and_swap_truncating(
          titles_names_ptr, played_title.title_name.c_str(), size);

      title_info_ptr->title_id = played_title.id;
      title_info_ptr->played = true;
      title_info_ptr->title_name = kernel_state_->memory()->HostToGuestVirtual(
          std::to_address(titles_names_ptr));
      title_info_ptr->title_name_length = size;

      title_info_ptr += 1;
      titles_names_ptr += size;

      returned_titles_count++;
    }
  }

  enumerate_titles_results_ptr->total_titles_count = total_titles_count;
  enumerate_titles_results_ptr->titles_returned = returned_titles_count;
  enumerate_titles_results_ptr->enumerate_title_info_ptr = title_info_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::SubscriptionEnumerate(uint32_t buffer_ptr) {
  // Fixes accessing marketplace Memberships.
  // Address: 924346C0

  GenericUnmarshaller* unmarshaller = new GenericUnmarshaller(buffer_ptr);

  SUBSCRIPTION_ENUMERATE_REQUEST* subscription_enumerate_ptr =
      unmarshaller->DeserializeReinterpret<SUBSCRIPTION_ENUMERATE_REQUEST>();

  if (!subscription_enumerate_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller->ZeroResults();

  const uint32_t request_type = subscription_enumerate_ptr->request_flags;

  std::string enumerate_flags = "";

  if (request_type & static_cast<uint32_t>(SUBSCRIPTION_ENUMERATE_FLAGS::New)) {
    enumerate_flags.append("New, ");
  }

  if (request_type &
      static_cast<uint32_t>(SUBSCRIPTION_ENUMERATE_FLAGS::Renewals)) {
    enumerate_flags.append("Renewals, ");
  }

  if (request_type &
      static_cast<uint32_t>(SUBSCRIPTION_ENUMERATE_FLAGS::Current)) {
    enumerate_flags.append("Current, ");
  }

  if (request_type &
      static_cast<uint32_t>(SUBSCRIPTION_ENUMERATE_FLAGS::Expired)) {
    enumerate_flags.append("Expired, ");
  }

  if (request_type &
      static_cast<uint32_t>(SUBSCRIPTION_ENUMERATE_FLAGS::Suspended)) {
    enumerate_flags.append("Suspended, ");
  }

  XELOGI("{}:: Requesting: {}", __func__, enumerate_flags);

  // Add max string length?
  const uint32_t subscription_info_size =
      sizeof(SUBSCRIPTION_INFO) * subscription_enumerate_ptr->max_results;

  assert_true(unmarshaller->GetAsyncTask()->GetXLiveAsyncTask()->results_size >
              subscription_info_size);

  SUBSCRIPTION_ENUMERATE_RESPONSE* subscription_enumerate_results_ptr =
      unmarshaller->Results<SUBSCRIPTION_ENUMERATE_RESPONSE>();

  SUBSCRIPTION_INFO* subscriptions_info_ptr =
      reinterpret_cast<SUBSCRIPTION_INFO*>(subscription_enumerate_results_ptr +
                                           1);

  uint32_t subscriptions_info_address =
      kernel_state_->memory()->HostToGuestVirtual(
          std::to_address(subscriptions_info_ptr));

  const std::vector<std::u16string> memberships = {
      u"Premium Xenia Canary",
      u"Premium Lite Xenia Canary",
  };

  char16_t* offer_names_ptr = reinterpret_cast<char16_t*>(
      subscriptions_info_ptr + subscription_enumerate_ptr->max_results);

  uint32_t total_membership_count = static_cast<uint32_t>(memberships.size());
  uint32_t returned_membership_count = 0;

  const uint32_t end_index = subscription_enumerate_ptr->starting_index +
                             subscription_enumerate_ptr->max_results;

  // Paging
  for (uint32_t i = subscription_enumerate_ptr->starting_index; i < end_index;
       i++) {
    if (i >= memberships.size()) {
      break;
    }

    const std::u16string membership = memberships[i];

    const uint16_t size = static_cast<uint16_t>(membership.size() + 1);

    xe::string_util::copy_and_swap_truncating(
        offer_names_ptr, membership.c_str(), membership.size() + 1);

    subscriptions_info_ptr->offer_id = i;
    subscriptions_info_ptr->offer_name =
        kernel_state_->memory()->HostToGuestVirtual(
            std::to_address(offer_names_ptr));
    subscriptions_info_ptr->offer_name_length = size;

    subscriptions_info_ptr += 1;
    offer_names_ptr += size;

    returned_membership_count++;
  }

  subscription_enumerate_results_ptr->offers_total = total_membership_count;
  subscription_enumerate_results_ptr->offers_returned =
      returned_membership_count;
  subscription_enumerate_results_ptr->subscription_info_ptr =
      subscriptions_info_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUpdateAccessTimes(uint32_t buffer_ptr) {
  // Blades Dashboard v1888
  // More Videos and Downloads

  XLIVEBASE_UPDATE_ACCESS_TIMES* data_ptr =
      memory_->TranslateVirtual<XLIVEBASE_UPDATE_ACCESS_TIMES*>(buffer_ptr);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XMessageEnumerate(uint32_t buffer_ptr,
                                          uint32_t buffer_length) {
  // Blades Dashboard v1888

  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 3) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  XLIVEBASE_MESSAGES_ENUMERATOR* entry =
      memory->TranslateVirtual<XLIVEBASE_MESSAGES_ENUMERATOR*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(memory->TranslateVirtual(
      static_cast<uint32_t>(entry->xuid.argument_value_ptr)));
  auto messages_count = memory->TranslateVirtual<xe::be<uint32_t>*>(
      static_cast<uint32_t>(entry->messages_count_ptr.argument_value_ptr));
  X_MESSAGE_SUMMARY* message_summaries =
      memory->TranslateVirtual<X_MESSAGE_SUMMARY*>(static_cast<uint32_t>(
          entry->message_summaries_ptr.argument_value_ptr));

  *messages_count = 0;

  for (uint32_t i = 0; i < *messages_count; i++) {
    X_MESSAGE_SUMMARY* summary = &message_summaries[i];
    std::memset(summary, 0, sizeof(X_MESSAGE_SUMMARY));
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XPresenceGetState(uint32_t buffer_ptr,
                                          uint32_t buffer_length) {
  // Blades Dashboard v1888

  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask* async_task = new XLivebaseAsyncTask(buffer_ptr);

  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 3) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  const XLIVEBASE_PRESENCE_GET_STATE* entry =
      memory->TranslateVirtual<XLIVEBASE_PRESENCE_GET_STATE*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(memory->TranslateVirtual(
      static_cast<uint32_t>(entry->xuid.argument_value_ptr)));
  auto state_flags_ptr = memory->TranslateVirtual<xe::be<uint32_t>*>(
      static_cast<uint32_t>(entry->state_flags_ptr.argument_value_ptr));
  auto session_id_ptr = memory->TranslateVirtual<XNKID*>(
      static_cast<uint32_t>(entry->session_id_ptr.argument_value_ptr));

  *state_flags_ptr = 0;
  *session_id_ptr = {};

  return X_E_SUCCESS;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
