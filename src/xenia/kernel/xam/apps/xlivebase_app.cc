/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <span>

#include "xenia/kernel/xam/apps/xlivebase_app.h"
#include "xenia/kernel/xenumerator.h"

#include "xenia/base/logging.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xnet.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <winsock2.h>                    // NOLINT(build/include_order)
#elif XE_PLATFORM_LINUX
#include <netinet/in.h>
#endif

DEFINE_bool(stub_xlivebase, false,
            "Return success for all unimplemented XLiveBase calls.", "Live");

DECLARE_bool(xstorage_backend);

DECLARE_bool(xstorage_user_data_backend);

DECLARE_int32(user_country);

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

XLiveBaseApp::XLiveBaseApp(KernelState* kernel_state)
    : App(kernel_state, 0xFC) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

X_HRESULT XLiveBaseApp::DispatchMessageSync(uint32_t message,
                                            uint32_t buffer_ptr,
                                            uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x00050002: {
      // Current session must have PRESENCE flag.
      XELOGD("XInviteSend({:08X}, {:08X})", buffer_ptr, buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x00058003: {
      // Called on startup of dashboard (netplay build)
      XELOGD("XLiveBaseLogonGetHR({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED;
    }
    case 0x0005008C: {
      XELOGD("XLiveBaseUnk5008C({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn5008C(buffer_ptr);
    }
    case 0x00050094: {
      // Called on startup of blades dashboard v4532 to v4552
      XELOGD("XLiveBaseUnk50094({:08x}, {:08x}) unimplemented", buffer_ptr,
             buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x00050008: {
      // Required to be successful for 534507D4
      XELOGD("XStorageDelete({:08x}, {:08x})", buffer_ptr, buffer_length);
      return XStorageDelete(buffer_ptr);
    }
    case 0x00050009: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);
    }
    case 0x0005000A: {
      // 4D5307D3, 415607F7, 584108F0
      XELOGD("XStorageEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XStorageEnumerate(buffer_ptr);
    }
    case 0x0005000B: {
      // Fixes Xbox Live error for 43430821
      XELOGD("XStorageUploadFromMemory({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageUploadFromMemory(buffer_ptr);
    }
    case 0x0005000C: {
      XELOGD("XStringVerify({:08X} {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr);
    }
    case 0x0005000D: {
      // Fixes hang when leaving session for 545107D5
      // 415607D2 says this is XStringVerify
      XELOGD("XStringVerify({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr);
    }
    case 0x0005000E: {
      XELOGD("XUserFindUsers({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserFindUsers(buffer_ptr);
    }
    case 0x0005000F: {
      XELOGD("_XAccountGetUserInfo({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x00050010: {
      XELOGD("XAccountGetUserInfo({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x00050036: {
      // 534507D4
      XELOGD("XOnlineQuerySearch({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XOnlineQuerySearch(buffer_ptr);
    }
    case 0x00050038: {
      // 4D5307D3, 4D5307D1
      XELOGD("XOnlineQuerySearch({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return XOnlineQuerySearch(buffer_ptr);
    }
    case 0x00050077: {
      XELOGD("XAccountGetPointsBalance({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return XAccountGetPointsBalance(buffer_ptr);
    }
    case 0x00050079: {
      // Fixes Xbox Live error for 454107DB
      XELOGD("XLiveBaseUnk50079({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return GenericMarshalled(buffer_ptr);
    }
    case 0x0005008B: {
      XELOGD("XLiveBaseUnk5008B({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn5008B(buffer_ptr);
    }
    case 0x0005008F: {
      XELOGD("XLiveBaseUnk5008F({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn5008F(buffer_ptr);
    }
    case 0x00050090: {
      XELOGD("XLiveBaseUnk50090({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn50090(buffer_ptr);
    }
    case 0x00050091: {
      XELOGD("XLiveBaseUnk50091({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn50091(buffer_ptr);
    }
    case 0x00050097: {
      XELOGD("XLiveBaseUnk50097({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return Unkn50097(buffer_ptr);
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
      assert_true(!buffer_length || buffer_length == 0x10);
      XELOGD("XContentGetMarketplaceCounts({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XContentGetMarketplaceCounts(buffer_ptr);
    }
    case 0x0005800A: {
      assert_true(!buffer_length || buffer_length == 12);
      XELOGD("XUpdateAccessTimes({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return XUpdateAccessTimes(buffer_ptr);
    }
    case 0x0005800C: {
      // 464F0800
      XELOGD("XUserMuteListAdd({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserMuteListAdd(buffer_ptr);
    }
    case 0x0005800D: {
      // 464F0800
      XELOGD("XUserMuteListRemove({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XUserMuteListRemove(buffer_ptr);
    }
    case 0x0005800E: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XUserMuteListQuery({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058017: {
      XELOGD("XUserFindUsersUnkn58017({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XUserFindUsersUnkn58017(buffer_ptr);
    }
    case 0x00058019: {
      // 54510846
      XELOGD("XPresenceCreateEnumerator({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceCreateEnumerator(buffer_length);
    }
    case 0x0005801C: {
      XELOGD("XPresenceGetState({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return XPresenceGetState(buffer_length);
    }
    case 0x0005801E: {
      // 54510846
      XELOGD("XPresenceSubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceSubscribe(buffer_length);
    }
    case 0x0005801F: {
      // 545107D1
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceUnsubscribe(buffer_length);
    }
    case 0x00058020: {
      // 0x00058004 is called right before this.
      // buffer_length seems to be the same ptr sent to 0x00058004.
      XELOGD("XFriendsCreateEnumerator({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XFriendsCreateEnumerator(buffer_length);
    }
    case 0x00058023: {
      // 584107D7
      // 5841091C expects xuid_invitee
      XELOGD(
          "CXLiveMessaging::XMessageGameInviteGetAcceptedInfo({:08X}, {:08X})",
          buffer_ptr, buffer_length);
      return XInviteGetAcceptedInfo(buffer_length);
    }
    case 0x00058024: {
      XELOGD("XMessageEnumerate({:08X}, {:08X}) Stubbed", buffer_ptr,
             buffer_length);
      return XMessageEnumerate(buffer_length);
    }
    case 0x00058032: {
      XELOGD("XOnlineGetTaskProgress({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XOnlineGetTaskProgress(buffer_ptr);
    }
    case 0x00058035: {
      // Fixes Xbox Live error for 513107D9
      // Required for 534507D4
      XELOGD("XStorageBuildServerPath({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageBuildServerPath(buffer_ptr);
    }
    case 0x00058037: {
      // Used in older games such as Crackdown, FM2, Saints Row 1
      XELOGD("XPresenceInitializeLegacy({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceInitialize(buffer_length);
    }
    case 0x00058044: {
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceUnsubscribe(buffer_length);
    }
    case 0x00058046: {
      // Used in newer games such as Forza 4, MW3, FH2
      //
      // Required to be successful for 4D530910 to detect signed-in profile
      XELOGD("XPresenceInitialize({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XPresenceInitialize(buffer_length);
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

X_HRESULT XLiveBaseApp::XPresenceInitialize(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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
// will be retuned in XPresenceCreateEnumerator.
X_HRESULT XLiveBaseApp::XPresenceSubscribe(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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

// Presence information for peers will not longer be retuned in
// XPresenceCreateEnumerator unless they're friends.
X_HRESULT XLiveBaseApp::XPresenceUnsubscribe(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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
X_HRESULT XLiveBaseApp::XPresenceCreateEnumerator(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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

X_HRESULT XLiveBaseApp::XOnlineQuerySearch(uint32_t buffer_ptr) {
  // Usually called after success returned from GetServiceInfo.

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XOnlineQuerySearch_Marshalled_Data* data_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<XOnlineQuerySearch_Marshalled_Data*>(buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  XOnlineQuerySearch_Args* args_online_query_search =
      kernel_state_->memory()->TranslateVirtual<XOnlineQuerySearch_Args*>(
          internal_data_ptr->start_args_ptr);

  QUERY_SEARCH_RESULT* query_search_results_ptr =
      kernel_state_->memory()->TranslateVirtual<QUERY_SEARCH_RESULT*>(
          internal_data_ptr->results_ptr);

  memset(query_search_results_ptr, 0, internal_data_ptr->results_size);

  XELOGI("{}: Title ID: {:08X}, Procedure Index: {}, Dataset ID: {:08X}",
         __func__, args_online_query_search->title_id,
         args_online_query_search->proc_index,
         args_online_query_search->dataset_id);

  return X_E_SUCCESS;
}

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

  memset(service_info_ptr, 0, sizeof(X_ONLINE_SERVICE_INFO));

  X_ONLINE_SERVICE_INFO service_info = {};

  HTTP_STATUS_CODE status =
      XLiveAPI::GetServiceInfoById(serviceid, &service_info);

  if (status != HTTP_STATUS_CODE::HTTP_OK) {
    return X_ONLINE_E_LOGON_SERVICE_NOT_REQUESTED;
  }

  memcpy(service_info_ptr, &service_info, sizeof(X_ONLINE_SERVICE_INFO));

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XFriendsCreateEnumerator(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_ptr);

  if (args_list->argument_count != 5) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  X_CREATE_FRIENDS_ENUMERATOR* friends_enumerator =
      memory->TranslateVirtual<X_CREATE_FRIENDS_ENUMERATOR*>(buffer_ptr);

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

X_HRESULT XLiveBaseApp::XInviteGetAcceptedInfo(uint32_t buffer_length) {
  Memory* memory = kernel_state_->memory();

  const X_ARGUMENT_LIST* args_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  if (args_list->argument_count != 2) {
    assert_always(fmt::format("{} - Invalid argument count!", __func__));
  }

  X_INVITE_GET_ACCEPTED_INFO* AcceptedInfo =
      memory->TranslateVirtual<X_INVITE_GET_ACCEPTED_INFO*>(buffer_length);

  const uint32_t user_index =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(AcceptedInfo->user_index.argument_value_ptr)));

  X_INVITE_INFO* invite_info =
      reinterpret_cast<X_INVITE_INFO*>(memory_->TranslateVirtual(
          static_cast<uint32_t>(AcceptedInfo->invite_info.argument_value_ptr)));

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
    return X_E_FAIL;
  }

  const auto session = XLiveAPI::XSessionGet(session_id);

  if (!session->SessionID_UInt()) {
    return X_E_FAIL;
  }

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
  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = nullptr;

  uint8_t* args_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->start_args_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  std::fill_n(results_ptr, internal_data_ptr->results_size, 0);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserMuteListAdd(uint32_t buffer_ptr) {
  X_MUTE_SET_STATE* remote_player_ptr =
      memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

  if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserMuteListRemove(uint32_t buffer_ptr) {
  X_MUTE_SET_STATE* remote_player_ptr =
      memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

  if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XAccountGetUserInfo(uint32_t buffer_ptr) {
  // Requires XEX_SYSTEM_ACCESS_PII privilege
  // 41560855 (TU 7+), 4D530AA5

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XAccountGetUserInfo_Marshalled_Data* data_ptr =
      kernel_state()
          ->memory()
          ->TranslateVirtual<XAccountGetUserInfo_Marshalled_Data*>(buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  X_GET_USER_INFO_RESPONSE* user_info_response_ptr =
      kernel_state_->memory()->TranslateVirtual<X_GET_USER_INFO_RESPONSE*>(
          internal_data_ptr->results_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  if (internal_data_ptr->results_size < XAccountGetUserInfoResponseSize()) {
    return X_ONLINE_E_ACCOUNTS_USER_GET_ACCOUNT_INFO_ERROR;
  }

  memset(user_info_response_ptr, 0, internal_data_ptr->results_size);

  uint32_t offset = 0;

  xe::be<uint64_t> xuid = *reinterpret_cast<uint64_t*>(args_stream_ptr);

  offset += sizeof(uint64_t);

  xe::be<uint64_t> machine_id =
      *reinterpret_cast<uint64_t*>(args_stream_ptr + offset);

  offset += sizeof(uint64_t);

  xe::be<uint32_t> title_id =
      *reinterpret_cast<uint32_t*>(args_stream_ptr + offset);

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

  XStorageEnumerate_Marshalled_Data* data_ptr =
      kernel_state()
          ->memory()
          ->TranslateVirtual<XStorageEnumerate_Marshalled_Data*>(buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  X_STORAGE_ENUMERATE_RESULTS* results_ptr =
      kernel_state_->memory()->TranslateVirtual<X_STORAGE_ENUMERATE_RESULTS*>(
          internal_data_ptr->results_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  // Fixed 415607F7 from crashing.
  memset(results_ptr, 0, internal_data_ptr->results_size);

  uint32_t offset = 0;

  xe::be<uint32_t> user_index = 0;
  memcpy(&user_index, args_stream_ptr, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> server_path_len = 0;
  memcpy(&server_path_len, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  char16_t* arg_server_path_ptr =
      reinterpret_cast<char16_t*>(args_stream_ptr + offset);

  uint32_t server_path_size = server_path_len * sizeof(char16_t);

  offset += server_path_size;

  uint8_t* arg_starting_index_ptr = args_stream_ptr + offset;
  uint32_t* starting_index_ptr =
      reinterpret_cast<uint32_t*>(args_stream_ptr + offset);

  offset += sizeof(uint32_t);

  uint32_t* arg_max_results_to_return_ptr =
      reinterpret_cast<uint32_t*>(args_stream_ptr + offset);

  // Exclude null-terminator
  server_path_len -= 1;

  std::u16string server_path;
  server_path.resize(server_path_len);

  xe::copy_and_swap(server_path.data(), arg_server_path_ptr, server_path_len);

  uint32_t starting_index = xe::load_and_swap<uint32_t>(arg_starting_index_ptr);
  uint32_t max_results_to_return =
      xe::load_and_swap<uint32_t>(arg_max_results_to_return_ptr);

  if (server_path_len > X_ONLINE_MAX_PATHNAME_LENGTH) {
    return X_E_FAIL;
  }

  auto user_profle = kernel_state()->xam_state()->GetUserProfile(user_index);

  const std::string enumeration_path = xe::to_utf8(server_path);

  const uint32_t available_to_return_items =
      static_cast<uint32_t>(std::floor<uint32_t>(static_cast<uint32_t>(
          (internal_data_ptr->results_size -
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

  if (server_path.empty()) {
    XELOGI("{}: Empty Server Path", __func__);
    return X_E_INVALIDARG;
  }

  if (max_results_to_return > X_STORAGE_MAX_RESULTS_TO_RETURN) {
    return X_E_INVALIDARG;
  }

  if (starting_index >= X_STORAGE_MAX_RESULTS_TO_RETURN) {
    return X_E_INVALIDARG;
  }

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
    uint32_t max_items = max_results_to_return < available_to_return_items
                             ? max_results_to_return
                             : available_to_return_items;

    const auto enumeration_result =
        XLiveAPI::XStorageEnumerate(enumeration_path, max_items);

    const auto& enumerated_files = enumeration_result.first;
    enumerated_backend = enumeration_result.second;

    for (uint32_t item_index = starting_index;
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

    for (uint32_t item_index = starting_index; const auto entry : entries) {
      std::string symlink_item_path =
          std::format("{}\\{}", item_parent, entry->name());

      // Path must use /
      // Keep server return consitant with XStorageBuildServerPath
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
      results_ptr->num_items_returned.get(), starting_index,
      max_results_to_return, final_enumeration_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStringVerify(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStringVerify_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<XStringVerify_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  STRING_VERIFY_RESPONSE* responses_ptr =
      kernel_state_->memory()->TranslateVirtual<STRING_VERIFY_RESPONSE*>(
          internal_data_ptr->results_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  memset(responses_ptr, 0, internal_data_ptr->results_size);

  uint16_t* locale_size_ptr =
      kernel_state_->memory()->TranslateVirtual<uint16_t*>(
          data_ptr->locale_size_ptr);
  uint16_t* num_strings_ptr =
      kernel_state_->memory()->TranslateVirtual<uint16_t*>(
          data_ptr->num_strings_ptr);
  uint16_t* last_entry_ptr =
      kernel_state_->memory()->TranslateVirtual<uint16_t*>(
          data_ptr->last_entry_ptr);

  uint16_t locale_size = *locale_size_ptr;
  uint16_t num_strings = *num_strings_ptr;

  if (locale_size > X_ONLINE_MAX_XSTRING_VERIFY_LOCALE) {
    return X_E_INVALIDARG;
  }

  if (num_strings > X_ONLINE_MAX_XSTRING_VERIFY_STRING_DATA) {
    return X_E_INVALIDARG;
  }

  xe::be<uint32_t> title_id = *reinterpret_cast<uint32_t*>(args_stream_ptr);

  xe::be<uint32_t> flags =
      *reinterpret_cast<uint32_t*>(args_stream_ptr + sizeof(uint32_t));

  char* locale_string_ptr = kernel_state_->memory()->TranslateVirtual<char*>(
      data_ptr->num_strings_ptr + sizeof(uint16_t));

  std::string locale = std::string(locale_string_ptr, locale_size);

  char* string_data = locale_string_ptr + locale_size;

  std::vector<std::string> strings_to_verify;

  uint32_t string_data_offset = 0;

  for (uint32_t i = 0; i < num_strings; i++) {
    uint16_t size = 0;
    memcpy(&size, string_data + string_data_offset, sizeof(uint16_t));

    string_data_offset += sizeof(uint16_t);

    // Unicode is represented as UTF-8 array
    char* string_data_ptr = string_data + string_data_offset;

    std::string input_string = std::string(string_data_ptr, size);

    string_data_offset += size;

    strings_to_verify.push_back(input_string);

    XELOGI("{}: {}", __func__, input_string);
  }

  uint32_t response_result_address =
      kernel_state_->memory()->HostToGuestVirtual(
          std::to_address(responses_ptr + 1));

  HRESULT* response_results_ptr =
      kernel_state_->memory()->TranslateVirtual<HRESULT*>(
          response_result_address);

  for (uint32_t i = 0; i < num_strings; i++) {
    response_results_ptr[i] = X_E_SUCCESS;
  }

  responses_ptr->num_strings = num_strings;
  responses_ptr->string_result_ptr = response_result_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageDelete(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageDelete_Marshalled_Data* data_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<XStorageDelete_Marshalled_Data*>(buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t offset = 0;

  xe::be<uint32_t> user_index = 0;
  memcpy(&user_index, args_stream_ptr, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> server_path_len = 0;
  memcpy(&server_path_len, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  char16_t* arg_server_path_ptr =
      reinterpret_cast<char16_t*>(args_stream_ptr + offset);

  uint32_t server_path_size = server_path_len * sizeof(char16_t);

  offset += server_path_size;

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  // Exclude null-terminator
  server_path_len -= 1;

  std::u16string server_path;
  server_path.resize(server_path_len, 0);

  xe::copy_and_swap(server_path.data(), arg_server_path_ptr,
                    static_cast<uint32_t>(server_path_len));

  std::string item_path = xe::to_utf8(server_path);

  X_STATUS result = X_E_FAIL;

  if (item_path.empty()) {
    XELOGI("{}: Empty Server Path", __func__);
    return X_ONLINE_E_STORAGE_INVALID_STORAGE_PATH;
  }

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

  XStorageDownloadToMemory_Marshalled_Data* data_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<XStorageDownloadToMemory_Marshalled_Data*>(
              buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS* download_results_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS*>(
              internal_data_ptr->results_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  // Fixed 415607DD & 41560834
  memset(download_results_ptr, 0, internal_data_ptr->results_size);

  uint32_t offset = 0;

  xe::be<uint32_t> user_index = 0;
  memcpy(&user_index, args_stream_ptr, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> server_path_len = 0;
  memcpy(&server_path_len, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  char16_t* arg_server_path_ptr =
      reinterpret_cast<char16_t*>(args_stream_ptr + offset);

  uint32_t server_path_size = server_path_len * sizeof(char16_t);

  offset += server_path_size;

  xe::be<uint32_t> buffer_size = 0;
  memcpy(&buffer_size, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> download_buffer_address = 0;
  memcpy(&download_buffer_address, args_stream_ptr + offset, sizeof(uint32_t));

  if (!download_buffer_address) {
    return X_E_INVALIDARG;
  }

  uint8_t* download_buffer_ptr =
      kernel_state()->memory()->TranslateVirtual<uint8_t*>(
          download_buffer_address);

  // 41560845 - Access Violation
  // std::fill_n(download_buffer_ptr, buffer_size, 0);

  std::span<uint8_t> download_buffer =
      std::span<uint8_t>(download_buffer_ptr, buffer_size);

  const auto user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  uint64_t xuid_owner = 0;

  if (user_profile) {
    xuid_owner = user_profile->GetOnlineXUID();
  }

  // Exclude null-terminator
  server_path_len -= 1;

  std::u16string server_path;
  server_path.resize(server_path_len, 0);

  xe::copy_and_swap(server_path.data(), arg_server_path_ptr,
                    static_cast<uint32_t>(server_path_len));

  std::string item_to_download = xe::to_utf8(server_path);

  X_STATUS result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;

  if (server_path.empty()) {
    return X_ONLINE_E_STORAGE_INVALID_STORAGE_PATH;
  }

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

      // Possible solution: Use custom header or encode binary in base64 with
      // metadata
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
        if (bytes_read > buffer_size) {
          XELOGI("{}: Provided file size {}b is larger than expected {}b",
                 __func__, bytes_read, buffer_size.get());
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
         __func__, download_results_ptr->bytes_total.get(), buffer_size.get(),
         item_to_download);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageUploadFromMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageUploadFromMemory_Marshalled_Data* data_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<XStorageUploadFromMemory_Marshalled_Data*>(
              buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t offset = 0;

  xe::be<uint32_t> user_index = 0;
  memcpy(&user_index, args_stream_ptr, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> server_path_len = 0;
  memcpy(&server_path_len, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  char16_t* arg_server_path_ptr =
      reinterpret_cast<char16_t*>(args_stream_ptr + offset);

  uint32_t server_path_size = server_path_len * sizeof(char16_t);

  offset += server_path_size;

  xe::be<uint32_t> buffer_size = 0;
  memcpy(&buffer_size, args_stream_ptr + offset, sizeof(uint32_t));

  offset += sizeof(uint32_t);

  xe::be<uint32_t> upload_buffer_address = 0;
  memcpy(&upload_buffer_address, args_stream_ptr + offset, sizeof(uint32_t));

  if (!upload_buffer_address) {
    return X_E_INVALIDARG;
  }

  if (buffer_size > kTMSClipMaxSize) {
    return X_ONLINE_E_STORAGE_FILE_IS_TOO_BIG;
  }

  uint8_t* upload_buffer_ptr =
      kernel_state()->memory()->TranslateVirtual<uint8_t*>(
          upload_buffer_address);

  std::span<uint8_t> upload_buffer =
      std::span<uint8_t>(upload_buffer_ptr, buffer_size);

  // Exclude null-terminator
  server_path_len -= 1;

  std::u16string server_path;
  server_path.resize(server_path_len);

  xe::copy_and_swap(server_path.data(), arg_server_path_ptr, server_path_len);

  std::string upload_file_path = xe::to_utf8(server_path);
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
        result = upload_file->WriteSync({upload_buffer.data(), buffer_size}, 0,
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

  XELOGI("{}: Size: {}b, Path: {}", __func__, buffer_size.get(),
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

  uint64_t xuid = 0;

  if (args->user_index == XUserIndexNone) {
    xuid = args->xuid;
  }

  bool is_per_user_title =
      args->storage_location == X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE;

  if (!xuid && is_per_user_title) {
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
    *numerator_ptr = 0;
  }

  if (denominator_ptr) {
    *denominator_ptr = 0;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserFindUsersUnkn58017(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  uint8_t* data_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(buffer_ptr);

  std::fill_n(data_ptr, sizeof(uint64_t), 0);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserFindUsers(uint32_t buffer_ptr) {
  // 584113E8, 58410B5D

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XUserFindUsers_Marshalled_Data* data_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<XUserFindUsers_Marshalled_Data*>(buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  FIND_USERS_RESPONSE* results_ptr =
      kernel_state_->memory()->TranslateVirtual<FIND_USERS_RESPONSE*>(
          internal_data_ptr->results_ptr);

  if (!data_ptr->internal_data_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->start_args_ptr) {
    return X_E_INVALIDARG;
  }

  if (!internal_data_ptr->results_ptr) {
    return X_E_INVALIDARG;
  }

  uint8_t* args_stream_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(
          internal_data_ptr->start_args_ptr);

  // Fixed 58410B5D
  memset(results_ptr, 0, internal_data_ptr->results_size);

  uint32_t offset = 0;

  // 1065
  uint32_t value_const = *reinterpret_cast<uint32_t*>(args_stream_ptr);

  offset += sizeof(uint32_t);

  // Data from 58017
  uint64_t unkn_value = *reinterpret_cast<uint64_t*>(args_stream_ptr + offset);

  offset += sizeof(uint64_t);

  // XnpLogonGetStatus
  SGADDR* security_gateway =
      reinterpret_cast<SGADDR*>(args_stream_ptr + offset);

  offset += sizeof(SGADDR);

  uint64_t xuid_issuer = *reinterpret_cast<uint64_t*>(args_stream_ptr + offset);

  offset += sizeof(uint64_t);

  uint32_t num_users = *reinterpret_cast<uint32_t*>(args_stream_ptr + offset);

  offset += sizeof(uint32_t);

  FIND_USER_INFO* users_ptr =
      reinterpret_cast<FIND_USER_INFO*>(args_stream_ptr + offset);

  std::vector<FIND_USER_INFO> find_users = {};
  std::vector<FIND_USER_INFO> resolved_users = {};

  for (uint32_t i = 0; i < num_users; i++) {
    FIND_USER_INFO user = users_ptr[i];

    const uint64_t xuid = xe::byte_swap(users_ptr[i].xuid);

    const auto user_profile =
        kernel_state()->xam_state()->GetUserProfileLive(xuid);

    // Only lookup non-local users
    if (user_profile) {
      FIND_USER_INFO local_user = users_ptr[i];

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

  uint32_t results_size =
      sizeof(FIND_USERS_RESPONSE) + (num_users * sizeof(FIND_USER_INFO));

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

X_HRESULT XLiveBaseApp::Unkn5008C(uint32_t buffer_ptr) {
  // Called on startup of blades dashboard v1888 to v2858
  // Address: 92433DA8

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  // 5 Arguments
  X_DATA_ARGS_5008C* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_5008C*>(
          internal_data_ptr->start_args_ptr);

  // Crashes if buffer filled with 0xFF
  memset(results_ptr, 0, internal_data_ptr->results_size);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XAccountGetPointsBalance(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Current Balance in sub menus:
  // All New Demos and Trailers
  // More Videos and Downloads
  // Address: 92433368

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  X_GET_POINTS_BALANCE_RESPONSE* results_ptr =
      kernel_state_->memory()->TranslateVirtual<X_GET_POINTS_BALANCE_RESPONSE*>(
          internal_data_ptr->results_ptr);

  // 2 Arguments
  X_DATA_ARGS_50077* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_50077*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  results_ptr->balance = 1000000000;
  results_ptr->dmp_account_status = DMP_STATUS_TYPE::DMP_STATUS_ACTIVE;
  results_ptr->response_flags =
      GET_POINTS_BALANCE_RESPONSE_FLAGS::ABOVE_LOW_BALANCE;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unkn5008B(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Fixes accessing marketplace Featured Downloads.
  // Address: 92433BB0

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  X_GET_FEATURED_DOWNLOADS_RESPONSE* results_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<X_GET_FEATURED_DOWNLOADS_RESPONSE*>(
              internal_data_ptr->results_ptr);

  // 5 Arguments
  X_DATA_ARGS_5008B* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_5008B*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  results_ptr->entries = 5;
  results_ptr->flags = 0xFFFFFFFF;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unkn5008F(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Fixes accessing marketplace sub menus:
  // All New Demos and Trailers
  // More Videos and Downloads
  // Address: 92433FA8

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  // 12 Arguments
  X_DATA_ARGS_5008F* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_5008F*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unkn50090(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Fixes accessing marketplace Game Downloads->All Games->Xbox Live Arcade
  // sub menu.
  // Address: 92434218

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  // 9 Arguments
  X_DATA_ARGS_50090* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_50090*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unkn50091(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Fixes accessing marketplace Game Downloads.
  // Address: 92434468

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  // 10 Arguments
  X_DATA_ARGS_50091* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_50091*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unkn50097(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888

  // Fixes accessing marketplace Memberships.
  // Address: 924346C0

  Generic_Marshalled_Data* data_ptr =
      kernel_state_->memory()->TranslateVirtual<Generic_Marshalled_Data*>(
          buffer_ptr);

  Internal_Marshalled_Data* internal_data_ptr =
      kernel_state_->memory()->TranslateVirtual<Internal_Marshalled_Data*>(
          data_ptr->internal_data_ptr);

  uint8_t* results_ptr = kernel_state_->memory()->TranslateVirtual<uint8_t*>(
      internal_data_ptr->results_ptr);

  // 13 Arguments
  X_DATA_ARGS_50097* args_ptr =
      kernel_state_->memory()->TranslateVirtual<X_DATA_ARGS_50097*>(
          internal_data_ptr->start_args_ptr);

  memset(results_ptr, 0, internal_data_ptr->results_size);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUpdateAccessTimes(uint32_t buffer_ptr) {
  // Called on blades dashboard v1888
  // More Videos and Downloads

  XLIVEBASE_UPDATE_ACCESS_TIMES* data_ptr =
      memory_->TranslateVirtual<XLIVEBASE_UPDATE_ACCESS_TIMES*>(buffer_ptr);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XMessageEnumerate(uint32_t buffer_length) {
  // Called on blades dashboard v1888

  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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

X_HRESULT XLiveBaseApp::XPresenceGetState(uint32_t buffer_length) {
  // Called on blades dashboard v1888

  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

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
