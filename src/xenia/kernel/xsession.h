/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSESSION_H_
#define XENIA_KERNEL_XSESSION_H_

#include "xenia/base/byte_order.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/session_object_json.h"
#include "xenia/kernel/xnet.h"
#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {

enum SessionFlags {
  HOST = 0x01,
  PRESENCE = 0x02,
  STATS = 0x04,
  MATCHMAKING = 0x08,
  ARBITRATION = 0x10,
  PEER_NETWORK = 0x20,
  SOCIAL_MATCHMAKING_ALLOWED = 0x80,
  INVITES_DISABLED = 0x0100,
  JOIN_VIA_PRESENCE_DISABLED = 0x0200,
  JOIN_IN_PROGRESS_DISABLED = 0x0400,
  JOIN_VIA_PRESENCE_FRIENDS_ONLY = 0x0800,

  SINGLEPLAYER_WITH_STATS = PRESENCE | STATS | INVITES_DISABLED |
                            JOIN_VIA_PRESENCE_DISABLED |
                            JOIN_IN_PROGRESS_DISABLED,

  LIVE_MULTIPLAYER_STANDARD = PRESENCE | STATS | MATCHMAKING | PEER_NETWORK,
  LIVE_MULTIPLAYER_RANKED = LIVE_MULTIPLAYER_STANDARD | ARBITRATION,
  SYSTEMLINK = PEER_NETWORK,
  GROUP_LOBBY = PRESENCE | PEER_NETWORK,
  GROUP_GAME = STATS | MATCHMAKING | PEER_NETWORK
};

enum class MEMBER_FLAGS : uint32_t { PRIVATE_SLOT = 0x01, ZOMBIE = 0x2 };

enum class XSESSION_STATE : uint32_t {
  LOBBY,
  REGISTRATION,
  INGAME,
  REPORTING,
  DELETED
};

enum STATE_FLAGS : uint32_t {
  STATE_FLAGS_CREATED = 0x01,
  STATE_FLAGS_HOST = 0x02,
  STATE_FLAGS_MIGRATED = 0x04,
  STATE_FLAGS_DELETED = 0x08,
};

struct X_KSESSION {
  xe::be<uint32_t> handle;
};
static_assert_size(X_KSESSION, 4);

struct XSESSION_INFO {
  XNKID sessionID;
  XNADDR hostAddress;
  XNKEY keyExchangeKey;
};

struct XSESSION_REGISTRANT {
  xe::be<uint64_t> MachineID;
  xe::be<uint32_t> Trustworthiness;
  xe::be<uint32_t> bNumUsers;
  xe::be<uint32_t> rgUsers;
};

struct XSESSION_REGISTRATION_RESULTS {
  xe::be<uint32_t> registrants_count;
  xe::be<uint32_t> registrants_ptr;
};

struct XSESSION_SEARCHRESULT {
  XSESSION_INFO info;
  xe::be<uint32_t> open_public_slots;
  xe::be<uint32_t> open_private_slots;
  xe::be<uint32_t> filled_public_slots;
  xe::be<uint32_t> filled_private_slots;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> contexts_count;
  xe::be<uint32_t> properties_ptr;
  xe::be<uint32_t> contexts_ptr;
};

struct XSESSION_SEARCHRESULT_HEADER {
  xe::be<uint32_t> search_results_count;
  xe::be<uint32_t> search_results_ptr;
};

struct XSESSION_LOCAL_DETAILS {
  xe::be<uint32_t> UserIndexHost;
  xe::be<uint32_t> GameType;
  xe::be<uint32_t> GameMode;
  xe::be<uint32_t> Flags;
  xe::be<uint32_t> MaxPublicSlots;
  xe::be<uint32_t> MaxPrivateSlots;
  xe::be<uint32_t> AvailablePublicSlots;
  xe::be<uint32_t> AvailablePrivateSlots;
  xe::be<uint32_t> ActualMemberCount;
  xe::be<uint32_t> ReturnedMemberCount;
  XSESSION_STATE eState;
  xe::be<uint64_t> Nonce;
  XSESSION_INFO sessionInfo;
  XNKID xnkidArbitration;
  xe::be<uint32_t> SessionMembers_ptr;
};

