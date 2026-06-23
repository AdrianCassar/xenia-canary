/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <span>

#include "xenia/kernel/xam/apps/xlivebase_app.h"

#include <xenia/kernel/util/xex2_info.h>
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xam/unmarshaller/generic_unmarshaller.h"
#include "xenia/kernel/xam/unmarshaller/schema_in_memory.h"
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

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

// Deleted XOnline Functions
// XGenresEnumerate (Blades) - 0x00050090
// XEnumerateTitlesByFilter (Blades) - 0x00050091
//
// These functions no longer exist in the latest online schema, their ordinals
// are not found. Blades likely wouldn't run on the latest version of XAM which
// includes the latest online schema. Therefore this usually wouldn't be an
// issue.
//
// We would need to support multiple versions of XLiveBase/online schema to fix
// this issue. Currently these message IDs do not conflict with any known usage
// therefore we can retain the functionality for now.
// Alternatively we could rely on the ordinal for async tasks instead of message
// ID?

XLiveBaseApp::XLiveBaseApp(KernelState* kernel_state)
    : App(kernel_state, 0xFC) {}

// Convert message ID to latest online schema.
uint32_t XLiveBaseApp::GetDispatchMessageID(uint32_t message,
                                            uint32_t buffer_ptr,
                                            uint32_t buffer_length) const {
  if (buffer_length != sizeof(XLIVEBASE_ASYNC_MESSAGE)) {
    return message;
  }

  SchemaInMemory schema(kernel_state_);
  schema.Bind(kernel_state_->xam_state()->GetOnlineSchemaAddress());

  const GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);
  XLivebaseAsyncTask task = unmarshaller.GetAsyncTask();
  SchemaInMemory title_schema = task.GetTitleSchema();

  const std::string function_name =
      schema.GetOrdinalFunctionName(task.GetXLiveAsyncTask()->ordinal);

  XELOGD("{}({:08X}, {:08X})",
         function_name.empty() ? "Unknown" : function_name, buffer_ptr,
         buffer_length);

  // If title (Dashboard/Avatar Editor) loads schema via XamGetOnlineSchema then
  // version will match.
  if (title_schema.SchemaVersion() == schema.SchemaVersion()) {
    return message;
  }

  uint16_t new_schema_index = 0;
  bool found = schema.XLookupSchemaIndexFromOrdinal(
      task.GetXLiveAsyncTask()->ordinal, &new_schema_index);

  const uint32_t message_id =
      found ? new_schema_index | kAsyncSchemaIndexMask : message;

  if (!found) {
    XELOGI("XLiveBase ordinal not found: {:04X}",
           task.GetXLiveAsyncTask()->ordinal.get());
    assert_always();
  }

  return message_id;
}

