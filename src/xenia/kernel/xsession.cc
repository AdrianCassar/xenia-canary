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
  if (IsCreated()) {
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

  local_details_.UserIndexHost = XUSER_INDEX_NONE;

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

  // Write user contexts. After session creation these are read only!
  contexts_.insert(user_profile->contexts_.cbegin(),
                   user_profile->contexts_.cend());

  if (flags == STATS) {
    CreateStatsSession(SessionInfo_ptr, Nonce_ptr, user_index, public_slots,
                       private_slots, flags);
  } else if (HasSessionFlag((SessionFlags)flags, HOST) ||
             flags == SINGLEPLAYER_WITH_STATS) {
    CreateHostSession(SessionInfo_ptr, Nonce_ptr, user_index, public_slots,
                      private_slots, flags);
  } else {
    JoinExistingSession(SessionInfo_ptr);
  }

  local_details_.GameType = GetGameTypeContext();
  local_details_.GameMode = GetGameModeContext();
  local_details_.Flags = flags;
  local_details_.MaxPublicSlots = public_slots;
  local_details_.MaxPrivateSlots = private_slots;
  local_details_.AvailablePublicSlots = public_slots;
  local_details_.AvailablePrivateSlots = private_slots;
  local_details_.ActualMemberCount = 0;
  local_details_.ReturnedMemberCount = 0;
  local_details_.eState = XSESSION_STATE::LOBBY;
  local_details_.Nonce = *Nonce_ptr;
  local_details_.sessionInfo = *SessionInfo_ptr;
  local_details_.xnkidArbitration = XNKID{};
  local_details_.SessionMembers_ptr = 0;

  state |= STATE_FLAGS_CREATED;

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
  state |= STATE_FLAGS_HOST;

  local_details_.UserIndexHost = user_index;

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

  if (session_id_ == 0) {
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
  state |= STATE_FLAGS_DELETED;

  // Begin XNetUnregisterKey?

  if (IsHost()) {
    XLiveAPI::DeleteSession(session_id_);
  }

  // session_id_ = 0;

  local_details_.eState = XSESSION_STATE::DELETED;
  // local_details_.sessionInfo.sessionID = XNKID{};
  return X_ERROR_SUCCESS;
}

// A member can be added by either local or remote, typically local members are
// joined via local but are often joined via remote - they're equivalent.
//
// If there are no private slots available then the member will occupy a public
// slot instead.
//
// TODO: Add player to recent player list, maybe backend responsibility.
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

  std::unordered_map<uint64_t, bool> members{};

  const auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  const auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  const auto private_slots_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->private_slots_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    XSESSION_MEMBER* member = new XSESSION_MEMBER();

    if (join_local) {
      const uint32_t user_index = static_cast<uint32_t>(indices_array[i]);

      if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
        return X_E_FAIL;
      }

      const auto profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);
      const xe::be<uint64_t> xuid = profile->xuid();

      const bool is_member_added =
          local_members_.find(xuid) != local_members_.end();

      if (is_member_added) {
        member = &local_members_[xuid];
      }

      member->OnlineXUID = xuid;
      member->UserIndex = user_index;

      local_details_.ActualMemberCount =
          std::min<int32_t>(MAX_USERS, local_details_.ActualMemberCount + 1);
    } else {
      // Default member
      const xe::be<uint64_t> xuid = xuid_array[i];
      const uint32_t user_index = XUSER_INDEX_NONE;

      const bool is_member_added =
          remote_members_.find(xuid) != remote_members_.end();

      if (is_member_added) {
        member = &remote_members_[xuid];
      }

      member->OnlineXUID = xuid;
      member->UserIndex = user_index;

      bool is_local_member = IsMemberLocallySignedIn(xuid, user_index);

      if (is_local_member) {
        const auto profile_manager =
            kernel_state()->xam_state()->profile_manager();
        member->UserIndex =
            profile_manager->GetUserIndexAssignedToProfile(xuid);

        local_details_.ActualMemberCount =
            std::min<int32_t>(MAX_USERS, local_details_.ActualMemberCount + 1);
      }
    }

    const bool is_private = private_slots_array[i];

    if (is_private && local_details_.AvailablePrivateSlots > 0) {
      member->SetPrivate();

      local_details_.AvailablePrivateSlots =
          std::max<int32_t>(0, local_details_.AvailablePrivateSlots - 1);
    } else {
      local_details_.AvailablePublicSlots =
          std::max<int32_t>(0, local_details_.AvailablePublicSlots - 1);
    }

    XELOGI("XUID: {:016X} - Occupying {} slot",
           static_cast<uint64_t>(member->OnlineXUID),
           member->IsPrivate() ? "private" : "public");

    members[member->OnlineXUID] = member->IsPrivate();

    if (join_local) {
      local_members_.emplace(member->OnlineXUID, *member);
    } else {
      remote_members_.emplace(member->OnlineXUID, *member);
    }
  }

  local_details_.ReturnedMemberCount = GetMembersCount();

  if (!members.empty() && IsHost()) {
    XLiveAPI::SessionJoinRemote(session_id_, members);
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::LeaveSession(XSessionLeave* data) {
  const bool leave_local = data->xuid_array_ptr == 0;

  std::string leave_type =
      leave_local ? "XGISessionLeaveLocal" : "XGISessionLeaveRemote";

  XELOGI("{}({:08X}, {}, {:08X}, {:08X})", leave_type,
         static_cast<uint32_t>(data->obj_ptr),
         static_cast<uint32_t>(data->array_count),
         static_cast<uint32_t>(data->xuid_array_ptr),
         static_cast<uint32_t>(data->indices_array_ptr));

  // Server already knows slots types from joining so we only need to send
  // xuids.
  std::vector<xe::be<uint64_t>> xuids{};

  auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  bool is_arbitrated = HasSessionFlag(
      static_cast<SessionFlags>((uint32_t)local_details_.Flags), ARBITRATION);

  for (uint32_t i = 0; i < data->array_count; i++) {
    XSESSION_MEMBER* member = new XSESSION_MEMBER();

    if (leave_local) {
      const uint32_t user_index = static_cast<uint32_t>(indices_array[i]);

      if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
        return X_E_FAIL;
      }

      const auto profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);
      const xe::be<uint64_t> xuid = profile->xuid();

      const bool is_member_added =
          local_members_.find(xuid) != local_members_.end();

      if (!is_member_added) {
        return X_ERROR_SUCCESS;
      }

      member = &local_members_[xuid];
    } else {
      const xe::be<uint64_t> xuid = xuid_array[i];

      const bool is_member_added =
          remote_members_.find(xuid) != remote_members_.end();

      if (!is_member_added) {
        return X_ERROR_SUCCESS;
      }

      member = &remote_members_[xuid];
    }

    if (member->IsPrivate()) {
      // Removing a private member but all members are removed
      assert_false(local_details_.AvailablePrivateSlots ==
                   local_details_.MaxPrivateSlots);

      local_details_.AvailablePrivateSlots =
          std::min<int32_t>(local_details_.MaxPrivateSlots,
                            local_details_.AvailablePrivateSlots + 1);
    } else {
      // Removing a public member but all members are removed
      assert_false(local_details_.AvailablePublicSlots ==
                   local_details_.MaxPublicSlots);

      local_details_.AvailablePublicSlots =
          std::min<int32_t>(local_details_.MaxPublicSlots,
                            local_details_.AvailablePublicSlots + 1);
    }

    // Keep arbitrated session members for stats reporting
    if (is_arbitrated) {
      member->SetZombie();
    }

    if (!member->IsZombie()) {
      bool removed = false;

      XELOGI("XUID: {:016X} - Leaving {} slot",
             static_cast<uint64_t>(member->OnlineXUID),
             member->IsPrivate() ? "private" : "public");

      const xe::be<uint64_t> xuid = member->OnlineXUID;

      if (leave_local) {
        removed = local_members_.erase(member->OnlineXUID);
      } else {
        removed = remote_members_.erase(member->OnlineXUID);
      }

      assert_true(removed);

      if (removed) {
        xuids.push_back(xuid);

        bool is_local_member =
            IsMemberLocallySignedIn(member->OnlineXUID, member->UserIndex);

        if (is_local_member) {
          local_details_.ActualMemberCount =
              std::max<int32_t>(0, local_details_.ActualMemberCount - 1);
        }
      }
    }
  }

  local_details_.ReturnedMemberCount = GetMembersCount();

  if (!xuids.empty() && IsHost()) {
    XLiveAPI::SessionLeaveRemote(session_id_, xuids);
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::ModifySession(XSessionModify* data) {
  XELOGI("Modifying session {:016X}", session_id_);
  PrintSessionType(static_cast<SessionFlags>((uint32_t)data->flags));

  local_details_.Flags = data->flags;

  const uint32_t num_private_slots = std::max<int32_t>(
      0, local_details_.MaxPrivateSlots - local_details_.AvailablePrivateSlots);

  const uint32_t num_public_slots = std::max<int32_t>(
      0, local_details_.MaxPublicSlots - local_details_.AvailablePublicSlots);

  data->maxPrivateSlots = std::max<int32_t>(0, data->maxPrivateSlots);
  data->maxPublicSlots = std::max<int32_t>(0, data->maxPublicSlots);

  local_details_.MaxPrivateSlots = data->maxPrivateSlots;
  local_details_.MaxPublicSlots = data->maxPublicSlots;

  local_details_.AvailablePrivateSlots =
      std::max<int32_t>(0, local_details_.MaxPrivateSlots - num_private_slots);
  local_details_.AvailablePublicSlots =
      std::max<int32_t>(0, local_details_.MaxPublicSlots - num_public_slots);

  PrintSessionDetails();

  if (IsHost()) {
    XLiveAPI::SessionModify(session_id_, data);
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessionDetails(XSessionDetails* data) {
  auto local_details_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->session_details_ptr);

  local_details_ptr->SessionMembers_ptr =
      kernel_state_->memory()->SystemHeapAlloc(sizeof(XSESSION_MEMBER) *
                                               GetMembersCount());

  local_details_.SessionMembers_ptr = local_details_ptr->SessionMembers_ptr;

  XSESSION_MEMBER* members_ptr =
      kernel_state_->memory()->TranslateVirtual<XSESSION_MEMBER*>(
          local_details_ptr->SessionMembers_ptr);

  uint32_t index = 0;

  for (auto const& [xuid, member] : local_members_) {
    members_ptr[index] = member;
    index++;
  }

  for (auto const& [xuid, member] : remote_members_) {
    members_ptr[index] = member;
    index++;
  }

  memcpy(local_details_ptr, &local_details_, sizeof(XSESSION_LOCAL_DETAILS));

  PrintSessionDetails();

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

  const auto result = XLiveAPI::XSessionMigration(session_id_, data);

  if (!result->SessionID_UInt()) {
    XELOGI("Session Migration Failed");
    return X_E_FAIL;
  }

  memset(SessionInfo_ptr, 0, sizeof(XSESSION_INFO));

  Uint64toXNKID(result->SessionID_UInt(), &SessionInfo_ptr->sessionID);
  XLiveAPI::IpGetConsoleXnAddr(&SessionInfo_ptr->hostAddress);
  GenerateIdentityExchangeKey(&SessionInfo_ptr->keyExchangeKey);

  // Update session id to migrated session id
  session_id_ = result->SessionID_UInt();

  state |= STATE_FLAGS_HOST;
  state |= STATE_FLAGS_MIGRATED;

  local_details_.UserIndexHost = data->user_index;
  local_details_.sessionInfo = *SessionInfo_ptr;
  local_details_.xnkidArbitration = local_details_.sessionInfo.sessionID;

  return X_ERROR_SUCCESS;
}

// Server dependancy can be removed if we calculate remote machine id from
// remote mac address.
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
    registrants[i].Trustworthiness = 1;

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

  Uint64toXNKID(session_id_, &local_details_.xnkidArbitration);

  local_details_.eState = XSESSION_STATE::REGISTRATION;

  // Assert?
  // local_details_.Nonce = data->session_nonce;

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
  if (!HasSessionFlag(static_cast<SessionFlags>((uint32_t)local_details_.Flags),
                      STATS)) {
    XELOGW("Session does not support stats.");
    return X_ERROR_FUNCTION_FAILED;
  }

  if (local_details_.eState != XSESSION_STATE::INGAME) {
    XELOGW("Writing stats outside of gameplay.");
    return X_ERROR_FUNCTION_FAILED;
  }

  if (!data->number_of_leaderboards) {
    XELOGW("No leaderboard stats to write.");
    return X_ERROR_SUCCESS;
  }

  XSessionViewProperties* leaderboard =
      kernel_state_->memory()->TranslateVirtual<XSessionViewProperties*>(
          data->leaderboards_ptr);

  XLiveAPI::SessionWriteStats(session_id_, data, leaderboard);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::StartSession(uint32_t flags) {
  local_details_.eState = XSESSION_STATE::INGAME;

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::EndSession() {
  local_details_.eState = XSESSION_STATE::REPORTING;

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessions(Memory* memory, XSessionSearch* search_data) {
  if (!search_data->results_buffer_size) {
    search_data->results_buffer_size =
        sizeof(XSESSION_SEARCHRESULT) * search_data->num_results;
    return ERROR_INSUFFICIENT_BUFFER;
  }

  const auto sessions = XLiveAPI::SessionSearch(search_data);

  const uint32_t session_count =
      std::min<int32_t>(search_data->num_results, (uint32_t)sessions.size());

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
  result->filled_private_slots = session->FilledPrivateSlotsCount();
  result->filled_public_slots = session->FilledPublicSlotsCount();
  result->open_private_slots = session->OpenPrivateSlotsCount();
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

void XSession::PrintSessionDetails() {
  XELOGI(
      "\n***************** PrintSessionDetails *****************\n"
      "UserIndex: {}\n"
      "GameType: {}\n"
      "GameMode: {}\n"
      "eState: {}\n"
      "Nonce: {:016X}\n"
      "Flags: {:08X}\n"
      "MaxPrivateSlots: {}\n"
      "MaxPublicSlots: {}\n"
      "AvailablePrivateSlots: {}\n"
      "AvailablePublicSlots: {}\n"
      "ActualMemberCount: {}\n"
      "ReturnedMemberCount: {}\n"
      "xnkidArbitration: {:016X}\n",
      local_details_.UserIndexHost.get(),
      local_details_.GameType ? "Standard" : "Ranked",
      local_details_.GameMode.get(),
      static_cast<uint32_t>(local_details_.eState), local_details_.Nonce.get(),
      local_details_.Flags.get(), local_details_.MaxPrivateSlots.get(),
      local_details_.MaxPublicSlots.get(),
      local_details_.AvailablePrivateSlots.get(),
      local_details_.AvailablePublicSlots.get(),
      local_details_.ActualMemberCount.get(),
      local_details_.ReturnedMemberCount.get(),
      local_details_.xnkidArbitration.as_uint64());

  uint32_t index = 0;

  for (const auto& [xuid, mamber] : local_members_) {
    XELOGI(
        "\n***************** LOCAL MEMBER {} *****************\n"
        "Online XUID: {:016X}\n"
        "UserIndex: {}\n"
        "Flags: {:08X}\n"
        "IsPrivate: {}\n",
        index++, mamber.OnlineXUID.get(), mamber.UserIndex.get(),
        mamber.Flags.get(), mamber.IsPrivate() ? "True" : "False");
  }

  index = 0;

  for (const auto& [xuid, mamber] : remote_members_) {
    XELOGI(
        "\n***************** REMOTE MEMBER {} *****************\n"
        "Online XUID: {:016X}\n"
        "UserIndex: {}\n"
        "Flags: {:08X}\n"
        "IsPrivate: {}\n",
        index++, mamber.OnlineXUID.get(), mamber.UserIndex.get(),
        mamber.Flags.get(), mamber.IsPrivate() ? "True" : "False");
  }
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