struct XSESSION_MEMBER {
  xe::be<uint64_t> OnlineXUID;
  xe::be<uint32_t> UserIndex;
  xe::be<uint32_t> Flags;

  void SetPrivate() {
    Flags |= static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT);
  }

  void SetZombie() { Flags |= static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE); }

  const bool IsPrivate() const {
    return (Flags & static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT)) ==
           static_cast<uint32_t>(MEMBER_FLAGS::PRIVATE_SLOT);
  }

  const bool IsZombie() const {
    return (Flags & static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE)) ==
           static_cast<uint32_t>(MEMBER_FLAGS::ZOMBIE);
  }
};

// TODO(Gliniak): Not sure if all these structures should be here.
struct XSessionModify {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> maxPublicSlots;
  xe::be<uint32_t> maxPrivateSlots;
};

struct XSessionStart {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
};

struct XSessionEnd {
  xe::be<uint32_t> obj_ptr;
};

struct XSessionSearch {
  xe::be<uint32_t> proc_index;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> num_results;
  xe::be<uint16_t> num_props;
  xe::be<uint16_t> num_ctx;
  xe::be<uint32_t> props_ptr;
  xe::be<uint32_t> ctx_ptr;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
};

struct XSessionSearchEx {
  XSessionSearch session_search;
  xe::be<uint32_t> num_users;
};

struct XSessionSearchByID {
  xe::be<uint32_t> user_index;
  XNKID session_id;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
};

struct XSessionSearchByIDs {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> num_session_ids;
  xe::be<uint32_t> session_ids;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
  xe::be<uint32_t> value_const1;  // 0
  xe::be<uint32_t> value_const2;  // 0
  xe::be<uint32_t> value_const3;  // 0
};

struct XSessionDetails {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> details_buffer_size;
  xe::be<uint32_t> session_details_ptr;
};

struct XSessionMigate {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> session_info_ptr;
  xe::be<uint32_t> user_index;
};

struct XSessionArbitrationData {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint64_t> session_nonce;
  xe::be<uint32_t> value_const;  // 300
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> results_ptr;
};

struct XSessionData {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> num_slots_public;
  xe::be<uint32_t> num_slots_private;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> session_info_ptr;
  xe::be<uint32_t> nonce_ptr;
};

struct XSessionWriteStats {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> unk_value;
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> number_of_leaderboards;
  xe::be<uint32_t> leaderboards_ptr;
};

struct XSessionModifySkill {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;
};

struct XSessionViewProperties {
  xe::be<uint32_t> leaderboard_id;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> properties_ptr;
};

struct XSessionJoin {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;     // 0 = Join Local
  xe::be<uint32_t> indices_array_ptr;  // 0 = Join Remote
  xe::be<uint32_t> private_slots_array_ptr;
};

struct XSessionLeave {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;     // 0 = Leave Local
  xe::be<uint32_t> indices_array_ptr;  // 0 = Leave Remote
  xe::be<uint32_t> unused;             // const 0
};

struct Player {
  xe::be<uint64_t> xuid;
  std::string hostAddress;
  xe::be<uint64_t> machineId;
  uint16_t port;
  xe::be<uint64_t> macAddress;  // 6 Bytes
  xe::be<uint64_t> sessionId;
};

struct SEARCH_RESULTS {
  XSESSION_SEARCHRESULT_HEADER header;
  XSESSION_SEARCHRESULT* results_ptr;
};

struct SessionJSON {
  xe::be<uint64_t> sessionid;
  xe::be<uint16_t> port;
  xe::be<uint32_t> flags;
  std::string hostAddress;
  std::string macAddress;
  xe::be<uint32_t> publicSlotsCount;
  xe::be<uint32_t> privateSlotsCount;
  xe::be<uint32_t> openPublicSlotsCount;
  xe::be<uint32_t> openPrivateSlotsCount;
  xe::be<uint32_t> filledPublicSlotsCount;
  xe::be<uint32_t> filledPrivateSlotsCount;
  std::vector<Player> players;
};

