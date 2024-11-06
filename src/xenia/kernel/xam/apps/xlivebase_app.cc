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
#include "xenia/base/threading.h"
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
      // Guess:
      // XStorageDownloadToMemory -> XStorageDownloadToMemoryGetProgress
      XELOGD(
          "XStorageDownloadToMemoryGetProgress({:08x}, {:08x}) unimplemented",
          buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050009: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);
    }
    case 0x0005000A: {
      XELOGD("XStorageEnumerate({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
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
      XELOGD("XUserMuteListSetState({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      X_MUTE_LIST_SET_STATE* mute_list_ptr =
          memory_->TranslateVirtual<X_MUTE_LIST_SET_STATE*>(buffer_ptr);

      mute_list_ptr->set_muted = !mute_list_ptr->set_muted;

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
      XELOGD("XPresenceCreateEnumerator({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XPresenceCreateEnumerator(buffer_length);
    }
    case 0x0005801E: {
      // 54510846
      XELOGD("XPresenceSubscribe({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return XPresenceSubscribe(buffer_length);
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
      XELOGD(
          "CXLiveMessaging::XMessageGameInviteGetAcceptedInfo({:08X}, {:08X}) "
          "unimplemented",
          buffer_ptr, buffer_length);
      return X_E_SUCCESS;
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
      XELOGD("XPresenceInitializeLegacy({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XPresenceInitialize(buffer_length);
    }
    case 0x00058044: {
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return XPresenceUnsubscribe(buffer_length);
    }
    case 0x00058046: {
      // Used in newer games such as Forza 4, MW3, FH2
      //
      // Required to be successful for 4D530910 to detect signed-in profile
      XELOGD("XPresenceInitialize({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
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

X_HRESULT XLiveBaseApp::XStorageDownloadToMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

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

  uint8_t* filename_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(args->file_name_ptr);
  const std::string filename =
      xe::to_utf8(load_and_swap<std::u16string>(filename_ptr));

  XELOGI("XStorageBuildServerPath: Requesting file: {} From storage type: {}",
         filename, static_cast<uint32_t>(args->storage_location));

  if (args->server_path_ptr) {
    const std::string server_path = fmt::format(
        "title/{:08X}/storage/{}", kernel_state()->title_id(), filename);

    const std::string endpoint_API =
        fmt::format("{}{}", XLiveAPI::GetApiAddress(), server_path);

    uint8_t* server_path_ptr =
        kernel_state_->memory()->TranslateVirtual<uint8_t*>(
            args->server_path_ptr);

    std::memcpy(server_path_ptr, endpoint_API.c_str(), endpoint_API.size());

    uint32_t* server_path_length =
        kernel_state_->memory()->TranslateVirtual<uint32_t*>(
            args->server_path_length_ptr);

    *server_path_length =
        xe::byte_swap<uint32_t>(uint32_t(endpoint_API.size()));
  }

  return X_E_SUCCESS;
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
