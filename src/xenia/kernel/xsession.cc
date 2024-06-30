/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xsession.h"

DECLARE_bool(upnp);

namespace xe {
namespace kernel {

void Uint64toXNKID(uint64_t sessionID, XNKID* xnkid) {
  uint64_t session_id = xe::byte_swap(sessionID);
  memcpy(xnkid->ab, &session_id, sizeof(XNKID));
}

uint64_t XNKIDtoUint64(XNKID* sessionID) {
  uint64_t session_id = 0;
  memcpy(&session_id, sessionID->ab, sizeof(XNKID));
  return xe::byte_swap(session_id);
}

XSession::XSession(KernelState* kernel_state)
    : XObject(kernel_state, Type::Session) {
  session_id_ = -1;
}

X_STATUS XSession::Initialize() {
  auto native_object = CreateNative(sizeof(X_KSESSION));
  if (!native_object) {
    return X_STATUS_NO_MEMORY;
  }

  auto guest_object = reinterpret_cast<X_KSESSION*>(native_object);
  guest_object->handle = handle();
  // Based on what is in XAM it seems like size of this object is only 4 bytes.
  return X_STATUS_SUCCESS;
}

X_RESULT XSession::CreateSession(uint8_t user_index, uint8_t public_slots,
                                 uint8_t private_slots, uint32_t flags,
                                 uint32_t session_info_ptr,
                                 uint32_t nonce_ptr) {
  if (is_session_created_) {
    // Todo: Find proper code!
    return X_ERROR_FUNCTION_FAILED;
  }

  const xam::UserProfile* user_profile =
      kernel_state_->xam_state()->GetUserProfile((uint32_t)user_index);
  if (!user_profile) {
    return X_ERROR_FUNCTION_FAILED;
  }

  XSESSION_INFO* SessionInfo_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_INFO*>(
          session_info_ptr);

  GenerateIdentityExchangeKey(&SessionInfo_ptr->keyExchangeKey);
  PrintSessionType((SessionFlags)flags);

  uint64_t* Nonce_ptr =
      kernel_state_->memory()->TranslateVirtual<uint64_t*>(nonce_ptr);
  // CSGO only uses STATS flag to create a session to POST stats pre round.
  // Minecraft and Portal 2 use flags HOST + STATS.
  //
  // Creating a session when not host can cause failure in joining sessions
  // such as L4D2 and Portal 2.
  //
  // Hexic creates a session with SINGLEPLAYER_WITH_STATS (without HOST bit)
  // with contexts
  //
  // Create presence sessions?
  // - Create when joining a session
  // - Explicitly create a presence session (Frogger without HOST bit)
  // Based on Presence flag set?
  if (flags == STATS) {
    CreateStatsSession(SessionInfo_ptr, Nonce_ptr, user_index, public_slots,
                       private_slots, flags);
  } else if (HasSessionFlag((SessionFlags)flags, HOST) ||
             flags == SINGLEPLAYER_WITH_STATS) {
    // Write user contexts. After session creation these are read only!
    contexts_.insert(user_profile->contexts_.cbegin(),
                     user_profile->contexts_.cend());

    CreateHostSession(SessionInfo_ptr, Nonce_ptr, user_index, public_slots,
                      private_slots, flags);
  } else {
    JoinExistingSession(SessionInfo_ptr);
  }

  is_session_created_ = true;
  return X_ERROR_SUCCESS;
}

void XSession::GenerateIdentityExchangeKey(XNKEY* key) {
  for (uint8_t i = 0; i < sizeof(XNKEY); i++) {
    key->ab[i] = i;
  }
}

uint64_t XSession::GenerateSessionId(uint8_t mask) {
  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist(0, -1);
  return ((uint64_t)mask << 56) | (dist(rd) & 0x0000FFFFFFFFFFFF);
}

X_RESULT XSession::CreateHostSession(XSESSION_INFO* session_info,
                                     uint64_t* nonce_ptr, uint8_t user_index,
                                     uint8_t public_slots,
                                     uint8_t private_slots, uint32_t flags) {
  if (!cvars::upnp) {
    XELOGI("Hosting while UPnP is disabled!");
  }

  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist(0, -1);
  *nonce_ptr = dist(rd);

  XSessionData* session_data = new XSessionData();
  session_data->user_index = user_index;
  session_data->num_slots_public = public_slots;
  session_data->num_slots_private = private_slots;
  session_data->flags = flags;
  session_id_ = GenerateSessionId(XNKID_ONLINE);
  Uint64toXNKID(session_id_, &session_info->sessionID);

  XLiveAPI::XSessionCreate(session_id_, session_data);

  XELOGI("Created session {:016X}", session_id_);

  XLiveAPI::SessionContextSet(session_id_, contexts_);

  XLiveAPI::IpGetConsoleXnAddr(&session_info->hostAddress);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::CreateStatsSession(XSESSION_INFO* session_info,
                                      uint64_t* nonce_ptr, uint8_t user_index,
                                      uint8_t public_slots,
                                      uint8_t private_slots, uint32_t flags) {
  return CreateHostSession(session_info, nonce_ptr, user_index, public_slots,
                           private_slots, flags);
}

X_RESULT XSession::JoinExistingSession(XSESSION_INFO* session_info) {
  session_id_ = XNKIDtoUint64(&session_info->sessionID);
  XELOGI("Joining session {:016X}", session_id_);

  assert_true(IsOnlinePeer(session_id_));

  if (session_id_ == NULL) {
    assert_always();
    return X_E_FAIL;
  }

  const std::unique_ptr<SessionObjectJSON> session =
      XLiveAPI::XSessionGet(session_id_);

  // Begin XNetRegisterKey?
  GetXnAddrFromSessionObject(session.get(), &session_info->hostAddress);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::DeleteSession() {
  // Begin XNetUnregisterKey?
  XLiveAPI::DeleteSession(session_id_);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::JoinSession(XSessionJoin* data) {
  const bool join_local = data->xuid_array_ptr == 0;
  std::string join_type =
      join_local ? "XGISessionJoinLocal" : "XGISessionJoinRemote";
  XELOGI("{}({:08X}, {}, {:08X}, {:08X}, {:08X})", join_type,
         static_cast<uint32_t>(data->obj_ptr),
         static_cast<uint32_t>(data->array_count),
         static_cast<uint32_t>(data->xuid_array_ptr),
         static_cast<uint32_t>(data->indices_array_ptr),
         static_cast<uint32_t>(data->private_slots_array_ptr));

  std::vector<std::string> xuids{};

  const auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  const auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  const auto private_slots = kernel_state_->memory()->TranslateVirtual<bool*>(
      data->private_slots_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    if (join_local) {
      const uint32_t index = (uint32_t)indices_array[i];
      const auto profile = kernel_state()->xam_state()->GetUserProfile(index);

      if (!profile) {
        assert_always();
        return X_E_FAIL;
      }

      const auto xuid = profile->xuid();

      // Convert local user index to xuid.
      xuids.push_back(string_util::to_hex_string(xuid));
    } else {
      xuids.push_back(string_util::to_hex_string(xuid_array[i]));
    }

    const bool private_slot = private_slots[i];

    if (private_slot) {
      XELOGI("Occupying private slot");
    } else {
      XELOGI("Occupying public slot");
    }
  }

  XLiveAPI::SessionJoinRemote(session_id_, xuids);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::LeaveSession(XSessionLeave* data) {
  const bool leavelocal = data->xuid_array_ptr == 0;

  std::string leave_type =
      leavelocal ? "XGISessionLeaveLocal" : "XGISessionLeaveRemote";

  XELOGI("{}({:08X}, {}, {:08X}, {:08X})", leave_type,
         static_cast<uint32_t>(data->obj_ptr),
         static_cast<uint32_t>(data->array_count),
         static_cast<uint32_t>(data->xuid_array_ptr),
         static_cast<uint32_t>(data->indices_array_ptr));

  std::vector<std::string> xuids{};

  auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    if (leavelocal) {
      const uint32_t index = (uint32_t)indices_array[i];
      const auto profile = kernel_state()->xam_state()->GetUserProfile(index);

      if (!profile) {
        assert_always();
        return X_E_FAIL;
      }

      // Convert local user index to xuid.
      xuids.push_back(string_util::to_hex_string(profile->xuid()));
    } else {
      xuids.push_back(string_util::to_hex_string(xuid_array[i]));
    }
  }

  XLiveAPI::SessionLeaveRemote(session_id_, xuids);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::ModifySession(XSessionModify* data) {
  XELOGI("Modifying session {:016X}", session_id_);
  PrintSessionType(static_cast<SessionFlags>((uint32_t)data->flags));

  XLiveAPI::SessionModify(session_id_, data);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessionDetails(XSessionDetails* data) {
  auto details_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->details_buffer);

  const std::unique_ptr<SessionObjectJSON> session =
      XLiveAPI::SessionDetails(session_id_);

  if (session->HostAddress().empty()) {
    return 1;
  }

  Uint64toXNKID(xe::byte_swap(session->SessionID_UInt()),
                &details_ptr->sessionInfo.sessionID);

  GetXnAddrFromSessionObject(session.get(),
                             &details_ptr->sessionInfo.hostAddress);

  GenerateIdentityExchangeKey(&details_ptr->sessionInfo.keyExchangeKey);

  details_ptr->UserIndexHost = 0;
  details_ptr->GameMode = 0;
  details_ptr->GameType = 0;
  details_ptr->Flags = session->Flags();
  details_ptr->MaxPublicSlots = session->PublicSlotsCount();
  details_ptr->MaxPrivateSlots = session->PrivateSlotsCount();

  // TODO:
  // Provide the correct counts.
  details_ptr->AvailablePrivateSlots = session->OpenPrivateSlotsCount();
  details_ptr->AvailablePublicSlots = session->OpenPublicSlotsCount();
  details_ptr->ActualMemberCount = session->FilledPublicSlotsCount();
  // details->ActualMemberCount =
  //     session->FilledPublicSlotsCount() + session->FilledPrivateSlotsCount();

  details_ptr->ReturnedMemberCount = (uint32_t)session->Players().size();
  details_ptr->eState = XSESSION_STATE::LOBBY;

  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint64_t> dist(0, -1);

  details_ptr->Nonce = dist(rnd);

  uint32_t members_ptr = kernel_state_->memory()->SystemHeapAlloc(
      sizeof(XSESSION_MEMBER) * details_ptr->ReturnedMemberCount);

  auto members =
      kernel_state_->memory()->TranslateVirtual<XSESSION_MEMBER*>(members_ptr);
  details_ptr->pSessionMembers = members_ptr;

  for (uint8_t i = 0; i < details_ptr->ReturnedMemberCount; i++) {
    members[i].UserIndex = 0xFE;
    members[i].xuidOnline = session->Players().at(i).XUID();
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::MigrateHost(XSessionMigate* data) {
  auto SessionInfo_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_INFO*>(
          data->session_info_ptr);

  if (!XLiveAPI::upnp_handler->is_active()) {
    XELOGI("Migrating without UPnP");
    // return X_E_FAIL;
  }

  const auto result = XLiveAPI::XSessionMigration(session_id_);

  if (!result->SessionID_UInt()) {
    XELOGI("Session Migration Failed");
    return X_E_FAIL;
  }

  memset(SessionInfo_ptr, 0, sizeof(XSESSION_INFO));

  Uint64toXNKID(result->SessionID_UInt(), &SessionInfo_ptr->sessionID);

  XLiveAPI::IpGetConsoleXnAddr(&SessionInfo_ptr->hostAddress);

  GenerateIdentityExchangeKey(&SessionInfo_ptr->keyExchangeKey);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::RegisterArbitration(XSessionArbitrationData* data) {
  XSESSION_REGISTRATION_RESULTS* results_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_REGISTRATION_RESULTS*>(
          data->results_ptr);

  const auto result = XLiveAPI::XSessionArbitration(session_id_);

  const uint32_t registrants_ptr = kernel_state_->memory()->SystemHeapAlloc(
      uint32_t(sizeof(XSESSION_REGISTRANT) * result->Machines().size()));

  results_ptr->registrants_count = uint32_t(result->Machines().size());
  results_ptr->registrants_ptr = registrants_ptr;

  XSESSION_REGISTRANT* registrants =
      kernel_state_->memory()->TranslateVirtual<XSESSION_REGISTRANT*>(
          registrants_ptr);

  for (uint8_t i = 0; i < result->Machines().size(); i++) {
    registrants[i].bTrustworthiness = 1;

    registrants[i].MachineID = result->Machines()[i].machine_id;
    registrants[i].bNumUsers = result->Machines()[i].player_count;

    const uint32_t users_ptr = kernel_state_->memory()->SystemHeapAlloc(
        sizeof(uint64_t) * registrants[i].bNumUsers);

    uint64_t* users_xuid_ptr =
        kernel_state_->memory()->TranslateVirtual<uint64_t*>(users_ptr);

    for (uint8_t j = 0; j < registrants[i].bNumUsers; j++) {
      users_xuid_ptr[j] = result->Machines()[i].xuids[j];
    }

    registrants[i].rgUsers = users_ptr;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::ModifySkill(XSessionModifySkill* data) {
  const auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    const auto& xuid = xuid_array[i];

    XELOGI("ModifySkill XUID: {:016X}", static_cast<uint64_t>(xuid));
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::WriteStats(XSessionWriteStats* data) {
  XSessionViewProperties* leaderboard =
      kernel_state_->memory()->TranslateVirtual<XSessionViewProperties*>(
          data->leaderboards_guest_address);

  XLiveAPI::SessionWriteStats(session_id_, data, leaderboard);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::StartSession(uint32_t flags) { return X_ERROR_SUCCESS; }

X_RESULT XSession::EndSession() { return X_ERROR_SUCCESS; }

X_RESULT XSession::GetSessions(Memory* memory, XSessionSearch* search_data) {
  if (!search_data->results_buffer_size) {
    search_data->results_buffer_size =
        sizeof(XSESSION_SEARCHRESULT) * search_data->num_results;
    return ERROR_INSUFFICIENT_BUFFER;
  }

  const auto sessions = XLiveAPI::SessionSearch(search_data);

  const uint32_t session_count =
      std::min<uint32_t>(search_data->num_results, (uint32_t)sessions.size());

  SEARCH_RESULTS* search_results_ptr =
      memory->TranslateVirtual<SEARCH_RESULTS*>(
          search_data->search_results_ptr);

  const uint32_t session_search_results_ptr =
      memory->SystemHeapAlloc(search_data->results_buffer_size);

  search_results_ptr->results_ptr =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT*>(
          session_search_results_ptr);

  for (uint32_t i = 0; i < session_count; i++) {
    const auto context =
        XLiveAPI::SessionContextGet(sessions.at(i)->SessionID_UInt());

    FillSessionContext(memory, context, &search_results_ptr->results_ptr[i]);
    FillSessionProperties(search_data->num_props, search_data->props_ptr,
                          &search_results_ptr->results_ptr[i]);
    FillSessionSearchResult(sessions.at(i),
                            &search_results_ptr->results_ptr[i]);
  }

  search_results_ptr->header.search_results_count = session_count;
  search_results_ptr->header.search_results_ptr = session_search_results_ptr;

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessionByID(Memory* memory,
                                  XSessionSearchID* search_data) {
  if (!search_data->results_buffer_size) {
    search_data->results_buffer_size = sizeof(XSESSION_SEARCHRESULT);
    return ERROR_INSUFFICIENT_BUFFER;
  }

  const auto session_id = XNKIDtoUint64(&search_data->session_id);

  if (!session_id) {
    assert_always();
    return X_ERROR_SUCCESS;
  }

  const auto session = XLiveAPI::XSessionGet(session_id);
  const uint32_t session_count = 1;

  SEARCH_RESULTS* search_results_ptr =
      memory->TranslateVirtual<SEARCH_RESULTS*>(
          search_data->search_results_ptr);

  const uint32_t session_search_result_ptr =
      memory->SystemHeapAlloc(search_data->results_buffer_size);

  search_results_ptr->results_ptr =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT*>(
          session_search_result_ptr);

  if (!session->HostAddress().empty()) {
    // HUH? How it should be filled in this case?
    FillSessionContext(memory, {}, &search_results_ptr->results_ptr[0]);
    FillSessionProperties(0, 0, &search_results_ptr->results_ptr[0]);
    FillSessionSearchResult(session, &search_results_ptr->results_ptr[0]);
  }

  search_results_ptr->header.search_results_count = session_count;
  search_results_ptr->header.search_results_ptr = session_search_result_ptr;

  return X_ERROR_SUCCESS;
}

void XSession::GetXnAddrFromSessionObject(SessionObjectJSON* session,
                                          XNADDR* XnAddr_ptr) {
  memset(XnAddr_ptr, 0, sizeof(XNADDR));

  // We only store online IP on server.

  // if (XLiveAPI::IsOnline()) {
  // } else {
  // }

  XnAddr_ptr->inaOnline = ip_to_in_addr(session->HostAddress());
  XnAddr_ptr->ina = ip_to_in_addr(session->HostAddress());

  const MacAddress mac = MacAddress(session->MacAddress());

  memcpy(&XnAddr_ptr->abEnet, mac.raw(), sizeof(MacAddress));
  memcpy(&XnAddr_ptr->abOnline, mac.raw(), sizeof(MacAddress));

  XnAddr_ptr->wPortOnline = session->Port();
}

void XSession::FillSessionSearchResult(
    const std::unique_ptr<SessionObjectJSON>& session,
    XSESSION_SEARCHRESULT* result) {
  result->filled_priv_slots = session->FilledPrivateSlotsCount();
  result->filled_public_slots = session->FilledPublicSlotsCount();
  result->open_priv_slots = session->OpenPrivateSlotsCount();
  result->open_public_slots = session->OpenPublicSlotsCount();

  Uint64toXNKID(session->SessionID_UInt(), &result->info.sessionID);

  GetXnAddrFromSessionObject(session.get(), &result->info.hostAddress);

  GenerateIdentityExchangeKey(&result->info.keyExchangeKey);
}

void XSession::FillSessionContext(Memory* memory,
                                  std::map<uint32_t, uint32_t> contexts,
                                  XSESSION_SEARCHRESULT* result) {
  result->contexts_count = (uint32_t)contexts.size();

  const uint32_t context_ptr = memory->SystemHeapAlloc(
      uint32_t(sizeof(XUSER_CONTEXT) * contexts.size()));

  XUSER_CONTEXT* contexts_to_get =
      memory->TranslateVirtual<XUSER_CONTEXT*>(context_ptr);

  uint32_t i = 0;
  for (const auto context : contexts) {
    contexts_to_get[i].context_id = context.first;
    contexts_to_get[i].value = context.second;
    i++;
  }

  result->contexts_ptr = context_ptr;
}

void XSession::FillSessionProperties(uint32_t properties_count,
                                     uint32_t properties_ptr,
                                     XSESSION_SEARCHRESULT* result) {
  result->properties_count = properties_count;
  result->properties_ptr = properties_ptr;
}

void XSession::PrintSessionType(SessionFlags flags) {
  std::string session_description = "";

  const std::map<SessionFlags, std::string> basic = {
      {HOST, "Host"},
      {PRESENCE, "Presence"},
      {STATS, "Stats"},
      {MATCHMAKING, "Matchmaking"},
      {ARBITRATION, "Arbitration"},
      {PEER_NETWORK, "Peer Network"},
      {SOCIAL_MATCHMAKING_ALLOWED, "Social Matchmaking"},
      {INVITES_DISABLED, "No invites"},
      {JOIN_VIA_PRESENCE_DISABLED, "Presence Join Disabled"},
      {JOIN_IN_PROGRESS_DISABLED, "In-Progress Join Disabled"},
      {JOIN_VIA_PRESENCE_FRIENDS_ONLY, "Friends Only"}};

  const std::map<SessionFlags, std::string> extended = {
      {SINGLEPLAYER_WITH_STATS, "Singleplayer with Stats"},
      {LIVE_MULTIPLAYER_STANDARD, "LIVE: Multiplayer"},
      {LIVE_MULTIPLAYER_RANKED, "LIVE: Multiplayer Ranked"},
      {GROUP_LOBBY, "Group Lobby"},
      {GROUP_GAME, "Group Game"}};

  for (const auto& entry : basic) {
    if (HasSessionFlag(flags, entry.first)) {
      session_description.append(entry.second + ", ");
    }
  }

  XELOGI("Session Description: {}", session_description);
  session_description.clear();

  for (const auto& entry : extended) {
    if (HasSessionFlag(flags, entry.first)) {
      session_description.append(entry.second + ", ");
    }
  }

  XELOGI("Session Extended Description: {}", session_description);
}

}  // namespace kernel
}  // namespace xe