struct MachineInfo {
  xe::be<uint64_t> machineId;
  xe::be<uint32_t> playerCount;
  std::vector<uint64_t> xuids;
};

struct XSessionArbitrationJSON {
  xe::be<uint32_t> totalPlayers;
  std::vector<MachineInfo> machines;
};

struct XUSER_CONTEXT {
  xe::be<uint32_t> context_id;
  xe::be<uint32_t> value;
};

class XSession : public XObject {
 public:
  static const Type kObjectType = Type::Session;

  XSession(KernelState* kernel_state);

  X_STATUS Initialize();
  X_RESULT CreateSession(uint8_t user_index, uint8_t public_slots,
                         uint8_t private_slots, uint32_t flags,
                         uint32_t session_info_ptr, uint32_t nonce_ptr);
  X_RESULT DeleteSession();

  X_RESULT JoinSession(XSessionJoin* data);
  X_RESULT LeaveSession(XSessionLeave* data);

  X_RESULT ModifySession(XSessionModify* data);
  X_RESULT GetSessionDetails(XSessionDetails* data);
  X_RESULT MigrateHost(XSessionMigate* data);
  X_RESULT RegisterArbitration(XSessionArbitrationData* data);
  X_RESULT ModifySkill(XSessionModifySkill* data);
  X_RESULT WriteStats(XSessionWriteStats* data);

  X_RESULT StartSession(uint32_t flags);
  X_RESULT EndSession();

  static X_RESULT GetSessions(Memory* memory, XSessionSearch* search_data,
                              uint32_t num_users);
  static X_RESULT GetSessionByID(Memory* memory,
                                 XSessionSearchByID* search_data);
  static X_RESULT GetSessionByIDs(Memory* memory,
                                  XSessionSearchByIDs* search_data);
  static X_RESULT GetSessionByIDs(Memory* memory, XNKID* session_ids_ptr,
                                  uint32_t num_session_ids,
                                  uint32_t search_results_ptr,
                                  uint32_t results_buffer_size);

  static constexpr uint8_t XNKID_ONLINE = 0xAE;
  static constexpr uint8_t XNKID_SYSTEM_LINK = 0x00;

  // static constexpr uint32_t ERROR_SESSION_WRONG_STATE = 0x80155206;

  static void GenerateIdentityExchangeKey(XNKEY* key) {
    for (uint8_t i = 0; i < sizeof(XNKEY); i++) {
      key->ab[i] = i;
    }
  }

  static const uint64_t GenerateSessionId(uint8_t mask) {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist(0, -1);

    return ((uint64_t)mask << 56) | (dist(rd) & 0x0000FFFFFFFFFFFF);
  }

  static const bool IsOnlinePeer(uint64_t session_id) {
    return ((session_id >> 56) & 0xFF) == XNKID_ONLINE;
  }

  static const bool IsSystemlink(uint64_t session_id) {
    return ((session_id >> 56) & 0xFF) == XNKID_SYSTEM_LINK;
  }

  const bool IsXboxLive() { return !is_systemlink_; }

  const bool IsSystemlink() { return is_systemlink_; }

  static const bool IsValidXNKID(uint64_t session_id) {
    if (!XSession::IsOnlinePeer(session_id) &&
            !XSession::IsSystemlink(session_id) ||
        session_id == 0) {
      assert_always();

      return false;
    }

    return true;
  }

  static const bool IsSystemlinkFlags(uint8_t flags) {
    const uint32_t systemlink = HOST | STATS | PEER_NETWORK;

    return (flags & ~systemlink) == 0;
  }

  const bool IsMemberLocallySignedIn(uint64_t xuid, uint32_t user_index) const {
    return kernel_state()->xam_state()->IsUserSignedIn(xuid) ||
           kernel_state()->xam_state()->IsUserSignedIn(user_index);
  }