/// <param name="buffer_ptr"> - Generic param1 could be anything.</param>
/// <param name="buffer_length"> - Generic param2 could be anything.</param>
X_HRESULT XLiveBaseApp::ExecuteDispatchMessage(uint32_t message,
                                               uint32_t buffer_ptr,
                                               uint32_t buffer_length,
                                               uint32_t* extended_error) {
  // NOTE: buffer_length may be zero or valid.
  uint8_t* buffer = memory_->TranslateVirtual<uint8_t*>(buffer_ptr);

  const uint32_t message_id =
      GetDispatchMessageID(message, buffer_ptr, buffer_length);

  switch (message_id) {
    case 0x00050002: {
      return XInviteSend(buffer_ptr);
    }
    case 0x00050008: {
      return XStorageDelete(buffer_ptr);
    }
    case 0x00050009: {
      // 534507D4, 555307D7, 545107D1
      return XStorageDownloadToMemory(buffer_ptr, extended_error);
    }
    case 0x0005000A: {
      // 4D5307D3, 415607F7, 584108F0, 5454082B, 545407F8, 575207FD, 555307D7
      return XStorageEnumerate(buffer_ptr);
    }
    case 0x0005000B: {
      // 43430821, 4E4D07D3
      return XStorageUploadFromMemory(buffer_ptr);
    }
    case 0x0005000C: {
      // 57520829, 4156081C, 415607D2
      return XStringVerify(buffer_ptr);
    }
    case 0x0005000D: {
      // 4D5307EA, 58410889
      return XUserEstimateRankForRating(buffer_ptr);
    }
    case 0x0005000E: {
      // 584113E8
      return XUserFindUsers(buffer_ptr);
    }
    case 0x0005000F: {
      // 454107DB, 4D530AA5, 454107F1
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x0005006E: {
      // 4D5307D3, 4D5307D1, 545407E2, 545407E3, 545407D2, 545407D3, 534507D4
      return XOnlineQuerySearch(buffer_ptr);
    }
    case 0x00050090: {
      // Deleted XOnline Function
      XELOGD("XGenresEnumerate({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XGenresEnumerate(buffer_ptr);
    }
    case 0x00050091: {
      // Deleted XOnline Function
      XELOGD("XEnumerateTitlesByFilter({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XEnumerateTitlesByFilter(buffer_ptr);
    }
    case 0x000500B0: {
      return XAccountGetUserInfo(buffer_ptr);
    }
    case 0x000500C6: {
      return XAccountGetPointsBalance(buffer_ptr);
    }
    case 0x000500F7: {
      return XOfferingContentEnumerate(buffer_ptr);
    }
    case 0x000500FE: {
      return XGetBannerList(buffer_ptr);
    }
    case 0x000500FF: {
      return XGetBannerListHot(buffer_ptr);
    }
    case 0x00050104: {
      return XOfferingSubscriptionEnumerate(buffer_ptr);
    }
    case 0x00050119: {
      return XPassportGetMemberName(buffer_ptr);
    }
    case 0x0005012B: {
      return XUserValidateAvatarManifest(buffer_ptr);
    }
    case 0x00058003: {
      assert_true(!buffer_ptr || !buffer_length);
      // Called on startup of dashboard
      XELOGD("XLiveBaseLogonGetHR({:08X}, {:08X})", buffer_ptr, buffer_length);
      const uint32_t live_connection_state =
          cvars::network_mode == NETWORK_MODE::XBOXLIVE
              ? X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED
              : X_ONLINE_S_LOGON_DISCONNECTED;

      return live_connection_state;
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
    case 0x00058056: {
      XELOGD("XPresenceInitializeEx({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XPresenceInitialize(buffer_ptr, buffer_length);
    }
    case 0x0005806A: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XLIVEBASE_WEBSERVICETASK_CALL));
      XELOGD("XOnlineCallWebService({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XOnlineCallWebService(buffer_ptr);
    }
    case 0x00058072: {
      XELOGD("XOnlineGetWebServiceTaskBufferSize({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XOnlineGetWebServiceTaskBufferSize(buffer_ptr);
    }
  }

  auto xlivebase_log = fmt::format(
      "{} XLIVEBASE message app={:08X}, msg={:08X}, buffer_ptr={:08X}, "
      "buffer_length={:08X}",
      cvars::stub_xlivebase ? "Stubbed" : "Unimplemented", app_id(), message_id,
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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 1);

  const X_PRESENCE_INITIALIZE* initialize =
      memory_->TranslateVirtual<X_PRESENCE_INITIALIZE*>(buffer_length);

  const uint32_t max_peer_subscriptions = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          initialize->max_peer_subscriptions.argument_value_ptr)));

  if (max_peer_subscriptions > X_ONLINE_PEER_SUBSCRIPTIONS) {
    return X_ONLINE_E_NOTIFICATION_TOO_MANY_SUBS;
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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 3);

  const X_PRESENCE_SUBSCRIBE* subscribe_args =
      memory_->TranslateVirtual<X_PRESENCE_SUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          subscribe_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(subscribe_args->peers.argument_value_ptr)));

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_INVALIDARG;
  }

  const uint32_t xuid_address =
      static_cast<uint32_t>(subscribe_args->peer_xuids_ptr.argument_value_ptr);

  if (!xuid_address) {
    return X_E_INVALIDARG;
  }

  const xe::be<uint64_t>* peer_xuids =
      memory_->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state_->xam_state()->GetUserProfile(user_index);

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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 3);

  const X_PRESENCE_UNSUBSCRIBE* unsubscribe_args =
      memory_->TranslateVirtual<X_PRESENCE_UNSUBSCRIBE*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          unsubscribe_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(unsubscribe_args->peers.argument_value_ptr)));

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
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
      memory_->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state_->xam_state()->GetUserProfile(user_index);

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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 7);

  const X_PRESENCE_CREATE* create_args = reinterpret_cast<X_PRESENCE_CREATE*>(
      memory_->TranslateVirtual(buffer_length));

  const uint32_t user_index =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(create_args->user_index.argument_value_ptr)));
  const uint32_t num_peers =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(create_args->num_peers.argument_value_ptr)));
  const uint32_t max_peers =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(create_args->max_peers.argument_value_ptr)));
  const uint32_t starting_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          create_args->starting_index.argument_value_ptr)));
  const uint32_t xuid_address =
      static_cast<uint32_t>(create_args->peer_xuids_ptr.argument_value_ptr);
  const uint32_t buffer_address =
      static_cast<uint32_t>(create_args->buffer_length_ptr.argument_value_ptr);
  const uint32_t handle_address = static_cast<uint32_t>(
      create_args->enumerator_handle_ptr.argument_value_ptr);

  const xe::be<uint64_t>* peer_xuids_ptr =
      memory_->TranslateVirtual<xe::be<uint64_t>*>(xuid_address);
  uint32_t* buffer_size_ptr =
      memory_->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory_->TranslateVirtual<uint32_t*>(handle_address);

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  *handle_ptr = 0;

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  *buffer_size_ptr = 0;

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
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

  if (!xuid_address) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto profile = kernel_state_->xam_state()->GetUserProfile(user_index);

  auto e = make_object<XStaticEnumerator<X_ONLINE_PRESENCE>>(kernel_state_,
                                                             num_peers);
  auto result = e->Initialize(user_index, app_id(), 0x5801A, 0x5801B, 0);

  if (XFAILED(result)) {
    return result;
  }

  const auto peer_xuids =
      std::vector<uint64_t>(peer_xuids_ptr, peer_xuids_ptr + num_peers);

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

  XQuerySearchUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller.PrettyPrintAttributesSpec();

  unmarshaller.ZeroResults();

  QUERY_SEARCH_RESULT* results_ptr =
      unmarshaller.Results<QUERY_SEARCH_RESULT>();

  const auto services = kernel_state_->GetXboxLiveAPI()->GetServices();

  results_ptr->total_results =
      static_cast<uint32_t>(services->QuerySearchResults().size());
  results_ptr->returned_results =
      static_cast<uint32_t>(services->QuerySearchResults().size());
  results_ptr->num_result_attributes = static_cast<uint32_t>(
      unmarshaller.SpecAttributes().size() * results_ptr->returned_results);

  X_ONLINE_QUERY_ATTRIBUTE* attributes_ptr =
      reinterpret_cast<X_ONLINE_QUERY_ATTRIBUTE*>(results_ptr + 1);

  uint32_t attributes_address =
      memory_->HostToGuestVirtual(std::to_address(attributes_ptr));

  results_ptr->attributes_ptr = attributes_address;

  for (uint32_t gateway_index = 0;
       const auto& gateway : services->QuerySearchResults()) {
    uint32_t attribute_index = static_cast<uint32_t>(
        (gateway_index * unmarshaller.SpecAttributes().size()));

    uint8_t* binary_alloc_ptr = reinterpret_cast<uint8_t*>(
        attributes_ptr + results_ptr->num_result_attributes);

    for (auto const& attribute : unmarshaller.SpecAttributes()) {
      switch (attribute.type) {
        case X_ONLINE_LSP_ATTRIBUTE_TSADDR: {
          assert_false(attribute.length != sizeof(TSADDR));

          // TODO(Adrian): Instead of allocating use title allocated buffer
          // results_ptr.
          uint32_t TSADDR_adderess = memory_->SystemHeapAlloc(sizeof(TSADDR));

          TSADDR* TSADDR_ptr =
              memory_->TranslateVirtual<TSADDR*>(TSADDR_adderess);

          *TSADDR_ptr = gateway;

          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_TSADDR;
          attributes_ptr[attribute_index].info.blob.length = sizeof(TSADDR);
          attributes_ptr[attribute_index].info.blob.value_ptr = TSADDR_adderess;
        } break;
        case X_ONLINE_LSP_ATTRIBUTE_XNKID: {
          assert_false(attribute.length != sizeof(XNKID));

          uint32_t XNKID_adderess = memory_->SystemHeapAlloc(sizeof(XNKID));

          XNKID* XNKID_ptr = memory_->TranslateVirtual<XNKID*>(XNKID_adderess);

          xe::be<uint64_t> session_id = GenerateSessionId(XNKID_SERVER);

          std::memcpy(XNKID_ptr, &session_id, sizeof(XNKID));

          attributes_ptr[attribute_index].attribute_id =
              X_ONLINE_LSP_ATTRIBUTE_XNKID;
          attributes_ptr[attribute_index].info.blob.length = sizeof(XNKID);
          attributes_ptr[attribute_index].info.blob.value_ptr = XNKID_adderess;
        } break;
        case X_ONLINE_LSP_ATTRIBUTE_KEY: {
          assert_false(attribute.length != sizeof(XNKEY));

          uint32_t XNKEY_adderess = memory_->SystemHeapAlloc(sizeof(XNKEY));

          XNKEY* XNKEY_ptr = memory_->TranslateVirtual<XNKEY*>(XNKEY_adderess);

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
          uint32_t string_adderess = memory_->SystemHeapAlloc(size);

          char16_t* string_ptr =
              memory_->TranslateVirtual<char16_t*>(string_adderess);

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
         results_ptr->returned_results.get(), unmarshaller.NumAttributes());

  return X_E_SUCCESS;
}

// Check whether XLSP services are available
X_HRESULT XLiveBaseApp::XOnlineGetServiceInfo(uint32_t serviceid,
                                              uint32_t serviceinfo) {
  if (!kernel_state_->GetXboxLiveAPI()->IsConnectedToServer()) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  if (!serviceinfo) {
    return X_E_SUCCESS;
  }

  X_ONLINE_SERVICE_INFO* service_info_ptr =
      memory_->TranslateVirtual<X_ONLINE_SERVICE_INFO*>(serviceinfo);

  std::memset(service_info_ptr, 0, sizeof(X_ONLINE_SERVICE_INFO));

  const auto services = kernel_state_->GetXboxLiveAPI()->GetServices();

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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 5);

  X_CREATE_FRIENDS_ENUMERATOR* friends_enumerator =
      memory_->TranslateVirtual<X_CREATE_FRIENDS_ENUMERATOR*>(buffer_length);

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->user_index.argument_value_ptr)));
  const uint32_t friends_starting_index = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->friends_starting_index.argument_value_ptr)));
  const uint32_t friends_amount = xe::load_and_swap<uint32_t>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          friends_enumerator->friends_amount.argument_value_ptr)));
  const uint32_t buffer_address =
      static_cast<uint32_t>(friends_enumerator->buffer_ptr.argument_value_ptr);
  const uint32_t handle_address =
      static_cast<uint32_t>(friends_enumerator->handle_ptr.argument_value_ptr);

  uint32_t* buffer_size_ptr =
      memory_->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory_->TranslateVirtual<uint32_t*>(handle_address);

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  // 41560834 and 45410923 expect invalid handle of 0 (not -1) for failure,
  // therefore set as soon as possible.
  *handle_ptr = 0;

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  *buffer_size_ptr = 0;

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (friends_starting_index >= X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  if (friends_amount > X_ONLINE_MAX_FRIENDS) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  auto const profile = kernel_state_->xam_state()->GetUserProfile(user_index);

  auto e = object_ref<FriendsEnumerator>(
      new FriendsEnumerator(kernel_state_, friends_amount));

  auto result = e->Initialize(-1, app_id(), 0x58021, 0x58022, 0);

  if (XFAILED(result)) {
    return result;
  }

  for (auto i = friends_starting_index; i < e->items_per_enumerate(); i++) {
    X_ONLINE_FRIEND peer = {};

    const bool is_friend = profile->GetFriendFromIndex(i, &peer);

    if (is_friend) {
      e->AppendItem(peer);
    }
  }

  const uint32_t friends_buffer_size =
      static_cast<uint32_t>(e->items_per_enumerate() * e->item_size());

  *buffer_size_ptr = xe::byte_swap<uint32_t>(friends_buffer_size);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());
  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XInviteSend(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  // Current session must have PRESENCE flag.

  XInviteSendUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  new xe::ui::HostNotificationWindow(
      kernel_state_->emulator()->imgui_drawer(), "Invites aren't supported!",
      xe::to_utf8(unmarshaller.DisplayString()), 0);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XInviteGetAcceptedInfo(uint32_t buffer_ptr,
                                               uint32_t buffer_length) {
  if (!buffer_ptr || !buffer_length) {
    return X_E_INVALIDARG;
  }

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 2);

  const X_INVITE_GET_ACCEPTED_INFO* accepted_info =
      memory_->TranslateVirtual<X_INVITE_GET_ACCEPTED_INFO*>(buffer_length);

  const uint32_t user_index =
      xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(
          static_cast<uint32_t>(accepted_info->user_index.argument_value_ptr)));

  X_INVITE_INFO* invite_info = reinterpret_cast<X_INVITE_INFO*>(
      memory_->TranslateVirtual(static_cast<uint32_t>(
          accepted_info->invite_info.argument_value_ptr)));

  if (!kernel_state_->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_FAIL;
  }

  const auto user_profile =
      kernel_state_->xam_state()->GetUserProfile(user_index);

  *invite_info = user_profile->GetSelfInvite();

  // Reset self invite
  user_profile->SetSelfInvite({});

  const auto presence = kernel_state_->GetXboxLiveAPI()->GetFriendsPresence(
      {invite_info->xuid_inviter});

  uint64_t session_id = 0;

  if (!presence->PlayersPresence().empty()) {
    session_id = presence->PlayersPresence().front().SessionID();
  }

  if (!session_id) {
    new xe::ui::HostNotificationWindow(
        kernel_state_->emulator()->imgui_drawer(), "Joining Session",
        "Unable to join session", 0);

    return X_ONLINE_E_SESSION_NOT_FOUND;
  }

  const auto session = kernel_state_->GetXboxLiveAPI()->XSessionGet(session_id);

  if (!session.SessionID_UInt()) {
    new xe::ui::HostNotificationWindow(
        kernel_state_->emulator()->imgui_drawer(), "Joining Session",
        "Unable to join session", 0);

    return X_ONLINE_E_SESSION_NOT_FOUND;
  }

  std::set<uint64_t> local_members = {};

  for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
    const auto profile = kernel_state_->xam_state()->GetUserProfile(i);

    if (profile && profile->IsLiveEnabled()) {
      local_members.insert(profile->GetOnlineXUID());
    }
  }

  kernel_state_->GetXboxLiveAPI()->SessionPreJoin(session_id, local_members);

  Uint64toXNKID(session.SessionID_UInt(), &invite_info->host_info.sessionID);
  GenerateIdentityExchangeKey(&invite_info->host_info.keyExchangeKey);

  XLiveAPI::GetXnAddrFromSessionObject(session,
                                       &invite_info->host_info.hostAddress);

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GenericMarshalled(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  const std::string task_url = unmarshaller.GetAsyncTask().GetTaskUrl();

  XELOGI("{}:: URL: {}", __func__, task_url);

  uint8_t* args_ptr = unmarshaller.DeserializeReinterpret<uint8_t>();

  if (!args_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  uint8_t* results_ptr = unmarshaller.Results<uint8_t>();

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

  if (!kernel_state_->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile =
      kernel_state_->xam_state()->GetUserProfile(remote_player_ptr->user_index);

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

  if (!kernel_state_->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile =
      kernel_state_->xam_state()->GetUserProfile(remote_player_ptr->user_index);

  bool muted = user_profile->MutePlayer(remote_player_ptr->remote_xuid);

  if (muted) {
    kernel_state_->BroadcastNotification(kXNotificationSystemMuteListChanged,
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

  if (!kernel_state_->xam_state()->IsUserSignedIn(
          remote_player_ptr->user_index)) {
    return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
  }

  auto user_profile =
      kernel_state_->xam_state()->GetUserProfile(remote_player_ptr->user_index);

  bool unmuted = user_profile->UnmutePlayer(remote_player_ptr->remote_xuid);

  if (unmuted) {
    kernel_state_->BroadcastNotification(kXNotificationSystemMuteListChanged,
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

  XAccountGetUserInfoUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  X_GET_USER_INFO_RESPONSE* user_info_response_ptr =
      unmarshaller.Results<X_GET_USER_INFO_RESPONSE>();

  unmarshaller.ZeroResults();

  // Example usage
  std::u16string first_name = u"First Name";
  std::u16string last_name = u"Last Name";
  std::u16string email = u"example@email.com";

  char16_t* first_name_ptr =
      reinterpret_cast<char16_t*>(user_info_response_ptr + 1);
  string_util::copy_and_swap_truncating(first_name_ptr, first_name.data(),
                                        MAX_FIRSTNAME_SIZE);

  char16_t* last_name_ptr =
      reinterpret_cast<char16_t*>(first_name_ptr + MAX_FIRSTNAME_SIZE);
  string_util::copy_and_swap_truncating(last_name_ptr, last_name.data(),
                                        MAX_LASTNAME_SIZE);

  char16_t* email_ptr =
      reinterpret_cast<char16_t*>(last_name_ptr + MAX_LASTNAME_SIZE);
  string_util::copy_and_swap_truncating(email_ptr, email.data(),
                                        MAX_EMAIL_SIZE);

  user_info_response_ptr->first_name_length =
      static_cast<uint32_t>(first_name.size());
  user_info_response_ptr->first_name =
      memory_->HostToGuestVirtual(std::to_address(first_name_ptr));

  user_info_response_ptr->last_name_length =
      static_cast<uint32_t>(last_name.size());
  user_info_response_ptr->last_name =
      memory_->HostToGuestVirtual(std::to_address(last_name_ptr));

  // 4D530AA5 wants an email
  user_info_response_ptr->email_length = static_cast<uint32_t>(email.size());
  user_info_response_ptr->email =
      memory_->HostToGuestVirtual(std::to_address(email_ptr));

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageEnumerate(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageEnumerateUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  // Fixed 415607F7 from crashing.
  unmarshaller.ZeroResults();

  X_STORAGE_ENUMERATE_RESULTS* results_ptr =
      unmarshaller.Results<X_STORAGE_ENUMERATE_RESULTS>();

  auto user_profle =
      kernel_state_->xam_state()->GetUserProfile(unmarshaller.UserIndex());

  const std::string guest_enumeration_path =
      xe::to_utf8(unmarshaller.ServerPath());
  const std::string host_enumeration_path_base =
      ConvertGuestServerPathToHostServerPath(guest_enumeration_path);

  const uint32_t available_to_return_items =
      static_cast<uint32_t>(std::floor<uint32_t>(static_cast<uint32_t>(
          (unmarshaller.GetAsyncTask().GetXLiveAsyncTask()->results_size -
           sizeof(X_STORAGE_ENUMERATE_RESULTS)) /
          (sizeof(X_STORAGE_FILE_INFO) +
           (X_ONLINE_MAX_PATHNAME_LENGTH * sizeof(char16_t))))));

  X_STORAGE_FILE_INFO* items_array_ptr =
      reinterpret_cast<X_STORAGE_FILE_INFO*>(results_ptr + 1);

  uint32_t items_address =
      memory_->HostToGuestVirtual(std::to_address(items_array_ptr));

  results_ptr->items_ptr = items_address;

  char16_t* items_path_name_array_ptr =
      reinterpret_cast<char16_t*>(items_array_ptr + available_to_return_items);

  X_STATUS result = X_E_SUCCESS;

  if (!available_to_return_items) {
    return X_E_INVALIDARG;
  }

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromGuestServerPath(guest_enumeration_path);

  if (facility_type != X_STORAGE_FACILITY::FACILITY_PER_TITLE) {
    XELOGI("{}: Unsupported Storage Facility: {}", __func__,
           static_cast<uint32_t>(facility_type));
    return X_E_FAIL;
  }

  const auto module = kernel_state_->GetExecutableModule();
  xe::xex2_version title_version = {};

  if (module) {
    xex2_opt_execution_info* info = nullptr;
    module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &info);

    if (info) {
      title_version = info->version();
    }
  }

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  bool enumerated_backend = false;

  if (route_backend) {
    const uint32_t max_items = std::min<uint32_t>(
        unmarshaller.MaxResultsToReturn(), available_to_return_items);

    const auto enumeration_result =
        kernel_state_->GetXboxLiveAPI()->XStorageEnumerate(
            host_enumeration_path_base, max_items);

    const auto& enumerated_files = enumeration_result.first;
    enumerated_backend = enumeration_result.second;

    for (uint32_t item_index = unmarshaller.StartingIndex();
         const auto& entry : enumerated_files->Items()) {
      std::string filename = utf8::find_name_from_path(entry.FilePath(), '/');

      // Path must use /
      std::u16string guest_backend_item_path = xe::to_utf16(
          ConvertHostServerPathToGuestServerPath(entry.FilePath()));

      char16_t* item_path_ptr =
          std::to_address(items_path_name_array_ptr +
                          (item_index * (X_ONLINE_MAX_PATHNAME_LENGTH)));

      uint32_t item_path_address = memory_->HostToGuestVirtual(item_path_ptr);

      xe::string_util::copy_and_swap_truncating(
          item_path_ptr, guest_backend_item_path, X_ONLINE_MAX_PATHNAME_LENGTH);

      items_array_ptr[item_index].path_name =
          static_cast<uint32_t>(guest_backend_item_path.size());
      items_array_ptr[item_index].path_name_ptr = item_path_address;

      items_array_ptr[item_index].title_id = entry.TitleID();
      items_array_ptr[item_index].title_version = title_version.value;
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
    const std::string symlink_enumeration_path =
        ConvertGuestServerPathToHostSymlinkPath(guest_enumeration_path);

    std::string filename =
        utf8::find_name_from_path(guest_enumeration_path, '/');

    std::string item_parent =
        std::filesystem::path(symlink_enumeration_path).parent_path().string();

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

    vfs::Entry* folder = kernel_state_->file_system()->ResolvePath(item_parent);

    uint32_t total_num_items = 0;

    if (folder) {
      for (const auto& child : folder->children()) {
        if (!(child->attributes() & X_FILE_ATTRIBUTE_DIRECTORY)) {
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
          if (!(entry->attributes() & X_FILE_ATTRIBUTE_DIRECTORY)) {
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

    for (uint32_t item_index = unmarshaller.StartingIndex();
         const auto entry : entries) {
      // Path must use /
      std::u16string guest_item_path;

      if (auto pos = guest_enumeration_path.rfind('/');
          pos != std::string::npos) {
        std::string guest_item_path_parent =
            guest_enumeration_path.substr(0, pos);

        guest_item_path = xe::to_utf16(
            fmt::format("{}/{}", guest_item_path_parent, entry->name()));
      }

      char16_t* item_path_ptr =
          std::to_address(items_path_name_array_ptr +
                          (item_index * (X_ONLINE_MAX_PATHNAME_LENGTH)));

      uint32_t item_path_address = memory_->HostToGuestVirtual(item_path_ptr);

      xe::string_util::copy_and_swap_truncating(
          item_path_ptr, guest_item_path.c_str(), X_ONLINE_MAX_PATHNAME_LENGTH);

      items_array_ptr[item_index].path_name =
          static_cast<uint32_t>(guest_item_path.size());
      items_array_ptr[item_index].path_name_ptr = item_path_address;

      const uint32_t size_bytes = static_cast<uint32_t>(entry->size());

      uint8_t country_id = kernel_state_->xconfig()->ReadSetting<uint8_t>(
          XCONFIG_USER_CATEGORY, XCONFIG_USER_COUNTRY);

      items_array_ptr[item_index].title_id = kernel_state_->title_id();
      items_array_ptr[item_index].title_version = title_version.value;
      items_array_ptr[item_index].owner_puid = 0;
      items_array_ptr[item_index].country_id = country_id;
      items_array_ptr[item_index].content_type =
          static_cast<uint32_t>(XContentType::kStorageDownload);
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
      results_ptr->num_items_returned.get(), unmarshaller.StartingIndex(),
      unmarshaller.MaxResultsToReturn(), guest_enumeration_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStringVerify(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStringVerifyUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller.ZeroResults();

  STRING_VERIFY_RESPONSE* responses_ptr =
      unmarshaller.Results<STRING_VERIFY_RESPONSE>();

  for (auto const& string_to_verify : unmarshaller.StringToVerify()) {
    XELOGI("{}: {}", __func__, string_to_verify);
  }

  uint32_t response_result_address =
      memory_->HostToGuestVirtual(std::to_address(responses_ptr + 1));

  X_HRESULT* response_results_ptr =
      memory_->TranslateVirtual<X_HRESULT*>(response_result_address);

  for (uint32_t i = 0; i < unmarshaller.NumStrings(); i++) {
    response_results_ptr[i] = X_E_SUCCESS;
  }

  responses_ptr->num_strings = unmarshaller.NumStrings();
  responses_ptr->string_result_ptr = response_result_address;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XUserEstimateRankForRating(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XUserEstimateRankForRatingUnmarshaller unmarshaller(kernel_state_,
                                                      buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller.ZeroResults();

  const uint32_t max_num_ranks_results =
      (unmarshaller.GetAsyncTask().GetXLiveAsyncTask()->results_size -
       sizeof(X_USER_ESTIMATE_RANK_RESULTS)) /
      sizeof(uint32_t);

  X_USER_ESTIMATE_RANK_RESULTS* estimate_rank_results_ptr =
      unmarshaller.Results<X_USER_ESTIMATE_RANK_RESULTS>();

  for (const auto& rank_estimate : unmarshaller.StatsEstimateRanks()) {
    const auto stats_view =
        kernel_state_->emulator()->game_info_database()->GetStatsView(
            rank_estimate.view_id);

    // Read the leaderboard to determine if clip should be uploaded?
    // XUserReadStats()
  }

  xe::be<uint32_t>* ranks =
      reinterpret_cast<xe::be<uint32_t>*>(estimate_rank_results_ptr + 1);

  // 58410889 expects ptr even if num_ranks is 0.
  estimate_rank_results_ptr->ranks_ptr =
      memory_->HostToGuestVirtual(std::to_address(ranks));

  // XPROPERTY_ATTACHMENT_SIZE?

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageDelete(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageDeleteUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  const std::string guest_item_path = xe::to_utf8(unmarshaller.ServerPath());
  const std::string host_item_path_base =
      ConvertGuestServerPathToHostServerPath(guest_item_path);

  X_STATUS result = X_E_FAIL;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromGuestServerPath(guest_item_path);

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
    const std::string host_item_path =
        kernel_state_->GetXboxLiveAPI()->BuildEndpoint(
            fmt::format("xstorage/{}", host_item_path_base));

    bool deleted =
        kernel_state_->GetXboxLiveAPI()->XStorageDelete(host_item_path);

    result = deleted ? X_E_SUCCESS : X_E_FAIL;
  }

  if (!route_backend || result) {
    const std::string symlink_item_path =
        ConvertGuestServerPathToHostSymlinkPath(guest_item_path);

    vfs::Entry* storage_item =
        kernel_state_->file_system()->ResolvePath(symlink_item_path);

    if (storage_item) {
      result = storage_item->Delete() ? X_E_SUCCESS : X_E_FAIL;
    } else {
      result = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
    }
  }

  XELOGI("{}: {}", __func__, guest_item_path);

  return result;
}

// XStorageDownloadToMemory:
// 41560817, 545107D1 (TU0), 4D5307D6
//
// Expects X_ONLINE_E_STORAGE_FILE_NOT_FOUND as extended error and
// X_ERROR_FUNCTION_FAILED as result.
X_HRESULT XLiveBaseApp::XStorageDownloadToMemory(uint32_t buffer_ptr,
                                                 uint32_t* extended_error) {
  // 41560817, 513107D5, 513107D9, 415607DD, 415607DD

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageDownloadToMemoryUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  unmarshaller.ZeroResults();

  X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS* download_results_ptr =
      unmarshaller.Results<X_STORAGE_DOWNLOAD_TO_MEMORY_RESULTS>();

  std::span<uint8_t> download_buffer = unmarshaller.GetDownloadBuffer();

  const std::string guest_item_to_download =
      xe::to_utf8(unmarshaller.ServerPath());
  const std::string host_item_to_download_base =
      ConvertGuestServerPathToHostServerPath(guest_item_to_download);

  X_STATUS result = X_E_SUCCESS;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromGuestServerPath(guest_item_to_download);

  // 4C4107D6 expects correct XUID otherwise fails to load downloaded content.
  // Since we don't return this information in the response we can try and
  // extract it locally as a workaround.
  uint64_t xuid_owner = 0;

  const std::optional<std::smatch> parsed_path =
      ParseGuestServerPath(guest_item_to_download);

  switch (facility_type) {
    case FACILITY_GAME_CLIP: {
      if (parsed_path.has_value()) {
        xuid_owner = xe::string_util::from_string<uint64_t>(
            parsed_path.value()[1].str(), true);
      }
    } break;
    case FACILITY_PER_TITLE: {
      // What XUID would be used here?
    } break;
    case FACILITY_PER_USER_TITLE: {
      if (parsed_path.has_value()) {
        xuid_owner = xe::string_util::from_string<uint64_t>(
            parsed_path.value()[2].str(), true);
      }
    } break;
    default: {
      // What XUID would be used for other content?
    } break;
  }

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    const std::string host_item_to_download_path =
        kernel_state_->GetXboxLiveAPI()->BuildEndpoint(
            fmt::format("xstorage/{}", host_item_to_download_base));

    const std::vector<uint8_t> buffer =
        kernel_state_->GetXboxLiveAPI()->XStorageDownload(
            host_item_to_download_path);

    if (!buffer.empty()) {
      if (buffer.size() > download_buffer.size_bytes()) {
        XELOGI("{}: Provided file size {}b is larger than expected {}b",
               __func__, buffer.size(), download_buffer.size_bytes());
        result = X_ERROR_FUNCTION_FAILED;

        if (extended_error) {
          *extended_error = X_E_INSUFFICIENT_BUFFER;
        }

        return result;
      }

      std::memcpy(download_buffer.data(), buffer.data(), buffer.size());

      // Possible solution: Use custom HTTP header or encode binary in base64
      // with metadata
      download_results_ptr->xuid_owner = xuid_owner;
      download_results_ptr->bytes_total = static_cast<uint32_t>(buffer.size());
      download_results_ptr->ft_created = time(0);

      result = X_E_SUCCESS;
    } else {
      result = X_ERROR_FUNCTION_FAILED;

      if (extended_error) {
        *extended_error = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
      }
    }
  }

  if (!route_backend || result) {
    const std::string symlink_item_to_download_path =
        ConvertGuestServerPathToHostSymlinkPath(guest_item_to_download);

    std::string filename =
        utf8::find_name_from_path(guest_item_to_download, '/');

    xe::vfs::File* output_file;
    xe::vfs::FileAction action = {};

    X_STATUS open_result = kernel_state_->file_system()->OpenFile(
        nullptr, symlink_item_to_download_path, xe::vfs::FileDisposition::kOpen,
        xe::vfs::FileAccess::kFileReadData, false, true, &output_file, &action);

    if (!open_result) {
      std::vector<uint8_t> file_data =
          std::vector<uint8_t>(output_file->entry()->size());

      size_t bytes_read = 0;
      open_result = output_file->ReadSync(
          {file_data.data(), output_file->entry()->size()}, 0, &bytes_read);

      if (!open_result) {
        if (bytes_read > unmarshaller.BufferSize()) {
          XELOGI("{}: Provided file size {}b is larger than expected {}b",
                 __func__, bytes_read, unmarshaller.BufferSize());
          result = X_ERROR_FUNCTION_FAILED;

          if (extended_error) {
            *extended_error = X_E_INSUFFICIENT_BUFFER;
          }

          return result;
        }

        std::memcpy(download_buffer.data(), file_data.data(), bytes_read);

        download_results_ptr->xuid_owner = xuid_owner;
        download_results_ptr->bytes_total = static_cast<uint32_t>(bytes_read);
        download_results_ptr->ft_created =
            output_file->entry()->create_timestamp();

        result = X_E_SUCCESS;
      } else {
        XELOGI("{}: Failed to download: {}", __func__, filename);
        result = X_ERROR_FUNCTION_FAILED;

        if (extended_error) {
          *extended_error = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
        }
      }

      output_file->Destroy();
    } else {
      XELOGI("{}: {} doesn't exist!", __func__, filename);
      result = X_ERROR_FUNCTION_FAILED;

      if (extended_error) {
        *extended_error = X_ONLINE_E_STORAGE_FILE_NOT_FOUND;
      }
    }
  }

  XELOGI("{}: Downloaded Bytes: {}b, Buffer Size: {}b, Server Path: {}",
         __func__, download_results_ptr->bytes_total.get(),
         unmarshaller.BufferSize(), guest_item_to_download);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageUploadFromMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageUploadToMemoryUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  const auto user_profile =
      kernel_state_->xam_state()->GetUserProfile(unmarshaller.UserIndex());

  // TODO:
  // We should include content owner XUID during upload so it's available for
  // download.
  const uint64_t xuid_owner = user_profile ? user_profile->GetOnlineXUID() : 0;

  std::span<uint8_t> upload_buffer = unmarshaller.GetUploadBuffer();

  const std::string guest_upload_file_path =
      xe::to_utf8(unmarshaller.ServerPath());
  const std::string host_upload_file_path_base =
      ConvertGuestServerPathToHostServerPath(guest_upload_file_path);

  std::string filename =
      utf8::find_name_from_path(host_upload_file_path_base, '/');

  X_STATUS result = X_E_FAIL;

  X_STORAGE_FACILITY facility_type =
      GetStorageFacilityTypeFromGuestServerPath(guest_upload_file_path);

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       facility_type != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    const std::string host_upload_file_path =
        kernel_state_->GetXboxLiveAPI()->BuildEndpoint(
            fmt::format("xstorage/{}", host_upload_file_path_base));

    X_STORAGE_UPLOAD_RESULT uploaded_result =
        kernel_state_->GetXboxLiveAPI()->XStorageUpload(host_upload_file_path,
                                                        upload_buffer);

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
    const std::string symlink_upload_file_path =
        ConvertGuestServerPathToHostSymlinkPath(guest_upload_file_path);

    std::string upload_file_path_parent =
        std::filesystem::path(symlink_upload_file_path).parent_path().string();

    // Check if entry exists
    vfs::Entry* entry =
        kernel_state_->file_system()->ResolvePath(upload_file_path_parent);

    if (entry) {
      vfs::File* upload_file = nullptr;
      vfs::FileAction action;

      result = kernel_state_->file_system()->OpenFile(
          nullptr, symlink_upload_file_path, vfs::FileDisposition::kOverwriteIf,
          vfs::FileAccess::kGenericWrite, false, true, &upload_file, &action);

      if (!result) {
        size_t bytes_written = 0;
        result = upload_file->WriteSync(
            {upload_buffer.data(), unmarshaller.BufferSize()}, 0,
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

  XELOGI("{}: Size: {}b, Path: {}", __func__, unmarshaller.BufferSize(),
         guest_upload_file_path);

  return result;
}

X_HRESULT XLiveBaseApp::XStorageBuildServerPath(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  X_STORAGE_BUILD_SERVER_PATH* args =
      memory_->TranslateVirtual<X_STORAGE_BUILD_SERVER_PATH*>(buffer_ptr);

  if (args->user_index >= XUserMaxUserCount &&
      args->user_index != XUserIndexNone) {
    return X_E_INVALIDARG;
  }

  uint64_t xuid = 0;

  if (args->user_index == XUserIndexNone) {
    xuid = args->xuid;

    if (!xuid) {
      return X_E_INVALIDARG;
    }
  } else {
    xuid = kernel_state_->xam_state()
               ->GetUserProfile(args->user_index.get())
               ->GetOnlineXUID();
  }

  if (!args->server_path_length_ptr) {
    return X_E_INVALIDARG;
  }

  auto read_filename = [args, this]() -> std::string {
    if (!args->file_name_ptr) {
      return "";
    }

    char16_t* filename_ptr =
        memory_->TranslateVirtual<char16_t*>(args->file_name_ptr);

    std::u16string filename =
        xe::string_util::read_u16string_and_swap(filename_ptr);
    std::string filename_str = xe::to_utf8(filename);

    return filename_str;
  };

  // Return path in guest format and use our own host path format for backend
  // and symlink.
  std::u16string guest_path;
  std::string host_path_base;

  std::string storage_type;

  X_STORAGE_FACILITY_INFO_GAME_CLIP* game_clip_info_ptr = nullptr;

  switch (args->storage_location) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      if (!args->storage_location_info_ptr ||
          args->storage_location_info_size !=
              sizeof(X_STORAGE_FACILITY_INFO_GAME_CLIP)) {
        return X_E_INVALIDARG;
      }

      game_clip_info_ptr =
          kernel_state_->memory()
              ->TranslateVirtual<X_STORAGE_FACILITY_INFO_GAME_CLIP*>(
                  args->storage_location_info_ptr);

      // Filename can be provided but we don't use it for clips because it can
      // be empty.
      const std::string filename = read_filename();

      XELOGI("{}: Leaderboard ID: {:08X}", __func__,
             game_clip_info_ptr->leaderboard_id.get());

      uint32_t view_id = game_clip_info_ptr->leaderboard_id;

      const auto stats_view =
          kernel_state_->emulator()->game_info_database()->GetStatsView(
              view_id);

      guest_path =
          to_utf16(fmt::format("//xestats/u:{:016x}/{:08x}/{:08x}", xuid,
                               kernel_state_->title_id(), view_id));

      host_path_base = fmt::format("clips/title/{:08X}/{:016X}/{:08X}",
                                   kernel_state_->title_id(), xuid, view_id);

      storage_type = "Game Clip";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      if (args->storage_location_info_ptr || args->storage_location_info_size) {
        return X_E_INVALIDARG;
      }

      const std::string filename = read_filename();

      if (filename.empty()) {
        return X_E_INVALIDARG;
      }

      guest_path = to_utf16(fmt::format("//title.{:08x}/t:{:08x}/{}",
                                        kernel_state_->title_id(),
                                        kernel_state_->title_id(), filename));

      host_path_base =
          fmt::format("title/{:08X}/{}", kernel_state_->title_id(), filename);

      storage_type = "Per Title";
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      if (args->storage_location_info_ptr || args->storage_location_info_size) {
        return X_E_INVALIDARG;
      }

      const std::string filename = read_filename();

      if (filename.empty()) {
        return X_E_INVALIDARG;
      }

      guest_path = to_utf16(fmt::format("//tuser.{:08x}/u:{:016x}/{:08x}/{}",
                                        kernel_state_->title_id(), xuid,
                                        kernel_state_->title_id(), filename));

      host_path_base = fmt::format("user/{:016X}/title/{:08X}/{}", xuid,
                                   kernel_state_->title_id(), filename);

      storage_type = "Per User Title";
    } break;
    default:
      return X_ONLINE_E_STORAGE_INVALID_FACILITY;
  }

  uint32_t server_path_length = 0;

  if (args->server_path_ptr) {
    const size_t size_bytes = xe::string_util::size_in_bytes(guest_path, true);

    // Ensure server path buffer has enough space
    if (args->server_path_length_ptr) {
      server_path_length = xe::load_and_swap<uint32_t>(
          memory_->TranslateVirtual(args->server_path_length_ptr));

      if (server_path_length < size_bytes) {
        return X_E_INVALIDARG;
      }
    }

    char16_t* server_path_ptr =
        memory_->TranslateVirtual<char16_t*>(args->server_path_ptr);

    xe::string_util::copy_and_swap_truncating(
        server_path_ptr, guest_path.data(), server_path_length);
  }

  if (args->server_path_length_ptr) {
    server_path_length = guest_path.size() + 1;

    xe::store_and_swap<uint32_t>(
        memory_->TranslateVirtual(args->server_path_length_ptr),
        server_path_length);
  }

  X_STATUS result = X_E_SUCCESS;

  bool route_backend =
      cvars::xstorage_backend &&
      (cvars::xstorage_user_data_backend ||
       args->storage_location != X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE);

  if (route_backend) {
    const std::string host_server_path =
        kernel_state_->GetXboxLiveAPI()->BuildEndpoint(
            fmt::format("xstorage/{}", host_path_base));

    const std::string valid_backend_path =
        std::filesystem::path(host_server_path).parent_path().string();

    X_STORAGE_BUILD_SERVER_PATH_RESULT build_result =
        kernel_state_->GetXboxLiveAPI()->XStorageBuildServerPath(
            valid_backend_path);

    if (build_result == X_STORAGE_BUILD_SERVER_PATH_RESULT::Created ||
        build_result == X_STORAGE_BUILD_SERVER_PATH_RESULT::Found) {
      result = X_E_SUCCESS;
    } else {
      result = X_E_FAIL;
    }
  }

  if (!route_backend || result) {
    const std::string symlink_path =
        fmt::format("{}{}", xstorage_symboliclink,
                    utf8::fix_path_separators(host_path_base));

    const std::string valid_symlink_path =
        std::filesystem::path(symlink_path).parent_path().string();

    // Check if entry exists
    vfs::Entry* entry =
        kernel_state_->file_system()->ResolvePath(valid_symlink_path);

    if (!entry) {
      // Prepare path for splitting
      std::string starting_dir = valid_symlink_path;
      std::replace(starting_dir.begin(), starting_dir.end(), ':',
                   kGuestPathSeparator);

      const auto path_parts = xe::utf8::split_path(starting_dir);

      if (path_parts.size() > 2) {
        starting_dir = path_parts[1];
      }

      // XSTORAGE entry
      vfs::Entry* xstorage_entry =
          kernel_state_->file_system()->ResolvePath(xstorage_symboliclink);

      if (xstorage_entry) {
        // Create root entry
        vfs::Entry* dir_entry = xstorage_entry->CreateEntry(
            starting_dir, xe::vfs::FileAttributeFlags::kFileAttributeDirectory);

        // Create child entries
        vfs::Entry* entries = kernel_state_->file_system()->CreatePath(
            valid_symlink_path,
            xe::vfs::FileAttributeFlags::kFileAttributeDirectory);

        // Update entry
        entry = kernel_state_->file_system()->ResolvePath(valid_symlink_path);

        if (entry) {
          result = X_E_SUCCESS;
          XELOGI("{}: Created Path: {}", __func__, valid_symlink_path);
        } else {
          result = X_E_FAIL;
          XELOGW("{}: Failed to create path: {}", __func__, valid_symlink_path);
        }
      }
    } else {
      result = X_E_SUCCESS;
      XELOGI("{}: Found Path: {}", __func__, valid_symlink_path);
    }
  }

  std::string desc = "Unknown Facility";

  switch (args->storage_location) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP:
      if (game_clip_info_ptr) {
        desc = fmt::format("Clip ID: {:08X}",
                           game_clip_info_ptr->leaderboard_id.get());
      }
      break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE:
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE:
      desc = fmt::format("Filename: {}", read_filename());
      break;
  }

  XELOGI("{}: {}, Storage Type: {}", __func__, desc, storage_type);

  return result;
}

X_HRESULT XLiveBaseApp::XOnlineGetTaskProgress(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  X_GET_TASK_PROGRESS* task_progress =
      memory_->TranslateVirtual<X_GET_TASK_PROGRESS*>(buffer_ptr);

  XAM_OVERLAPPED* overlapped_ptr =
      memory_->TranslateVirtual<XAM_OVERLAPPED*>(task_progress->overlapped_ptr);

  uint32_t* percent_complete_ptr = nullptr;
  uint64_t* numerator_ptr = nullptr;
  uint64_t* denominator_ptr = nullptr;

  if (task_progress->percent_complete_ptr) {
    percent_complete_ptr = memory_->TranslateVirtual<uint32_t*>(
        task_progress->percent_complete_ptr);
  }

  if (task_progress->numerator_ptr) {
    numerator_ptr =
        memory_->TranslateVirtual<uint64_t*>(task_progress->numerator_ptr);
  }

  if (task_progress->denominator_ptr) {
    denominator_ptr =
        memory_->TranslateVirtual<uint64_t*>(task_progress->denominator_ptr);
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
      memory_->TranslateVirtual<XLIVEBASE_GET_SEQUENCE*>(buffer_ptr);

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

  XUserFindUsersUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  // Fixed 58410B5D
  unmarshaller.ZeroResults();

  FIND_USERS_RESPONSE* results_ptr =
      unmarshaller.Results<FIND_USERS_RESPONSE>();

  std::vector<FIND_USER_INFO> find_users = {};
  std::vector<FIND_USER_INFO> resolved_users = {};

  for (auto const& user : unmarshaller.Users()) {
    const uint64_t xuid = xe::byte_swap(user.xuid);

    const auto user_profile =
        kernel_state_->xam_state()->GetUserProfileLive(xuid);

    // Only lookup non-local users
    if (user_profile) {
      FIND_USER_INFO local_user = user;

      local_user.xuid = xuid;
      xe::string_util::copy_truncating(local_user.gamertag,
                                       user_profile->name().c_str(),
                                       sizeof(local_user.gamertag));

      resolved_users.push_back(local_user);
    } else if (xuid != 0) {
      find_users.push_back(user);
    }
  }

  if (!find_users.empty()) {
    auto resolved = kernel_state_->GetXboxLiveAPI()
                        ->GetFindUsers(find_users)
                        ->GetResolvedUsers();

    resolved_users.insert(resolved_users.end(), resolved.begin(),
                          resolved.end());
  }

  uint32_t results_size = sizeof(FIND_USERS_RESPONSE) +
                          (unmarshaller.NumUsers() * sizeof(FIND_USER_INFO));

  uint32_t users_address =
      memory_->HostToGuestVirtual(std::to_address(results_ptr + 1));

  FIND_USER_INFO* user_results_ptr =
      memory_->TranslateVirtual<FIND_USER_INFO*>(users_address);

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
      kernel_state_->memory()
          ->TranslateVirtual<X_CONTENT_GET_MARKETPLACE_COUNTS*>(buffer_ptr);

  X_OFFERING_CONTENTAVAILABLE_RESULT* results_ptr =
      kernel_state_->memory()
          ->TranslateVirtual<X_OFFERING_CONTENTAVAILABLE_RESULT*>(
              marketplace_counts_ptr->results_ptr);

  memset(results_ptr, 0, sizeof(X_OFFERING_CONTENTAVAILABLE_RESULT));

  const auto user_profile = kernel_state_->xam_state()->GetUserProfile(
      marketplace_counts_ptr->user_index);

  XELOGI("{}: Content Categories: {:08X}", __func__,
         marketplace_counts_ptr->content_categories.get());

  return X_E_SUCCESS;
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

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  XACCOUNT_GET_POINTS_BALANCE_REQUEST* points_balance_request_ptr =
      unmarshaller
          .DeserializeReinterpret<XACCOUNT_GET_POINTS_BALANCE_REQUEST>();

  if (!points_balance_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  X_GET_POINTS_BALANCE_RESPONSE* points_balance_results_ptr =
      unmarshaller.Results<X_GET_POINTS_BALANCE_RESPONSE>();

  points_balance_results_ptr->balance = 1000000000;
  points_balance_results_ptr->dmp_account_status =
      DMP_STATUS_TYPE::DMP_STATUS_ACTIVE;
  points_balance_results_ptr->response_flags =
      GET_POINTS_BALANCE_RESPONSE_FLAGS::ABOVE_LOW_BALANCE;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XGetBannerList(uint32_t buffer_ptr) {
  // Called on startup of blades dashboard v1888 to v2858
  // Address: 92433DA8

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  GET_BANNER_LIST_REQUEST* banner_list_request_ptr =
      unmarshaller.DeserializeReinterpret<GET_BANNER_LIST_REQUEST>();

  if (!banner_list_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  GET_BANNER_LIST_RESPONSE* banner_list_results_ptr =
      unmarshaller.Results<GET_BANNER_LIST_RESPONSE>();

  banner_list_results_ptr->banner_count_total = 5;
  banner_list_results_ptr->banner_count = 0;
  banner_list_results_ptr->banner_list = 0;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XGetBannerListHot(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace Featured Downloads.
  // Address: 92433BB0

  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  GET_BANNER_LIST_REQUEST* banner_list_request =
      unmarshaller.DeserializeReinterpret<GET_BANNER_LIST_REQUEST>();

  if (!banner_list_request) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  GET_BANNER_LIST_RESPONSE* banner_list_results_ptr =
      unmarshaller.Results<GET_BANNER_LIST_RESPONSE>();

  banner_list_results_ptr->banner_count_total = 5;
  banner_list_results_ptr->banner_count = 0xFFFF;
  banner_list_results_ptr->banner_list = 0xFF;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XOfferingContentEnumerate(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace sub menus:
  // All New Demos and Trailers
  // More Videos and Downloads
  // Address: 92433FA8

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  CONTENT_ENUMERATE_REQUEST* content_enumerate_request_ptr =
      unmarshaller.DeserializeReinterpret<CONTENT_ENUMERATE_REQUEST>();

  if (!content_enumerate_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  // Results Layout
  // CONTENT_ENUMERATE_RESPONSE[]
  // CONTENT_INFO[]
  // char16_t[] (Content Names)
  CONTENT_ENUMERATE_RESPONSE* content_enumerate_results_ptr =
      unmarshaller.Results<CONTENT_ENUMERATE_RESPONSE>();

  CONTENT_INFO* content_info_ptr =
      reinterpret_cast<CONTENT_INFO*>(content_enumerate_results_ptr + 1);

  uint32_t content_info_address =
      memory_->HostToGuestVirtual(std::to_address(content_info_ptr));

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

    content_info_ptr->offer_name =
        memory_->HostToGuestVirtual(std::to_address(content_names_ptr));
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

X_HRESULT XLiveBaseApp::XGenresEnumerate(uint32_t buffer_ptr) {
  // Fixes accessing marketplace Game Downloads->All Games->Xbox Live Arcade
  // sub menu.
  // Address: 92434218

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  GENRES_ENUMERATE_REQUEST* genre_enumerate_request_ptr =
      unmarshaller.DeserializeReinterpret<GENRES_ENUMERATE_REQUEST>();

  if (!genre_enumerate_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  // Add max string length?
  const uint32_t total_genre_info_size =
      sizeof(GENRE_INFO) * genre_enumerate_request_ptr->max_count;

  assert_true(unmarshaller.GetAsyncTask().GetXLiveAsyncTask()->results_size >
              total_genre_info_size);

  // Results Layout
  // GENRES_ENUMERATE_RESPONSE[]
  // GENRE_INFO[]
  // char16_t[] (Genres Names)
  GENRES_ENUMERATE_RESPONSE* genre_enumerate_results_ptr =
      unmarshaller.Results<GENRES_ENUMERATE_RESPONSE>();

  GENRE_INFO* genre_info_ptr =
      reinterpret_cast<GENRE_INFO*>(genre_enumerate_results_ptr + 1);

  uint32_t genre_info_address =
      memory_->HostToGuestVirtual(std::to_address(genre_info_ptr));

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
        memory_->HostToGuestVirtual(std::to_address(genre_names_ptr));
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

X_HRESULT XLiveBaseApp::XEnumerateTitlesByFilter(uint32_t buffer_ptr) {
  // Blades Dashboard v1888

  // Fixes accessing marketplace Game Downloads.
  // Address: 92434468

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  ENUMERATE_TITLES_BY_FILTER* enumerate_titles_request_ptr =
      unmarshaller.DeserializeReinterpret<ENUMERATE_TITLES_BY_FILTER>();

  if (!enumerate_titles_request_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

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
      unmarshaller.Results<ENUMERATE_TITLES_BY_FILTER_RESPONSE>();

  ENUMERATE_TITLES_INFO* title_info_ptr =
      reinterpret_cast<ENUMERATE_TITLES_INFO*>(enumerate_titles_results_ptr +
                                               1);

  // Add max string length?
  const uint32_t enumerate_titles_info_size =
      sizeof(ENUMERATE_TITLES_INFO) * enumerate_titles_request_ptr->max_count;

  assert_true(unmarshaller.GetAsyncTask().GetXLiveAsyncTask()->results_size >
              enumerate_titles_info_size);

  uint32_t title_info_address =
      memory_->HostToGuestVirtual(std::to_address(title_info_ptr));

  uint32_t total_titles_count = 0;
  uint32_t returned_titles_count = 0;

  if (enumerate_titles_request_ptr->request_flags &
      static_cast<uint16_t>(ENUMERATE_TITLES_BY_FILTER_FLAGS::Played)) {
    const std::vector<TitleInfo> played_titles =
        kernel_state_->xam_state()->user_tracker()->GetPlayedTitles(
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
      title_info_ptr->title_name =
          memory_->HostToGuestVirtual(std::to_address(titles_names_ptr));
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

X_HRESULT XLiveBaseApp::XOfferingSubscriptionEnumerate(uint32_t buffer_ptr) {
  // Fixes accessing marketplace Memberships.
  // Address: 924346C0

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  SUBSCRIPTION_ENUMERATE_REQUEST* subscription_enumerate_ptr =
      unmarshaller.DeserializeReinterpret<SUBSCRIPTION_ENUMERATE_REQUEST>();

  if (!subscription_enumerate_ptr) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

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

  assert_true(unmarshaller.GetAsyncTask().GetXLiveAsyncTask()->results_size >
              subscription_info_size);

  SUBSCRIPTION_ENUMERATE_RESPONSE* subscription_enumerate_results_ptr =
      unmarshaller.Results<SUBSCRIPTION_ENUMERATE_RESPONSE>();

  SUBSCRIPTION_INFO* subscriptions_info_ptr =
      reinterpret_cast<SUBSCRIPTION_INFO*>(subscription_enumerate_results_ptr +
                                           1);

  uint32_t subscriptions_info_address =
      memory_->HostToGuestVirtual(std::to_address(subscriptions_info_ptr));

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
        memory_->HostToGuestVirtual(std::to_address(offer_names_ptr));
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

X_HRESULT XLiveBaseApp::XUserValidateAvatarManifest(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  X_HRESULT deserialize_result = unmarshaller.Deserialize();

  if (deserialize_result) {
    return deserialize_result;
  }

  X_VALIDATE_AVATAR_MANIFEST_RESULT* valiate_avatar_manifest =
      unmarshaller.Results<X_VALIDATE_AVATAR_MANIFEST_RESULT>();

  unmarshaller.ZeroResults();

  valiate_avatar_manifest->ValidationResult = true;

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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 3);

  XLIVEBASE_MESSAGES_ENUMERATOR* entry =
      memory_->TranslateVirtual<XLIVEBASE_MESSAGES_ENUMERATOR*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(memory_->TranslateVirtual(
      static_cast<uint32_t>(entry->xuid.argument_value_ptr)));
  auto messages_count = memory_->TranslateVirtual<xe::be<uint32_t>*>(
      static_cast<uint32_t>(entry->messages_count_ptr.argument_value_ptr));
  X_MESSAGE_SUMMARY* message_summaries =
      memory_->TranslateVirtual<X_MESSAGE_SUMMARY*>(static_cast<uint32_t>(
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

  XLivebaseAsyncTask async_task(kernel_state_, buffer_ptr);

  const X_ARGUMENT_LIST* args_list =
      memory_->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_length);

  assert_false(args_list->argument_count != 3);

  const XLIVEBASE_PRESENCE_GET_STATE* entry =
      memory_->TranslateVirtual<XLIVEBASE_PRESENCE_GET_STATE*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(memory_->TranslateVirtual(
      static_cast<uint32_t>(entry->xuid.argument_value_ptr)));
  auto state_flags_ptr = memory_->TranslateVirtual<xe::be<uint32_t>*>(
      static_cast<uint32_t>(entry->state_flags_ptr.argument_value_ptr));
  auto session_id_ptr = memory_->TranslateVirtual<XNKID*>(
      static_cast<uint32_t>(entry->session_id_ptr.argument_value_ptr));

  *state_flags_ptr = 0;
  *session_id_ptr = {};

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XOnlineCallWebService(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XLIVEBASE_WEBSERVICETASK_CALL* data =
      memory_->TranslateVirtual<XLIVEBASE_WEBSERVICETASK_CALL*>(buffer_ptr);

  std::string uri = memory_->TranslateVirtual<char*>(data->uri);
  std::string verb = memory_->TranslateVirtual<char*>(data->verb);
  std::string request_filter =
      memory_->TranslateVirtual<char*>(data->request_filter_ptr);
  std::string response_filter =
      memory_->TranslateVirtual<char*>(data->response_filter_ptr);
  std::string sts_relying_party_id =
      memory_->TranslateVirtual<char*>(data->sts_relying_party_id_ptr);

  xe::be<uint32_t>* http_status_code =
      memory_->TranslateVirtual<xe::be<uint32_t>*>(data->http_status_code_ptr);

  *http_status_code = HTTP_STATUS_CODE::HTTP_OK;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XOnlineGetWebServiceTaskBufferSize(
    uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XLIVEBASE_WEBSERVICETASK_GETBUFFERSIZE* get_buffer_size =
      memory_->TranslateVirtual<XLIVEBASE_WEBSERVICETASK_GETBUFFERSIZE*>(
          buffer_ptr);

  std::string uri = memory_->TranslateVirtual<char*>(get_buffer_size->uri_ptr);
  std::string request_filter =
      memory_->TranslateVirtual<char*>(get_buffer_size->request_filter_ptr);
  std::string response_filter =
      memory_->TranslateVirtual<char*>(get_buffer_size->response_filter_ptr);

  xe::be<uint32_t>* task_biffer_size =
      memory_->TranslateVirtual<xe::be<uint32_t>*>(
          get_buffer_size->task_buffer_size_ptr);

  *task_biffer_size = 0;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XPassportGetMemberName(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  GenericUnmarshaller unmarshaller(kernel_state_, buffer_ptr);

  XPASSPORT_MEMBERS_NAME* members =
      unmarshaller.DeserializeReinterpret<XPASSPORT_MEMBERS_NAME>();

  if (!members) {
    return X_E_INVALIDARG;
  }

  unmarshaller.ZeroResults();

  PASSPORT_GET_MEMBER_NAME_RESPONSE* results_ptr =
      unmarshaller.Results<PASSPORT_GET_MEMBER_NAME_RESPONSE>();

  return X_E_SUCCESS;
}

#pragma region XStoragePathHelpers

X_STORAGE_FACILITY XLiveBaseApp::GetStorageFacilityTypeFromGuestServerPath(
    const std::string server_path) const {
  if (server_path.starts_with("//xestats")) {
    return X_STORAGE_FACILITY::FACILITY_GAME_CLIP;
  }

  if (server_path.starts_with("//title")) {
    return X_STORAGE_FACILITY::FACILITY_PER_TITLE;
  }

  if (server_path.starts_with("//tuser")) {
    return X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE;
  }

  // TODO:
  // Avatar Editor custom gamerpics
  // avatar/u:0009000000000000/avatarpic-l.png

  return X_STORAGE_FACILITY::FACILITY_INVALID;
}

std::optional<std::smatch> XLiveBaseApp::ParseGuestServerPath(
    const std::string& server_path) const {
  std::optional<std::smatch> match = std::nullopt;
  std::regex pattern;

  switch (GetStorageFacilityTypeFromGuestServerPath(server_path)) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      // //xestats/u:0009000000000000/ffffffff/00000000
      pattern = std::regex(R"(^//xestats/u:(.{16})/(.{8})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      // //title.ffffffff/t:ffffffff/File.bin
      pattern = std::regex(R"(^//title.(.{8})/t:(.{8})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      // //tuser.ffffffff/u:0009000000000000/ffffffff/File.bin
      pattern = std::regex(R"(^//tuser.(.{8})/u:(.{16})/(.{8})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_INVALID:
    default:
      break;
  }

  std::smatch matches;
  if (std::regex_search(server_path, matches, pattern)) {
    match = matches;
  }

  return match;
}

std::string XLiveBaseApp::ConvertGuestServerPathToHostServerPath(
    const std::string server_path) const {
  std::string host_server_path;

  const std::optional<std::smatch> parsed_guest_path =
      ParseGuestServerPath(server_path);

  if (!parsed_guest_path.has_value()) {
    return host_server_path;
  }

  const std::smatch matches = parsed_guest_path.value();

  switch (GetStorageFacilityTypeFromGuestServerPath(server_path)) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      const uint64_t xuid =
          xe::string_util::from_string<uint64_t>(matches[1].str(), true);
      const uint32_t title_id =
          xe::string_util::from_string<uint32_t>(matches[2].str(), true);
      const uint32_t leadereboard_id =
          xe::string_util::from_string<uint32_t>(matches[3].str(), true);

      host_server_path = fmt::format("clips/title/{:08X}/{:016X}/{:08X}",
                                     title_id, xuid, leadereboard_id);
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      const uint32_t title_id_1 =
          xe::string_util::from_string<uint32_t>(matches[1].str(), true);
      const uint32_t title_id_2 =
          xe::string_util::from_string<uint32_t>(matches[2].str(), true);
      const std::string filename = matches[3].str();

      host_server_path = fmt::format("title/{:08X}/{}", title_id_1, filename);
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      const uint32_t title_id_1 =
          xe::string_util::from_string<uint32_t>(matches[1].str(), true);
      const uint64_t xuid =
          xe::string_util::from_string<uint64_t>(matches[2].str(), true);
      const uint32_t title_id_2 =
          xe::string_util::from_string<uint32_t>(matches[3].str(), true);
      const std::string filename = matches[4].str();

      host_server_path = fmt::format("user/{:016X}/title/{:08X}/{}", xuid,
                                     title_id_1, filename);
    } break;
    case X_STORAGE_FACILITY::FACILITY_INVALID:
    default:
      break;
  }

  return host_server_path;
}

std::string XLiveBaseApp::ConvertGuestServerPathToHostSymlinkPath(
    const std::string server_path) const {
  const std::string host_server_path =
      ConvertGuestServerPathToHostServerPath(server_path);

  const std::string symlink_path =
      std::format("{}{}", xstorage_symboliclink,
                  utf8::fix_path_separators(host_server_path));

  return symlink_path;
}

X_STORAGE_FACILITY XLiveBaseApp::GetStorageFacilityTypeFromHostServerPath(
    const std::string server_path) const {
  if (server_path.starts_with("xstorage/clip")) {
    return X_STORAGE_FACILITY::FACILITY_GAME_CLIP;
  }

  if (server_path.starts_with("xstorage/title")) {
    return X_STORAGE_FACILITY::FACILITY_PER_TITLE;
  }

  if (server_path.starts_with("xstorage/user")) {
    return X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE;
  }

  return X_STORAGE_FACILITY::FACILITY_INVALID;
}

std::optional<std::smatch> XLiveBaseApp::ParseHostServerPath(
    const std::string& server_path) const {
  std::optional<std::smatch> match = std::nullopt;
  std::regex pattern;

  switch (GetStorageFacilityTypeFromHostServerPath(server_path)) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      // xstorage/clips/title/FFFFFFFF/0009000000000000/00000000
      pattern = std::regex(R"(^xstorage/clips/title/(.{8})/(.{16})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      // xstorage/title/FFFFFFFF/File.bin
      pattern = std::regex(R"(^xstorage/title/(.{8})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      // xstorage/user/0009000000000000/title/FFFFFFFF/File.bin
      pattern = std::regex(R"(^xstorage/user/(.{16})/title/(.{8})/(.*)$)");
    } break;
    case X_STORAGE_FACILITY::FACILITY_INVALID:
    default:
      break;
  }

  std::smatch matches;
  if (std::regex_search(server_path, matches, pattern)) {
    match = matches;
  }

  return match;
}

std::string XLiveBaseApp::ConvertHostServerPathToGuestServerPath(
    const std::string server_path) const {
  std::string guest_server_path;

  const std::optional<std::smatch> parsed_guest_path =
      ParseHostServerPath(server_path);

  if (!parsed_guest_path.has_value()) {
    return guest_server_path;
  }

  const std::smatch matches = parsed_guest_path.value();

  switch (GetStorageFacilityTypeFromHostServerPath(server_path)) {
    case X_STORAGE_FACILITY::FACILITY_GAME_CLIP: {
      const uint32_t title_id =
          xe::string_util::from_string<uint32_t>(matches[1].str(), true);
      const uint64_t xuid =
          xe::string_util::from_string<uint64_t>(matches[2].str(), true);
      const uint32_t view_id =
          xe::string_util::from_string<uint32_t>(matches[3].str(), true);

      guest_server_path = fmt::format("//xestats/u:{:016x}/{:08x}/{:08x}", xuid,
                                      title_id, view_id);
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_TITLE: {
      const uint32_t title_id =
          xe::string_util::from_string<uint32_t>(matches[1].str(), true);
      const std::string filename = matches[2].str();

      guest_server_path = fmt::format("//title.{:08x}/t:{:08x}/{}", title_id,
                                      title_id, filename);
    } break;
    case X_STORAGE_FACILITY::FACILITY_PER_USER_TITLE: {
      const uint64_t xuid =
          xe::string_util::from_string<uint64_t>(matches[1].str(), true);
      const uint32_t title_id =
          xe::string_util::from_string<uint32_t>(matches[2].str(), true);
      const std::string filename = matches[3].str();

      guest_server_path = fmt::format("//tuser.{:08x}/u:{:016x}/{:08x}/{}",
                                      title_id, xuid, title_id, filename);
    } break;
    case X_STORAGE_FACILITY::FACILITY_INVALID:
    default:
      break;
  }

  return guest_server_path;
}

#pragma endregion

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
