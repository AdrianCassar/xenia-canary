/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

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
      uint32_t* marshalled_object_ptr =
          memory_->TranslateVirtual<uint32_t*>(buffer_ptr);

      return X_E_SUCCESS;
    }
    case 0x00058003: {
      // Called on startup of dashboard (netplay build)
      XELOGD("XLiveBaseLogonGetHR({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED;
    }
    case 0x0005008C: {
      // Called on startup of blades dashboard v1888 to v2858
      XELOGD("XLiveBaseUnk5008C, unimplemented");
      return X_E_FAIL;
    }
    case 0x00050094: {
      // Called on startup of blades dashboard v4532 to v4552
      XELOGD("XLiveBaseUnk50094, unimplemented");
      return X_E_FAIL;
    }
    case 0x00050008: {
      // Required to be successful for 534507D4
      XELOGD("XStorageDelete({:08x}, {:08x})", buffer_ptr, buffer_length);
      return XStorageDelete(buffer_ptr);
    }
    case 0x00050009: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);
    }
    case 0x0005000A: {
      // 4D5307D3 needs X_E_SUCCESS
      // 415607F7 needs X_E_FAIL to prevent crash.
      // 584108F0
      XELOGD("XStorageEnumerate({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
    }
    case 0x0005000B: {
      // Fixes Xbox Live error for 43430821
      XELOGD("XStorageUploadFromMemory({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XStorageUploadFromMemory(buffer_ptr);
    }
    case 0x0005000C: {
      XELOGD("XStringVerify({:08X} {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr, buffer_length);
    }
    case 0x0005000D: {
      // Fixes hang when leaving session for 545107D5
      // 415607D2 says this is XStringVerify
      XELOGD("XStringVerify({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr, buffer_length);
    }
    case 0x0005000E: {
      // Before every call there is a call to XUserFindUsers
      // Success stub:
      // 584113E8 successfully creates session.
      // 58410B5D craches.
      XELOGD("XUserFindUsersResponseSize({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
    }
    case 0x0005000F: {
      // 41560855 included from TU 7
      // Attempts to set a dvar for ui_email_address but fails on
      // WideCharToMultiByte
      //
      // 4D530AA5 encounters "Failed to retrieve account credentials".
      XELOGD("_XAccountGetUserInfo({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_ERROR_FUNCTION_FAILED;
    }
    case 0x00050010: {
      XELOGD("XAccountGetUserInfo({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_ERROR_FUNCTION_FAILED;
    }
    case 0x0005801C: {
      // Called on blades dashboard v1888
      XELOGD("XLiveBaseUnk5801C({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return Unk5801C(buffer_length);
    }
    case 0x00058024: {
      // Called on blades dashboard v1888
      XELOGD("XLiveBaseUnk58024({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return Unk58024(buffer_length);
    }
    case 0x00050036: {
      XELOGD("XOnlineQuerySearch({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050038: {
      // 4D5307D3
      // 4D5307D1
      XELOGD("XOnlineQuerySearch({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050077: {
      // Called on blades dashboard v1888
      // Current Balance in sub menus:
      // All New Demos and Trailers
      // More Videos and Downloads
      XELOGD("XLiveBaseUnk50077({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050079: {
      // Fixes Xbox Live error for 454107DB
      XELOGD("XLiveBaseUnk50079({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005008B: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Featured Downloads.
      XELOGD("XLiveBaseUnk5008B({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005008F: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace sub menus:
      // All New Demos and Trailers
      // More Videos and Downloads
      XELOGD("XLiveBaseUnk5008F({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050090: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Game Downloads->All Games->Xbox Live Arcade
      // sub menu.
      XELOGD("XLiveBaseUnk50090({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050091: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Game Downloads.
      XELOGD("XLiveBaseUnk50091({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050097: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Memberships.
      XELOGD("XLiveBaseUnk50097({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058004: {
      // Called on startup, seems to just return a bool in the buffer.
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetLogonId({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // ?
      return X_E_SUCCESS;
    }
    case 0x00058006: {
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetNatType({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // XONLINE_NAT_OPEN
      return X_E_SUCCESS;
    }
    case 0x00058007: {
      // Occurs if title calls XOnlineGetServiceInfo, expects dwServiceId
      // and pServiceInfo. pServiceInfo should contain pointer to
      // XONLINE_SERVICE_INFO structure.
      XELOGD("CXLiveLogon::GetServiceInfo({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return GetServiceInfo(buffer_ptr, buffer_length);
    }
    case 0x00058009: {
      XELOGD("XContentGetMarketplaceCounts({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005800C: {
      // 464F0800
      XELOGD("XUserMuteListSetState({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      X_MUTE_SET_STATE* remote_player_ptr =
          memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

      if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
        return X_E_INVALIDARG;
      }

      return X_E_SUCCESS;
    }
    case 0x0005800D: {
      // 464F0800
      XELOGD("XUserMuteListSetState({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      X_MUTE_SET_STATE* remote_player_ptr =
          memory_->TranslateVirtual<X_MUTE_SET_STATE*>(buffer_ptr);

      if (!IsOnlineXUID(remote_player_ptr->remote_xuid)) {
        return X_E_INVALIDARG;
      }

      return X_E_SUCCESS;
    }
    case 0x0005800E: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XUserMuteListQuery({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058017: {
      XELOGD("XUserFindUsers({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058019: {
      // 54510846
      XELOGD("XPresenceCreateEnumerator({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceCreateEnumerator(buffer_length);
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
      // We should create a XamEnumerate-able empty list here, but I'm not
      // sure of the format.
      // buffer_length seems to be the same ptr sent to 0x00058004.
      XELOGD("CXLiveFriends::Enumerate({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return CreateFriendsEnumerator(buffer_length);
    }
    case 0x00058023: {
      // 584107D7
      // 5841091C expects xuid_invitee
      XELOGD(
          "CXLiveMessaging::XMessageGameInviteGetAcceptedInfo({:08X}, {:08X})",
          buffer_ptr, buffer_length);
      return XInviteGetAcceptedInfo(buffer_length);
    }
    case 0x00058032: {
      XELOGD("XGetTaskProgress({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
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

  const X_ARGUEMENT_ENTRY* entry =
      memory->TranslateVirtual<X_ARGUEMENT_ENTRY*>(buffer_length);

  const uint32_t max_peer_subscriptions =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(entry->object_ptr));

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

  const X_PRESENCE_SUBSCRIBE* args_list =
      memory->TranslateVirtual<X_PRESENCE_SUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->user_index.object_ptr));
  const uint32_t num_peers = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->peers.object_ptr));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  if (num_peers <= 0) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address = args_list->peer_xuids_ptr.object_ptr;

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

  const X_PRESENCE_UNSUBSCRIBE* args_list =
      memory->TranslateVirtual<X_PRESENCE_UNSUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->user_index.object_ptr));
  const uint32_t num_peers = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->peers.object_ptr));

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  if (num_peers <= 0) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address = args_list->peer_xuids_ptr.object_ptr;

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

  const X_PRESENCE_CREATE* args_list = reinterpret_cast<X_PRESENCE_CREATE*>(
      memory->TranslateVirtual(buffer_length));

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->user_index.object_ptr));
  const uint32_t num_peers = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->num_peers.object_ptr));
  const uint32_t max_peers = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->max_peers.object_ptr));
  const uint32_t starting_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(args_list->starting_index.object_ptr));

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

  const uint32_t xuid_address = args_list->peer_xuids_ptr.object_ptr;
  const uint32_t buffer_address = args_list->buffer_length_ptr.object_ptr;
  const uint32_t handle_address = args_list->enumerator_handle_ptr.object_ptr;

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

  uint32_t* buffer_ptr = memory->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory->TranslateVirtual<uint32_t*>(handle_address);

  const uint32_t presence_buffer_size =
      static_cast<uint32_t>(e->items_per_enumerate() * e->item_size());

  *buffer_ptr = xe::byte_swap<uint32_t>(presence_buffer_size);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GetServiceInfo(uint32_t serviceid,
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

X_HRESULT XLiveBaseApp::CreateFriendsEnumerator(uint32_t buffer_args) {
  if (!buffer_args) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_ARGUMENT_LIST* arg_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_args);

  if (arg_list->argument_count <= 3) {
    assert_always(
        "XLiveBaseApp::CreateFriendsEnumerator - Invalid argument count!");
  }

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[0].object_ptr));
  const uint32_t friends_starting_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[1].object_ptr));
  const uint32_t friends_amount = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[2].object_ptr));

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (friends_starting_index >= X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  if (friends_amount > X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  const uint32_t buffer_address = arg_list->entry[3].object_ptr;
  const uint32_t handle_address = arg_list->entry[4].object_ptr;

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  uint32_t* buffer_ptr = memory->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory->TranslateVirtual<uint32_t*>(handle_address);

  *buffer_ptr = 0;
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

  *buffer_ptr = xe::byte_swap<uint32_t>(friends_buffer_size);

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
  X_INVITE_GET_ACCEPTED_INFO* AcceptedInfo =
      memory_->TranslateVirtual<X_INVITE_GET_ACCEPTED_INFO*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(AcceptedInfo->user_index.object_ptr));

  X_INVITE_INFO* invite_info = reinterpret_cast<X_INVITE_INFO*>(
      memory_->TranslateVirtual(AcceptedInfo->invite_info.object_ptr));

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
  memcpy(&invite_info->host_info.hostAddress.abOnline, mac.raw(),
         sizeof(MacAddress));

  invite_info->host_info.hostAddress.wPortOnline = session->Port();

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStringVerify(uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t* data_ptr =
      kernel_state_->memory()->TranslateVirtual<uint32_t*>(buffer_ptr);

  // TODO(Gliniak): Figure out structure after marshaling.
  // Based on what game does there must be some structure that
  // checks if string is proper.
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
  // 41560817, 513107D5, 513107D9 has issues with X_E_FAIL.
  // 513107D5, 513107D9 prefer X_ERROR_FUNCTION_FAILED.

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  // 415607DD has issues with X_E_SUCCESS and X_ERROR_FUNCTION_FAILED.
  // 41560834 fails on memcpy due to dwBytesTotal corruption.
  // if (kernel_state()->title_id() == 0x415607DD ||
  //    kernel_state()->title_id() == 0x41560834) {
  //  return X_E_FAIL;
  // }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageUploadFromMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
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
      const std::string path =
          fmt::format("title/{:08X}/storage/clips/{}",
                      kernel_state()->title_id(), filename_str);

      backend_server_path_str =
          fmt::format("{}/{}", backend_domain_prefix, path);

      symlink_path = fmt::format("{}{}", xstorage_symboliclink, path);
      symlink_path = utf8::fix_guest_path_separators(symlink_path);

      xe::be<uint32_t> leaderboard_id = 0;

      if (args->storage_location_info_ptr) {
        uint32_t* leaderboard_id_ptr =
            kernel_state_->memory()->TranslateVirtual<uint32_t*>(
                args->storage_location_info_ptr);

        leaderboard_id = *leaderboard_id_ptr;

        XELOGI("{}: Leaderboard ID: {}", __func__, leaderboard_id.get());
      }

      storage_type = "Game Clip";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      const std::string path = fmt::format(
          "title/{:08X}/storage/{}", kernel_state()->title_id(), filename_str);

      backend_server_path_str =
          fmt::format("{}/{}", backend_domain_prefix, path);

      symlink_path = fmt::format("{}{}", xstorage_symboliclink, path);
      symlink_path = utf8::fix_guest_path_separators(symlink_path);

      storage_type = "Per Title";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      const std::string path =
          fmt::format("user/{:016X}/title/{:08X}/storage/{}", xuid,
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

X_STORAGE_FACILITY XLiveBaseApp::GetStorageFacilityTypeFromServerPath(
    std::string server_path) {
  std::string title_facility =
      R"(title(/|\\)[0-9a-fA-F]{8}(/|\\)storage(/|\\))";
  std::string clips_facility = R"(clips(/|\\))";
  std::string user_facility = R"(user(/|\\)[0-9a-fA-F]{16}(/|\\))";

  std::regex title_facility_regex(title_facility);
  std::regex clips_facility_regex(
      fmt::format("{}{}", title_facility, clips_facility));
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

X_HRESULT XLiveBaseApp::Unk58024(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_DATA_58024* entry = memory->TranslateVirtual<X_DATA_58024*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(
      memory->TranslateVirtual(entry->xuid.object_ptr));
  uint32_t ukn2 = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn2.object_ptr));
  uint32_t ukn3_ptr = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn3.object_ptr));

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unk5801C(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_DATA_5801C* entry = memory->TranslateVirtual<X_DATA_5801C*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(
      memory->TranslateVirtual(entry->xuid.object_ptr));
  uint32_t ukn2 = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn2.object_ptr));
  uint32_t ukn3_ptr = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn3.object_ptr));

  return X_E_SUCCESS;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