  const uint32_t GetMembersCount() const {
    const uint32_t max_slots =
        local_details_.MaxPrivateSlots + local_details_.MaxPublicSlots;

    const uint32_t available_slots = local_details_.AvailablePrivateSlots +
                                     local_details_.AvailablePublicSlots;

    // When adding a member we can calculate the next slot
    const uint32_t used_slot = max_slots - available_slots;

    const uint32_t members_size = static_cast<uint32_t>(local_members_.size()) +
                                  static_cast<uint32_t>(remote_members_.size());

    assert_false(used_slot != members_size);

    return members_size;
  }

  const uint32_t GetGameModeContext() {
    return contexts_.find(X_CONTEXT_GAME_MODE) != contexts_.end()
               ? contexts_[X_CONTEXT_GAME_MODE]
               : 0;
  }

  const uint32_t GetGameTypeContext() {
    return contexts_.find(X_CONTEXT_GAME_TYPE) != contexts_.end()
               ? contexts_[X_CONTEXT_GAME_TYPE]
               : X_CONTEXT_GAME_TYPE_STANDARD;
  }

  const uint32_t GetPresenceContext() {
    return contexts_.find(X_CONTEXT_PRESENCE) != contexts_.end()
               ? contexts_[X_CONTEXT_PRESENCE]
               : 0;
  }

  const bool IsCreated() const {
    return (state_ & STATE_FLAGS_CREATED) == STATE_FLAGS_CREATED;
  }

  const bool IsHost() const {
    return (state_ & STATE_FLAGS_HOST) == STATE_FLAGS_HOST;
  }

  const bool IsMigrted() const {
    return (state_ & STATE_FLAGS_MIGRATED) == STATE_FLAGS_MIGRATED;
  }

  const bool IsDeleted() const {
    return (state_ & STATE_FLAGS_DELETED) == STATE_FLAGS_DELETED;
  }

 private:
  void PrintSessionDetails();
  void PrintSessionType(SessionFlags flags);

  X_RESULT CreateHostSession(XSESSION_INFO* session_info, uint64_t* nonce_ptr,
                             uint8_t user_index, uint8_t public_slots,
                             uint8_t private_slots, uint32_t flags);
  X_RESULT CreateStatsSession(XSESSION_INFO* session_info, uint64_t* nonce_ptr,
                              uint8_t user_index, uint8_t public_slots,
                              uint8_t private_slots, uint32_t flags);
  X_RESULT JoinExistingSession(XSESSION_INFO* session_info);

  const bool HasSessionFlag(SessionFlags flags,
                            SessionFlags checked_flag) const {
    return (flags & checked_flag) == checked_flag;
  };

  static void GetXnAddrFromSessionObject(SessionObjectJSON* session,
                                         XNADDR* XnAddr_ptr);

  static void FillSessionSearchResult(
      const std::unique_ptr<SessionObjectJSON>& session_info,
      XSESSION_SEARCHRESULT* result);

  static void FillSessionContext(Memory* memory,
                                 std::map<uint32_t, uint32_t> contexts,
                                 XSESSION_SEARCHRESULT* result);

  static void FillSessionProperties(uint32_t properties_count,
                                    uint32_t properties_ptr,
                                    XSESSION_SEARCHRESULT* result);

  // uint64_t migrated_session_id_;
  uint64_t session_id_ = 0;
  uint32_t state_ = 0;

  bool is_systemlink_ = false;

  XSESSION_LOCAL_DETAILS local_details_{};

  std::map<uint64_t, XSESSION_MEMBER> local_members_{};
  std::map<uint64_t, XSESSION_MEMBER> remote_members_{};

  // These are all contexts that host provided during creation of a session.
  // These are constant for single session.
  std::map<uint32_t, uint32_t> contexts_;
  // TODO!
  std::vector<uint8_t> properties_;
  std::vector<uint8_t> stats_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSESSION_H_