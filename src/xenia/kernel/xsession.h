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
#include "xenia/kernel/json/session_object_json.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/xlast.h"
#include "xenia/kernel/xam/user_property.h"
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

// TODO(Gliniak): Not sure if all these structures should be here.
struct XGI_SESSION_MODIFY {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> maxPublicSlots;
  xe::be<uint32_t> maxPrivateSlots;
};
static_assert_size(XGI_SESSION_MODIFY, 0x10);

struct XGI_SESSION_STATE {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint64_t> session_nonce;
};
static_assert_size(XGI_SESSION_STATE, 0x10);

struct XGI_SESSION_SEARCH {
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
static_assert_size(XGI_SESSION_SEARCH, 0x20);

struct XGI_SESSION_SEARCH_EX {
  XGI_SESSION_SEARCH session_search;
  xe::be<uint32_t> num_users;
};
static_assert_size(XGI_SESSION_SEARCH_EX, 0x24);

struct XGI_SESSION_SEARCH_BYID {
  xe::be<uint32_t> user_index;
  XNKID session_id;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
};
static_assert_size(XGI_SESSION_SEARCH_BYID, 0x14);

struct XGI_SESSION_SEARCH_BYIDS {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> num_session_ids;
  xe::be<uint32_t> session_ids_ptr;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
  xe::be<uint32_t> reserved1;
  xe::be<uint32_t> reserved2;
  xe::be<uint32_t> reserved3;
};
static_assert_size(XGI_SESSION_SEARCH_BYIDS, 0x20);

struct XGI_SESSION_SEARCH_WEIGHTED {
  xe::be<uint32_t> proc_index;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> num_results;
  xe::be<uint16_t> num_weighted_properties;
  xe::be<uint16_t> num_weighted_contexts;
  xe::be<uint32_t> weighted_search_properties_ptr;
  xe::be<uint32_t> weighted_search_contexts_ptr;
  xe::be<uint16_t> num_props;
  xe::be<uint16_t> num_ctx;
  xe::be<uint32_t> non_weighted_search_properties_ptr;
  xe::be<uint32_t> non_weighted_search_contexts_ptr;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
  xe::be<uint32_t> num_users;
  xe::be<uint32_t> weighted_search;
};
static_assert_size(XGI_SESSION_SEARCH_WEIGHTED, 0x34);

struct XGI_SESSION_DETAILS {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> details_buffer_size;
  xe::be<uint32_t> session_details_ptr;
  xe::be<uint32_t> reserved1;
  xe::be<uint32_t> reserved2;
  xe::be<uint32_t> reserved3;
};
static_assert_size(XGI_SESSION_DETAILS, 0x18);

struct XGI_SESSION_MIGRATE {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> session_info_ptr;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> reserved1;
  xe::be<uint32_t> reserved2;
  xe::be<uint32_t> reserved3;
};
static_assert_size(XGI_SESSION_MIGRATE, 0x18);

struct XGI_SESSION_ARBITRATION {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint64_t> session_nonce;
  xe::be<uint32_t> session_duration_sec;  // 300
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> results_ptr;
};
static_assert_size(XGI_SESSION_ARBITRATION, 0x20);

struct XGI_SESSION_CREATE {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> num_slots_public;
  xe::be<uint32_t> num_slots_private;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> session_info_ptr;
  xe::be<uint32_t> nonce_ptr;
};
static_assert_size(XGI_SESSION_CREATE, 0x1C);

struct XGI_STATS_WRITE {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> num_views;
  xe::be<uint32_t> views_ptr;
};
static_assert_size(XGI_STATS_WRITE, 0x18);

struct XGI_SESSION_MODIFYSKILL {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;
  xe::be<uint32_t> reserved1;
  xe::be<uint32_t> reserved2;
  xe::be<uint32_t> reserved3;
};
static_assert_size(XGI_SESSION_MODIFYSKILL, 0x18);

struct XGI_SESSION_MANAGE {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;
  xe::be<uint32_t> indices_array_ptr;
  xe::be<uint32_t> private_slots_array_ptr;
};
static_assert_size(XGI_SESSION_MANAGE, 0x14);

struct XGI_SESSION_INVITE {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> session_info_ptr;
};
static_assert_size(XGI_SESSION_INVITE, 0x8);

struct SEARCH_RESULTS {
  XSESSION_SEARCHRESULT_HEADER header;
  XSESSION_SEARCHRESULT* results_ptr;
};

struct Player {
  xe::be<uint64_t> xuid;
  std::string hostAddress;
  xe::be<uint64_t> machineId;
  uint16_t port;
  xe::be<uint64_t> macAddress;  // 6 Bytes
  xe::be<uint64_t> sessionId;
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

class XSession : public XObject {
 public:
  static const Type kObjectType = Type::Session;

  XSession(KernelState* kernel_state);

  X_STATUS Initialize();
  X_RESULT CreateSession(uint8_t user_index, uint8_t public_slots,
                         uint8_t private_slots, uint32_t flags,
                         uint32_t session_info_ptr, uint32_t nonce_ptr);
  X_RESULT DeleteSession(XGI_SESSION_STATE* state);

  X_RESULT JoinSession(XGI_SESSION_MANAGE* data);
  X_RESULT LeaveSession(XGI_SESSION_MANAGE* data);

  X_RESULT ModifySession(XGI_SESSION_MODIFY* data);
  X_RESULT GetSessionDetails(XGI_SESSION_DETAILS* data);
  X_RESULT MigrateHost(XGI_SESSION_MIGRATE* data);
  X_RESULT RegisterArbitration(XGI_SESSION_ARBITRATION* data);
  X_RESULT ModifySkill(XGI_SESSION_MODIFYSKILL* data);
  X_RESULT WriteStats(XGI_STATS_WRITE* data);

  X_RESULT StartSession(XGI_SESSION_STATE* state);
  X_RESULT EndSession(XGI_SESSION_STATE* state);

  static X_RESULT GetSessions(KernelState* kernel_state,
                              XGI_SESSION_SEARCH* search_data,
                              uint32_t num_users);
  static X_RESULT GetWeightedSessions(KernelState* kernel_state,
                                      XGI_SESSION_SEARCH_WEIGHTED* search_data,
                                      uint32_t num_users);
  static X_RESULT GetSessionByID(Memory* memory,
                                 XGI_SESSION_SEARCH_BYID* search_data);
  static X_RESULT GetSessionByIDs(Memory* memory,
                                  XGI_SESSION_SEARCH_BYIDS* search_data);
  static X_RESULT GetSessionByIDs(Memory* memory, XNKID* session_ids_ptr,
                                  uint32_t num_session_ids,
                                  uint32_t search_results_ptr,
                                  uint32_t results_buffer_size);

  const bool IsXboxLive() { return !is_systemlink_; }

  const bool IsSystemlink() { return is_systemlink_; }

  static const bool IsSystemlinkFlags(uint8_t flags) {
    // STATS
    const uint32_t systemlink = HOST | PEER_NETWORK;

    return (flags & ~systemlink) == 0;
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

  const xe::be<uint32_t> GetGameModeValue(uint64_t xuid) {
    const xam::Property* gamemode =
        kernel_state()->xam_state()->user_tracker()->GetProperty(
            xuid, XCONTEXT_GAME_MODE);

    if (gamemode) {
      return gamemode->get_data()->data.u32;
    }

    return 0;
  }

  const xe::be<uint32_t> GetGameTypeValue(uint64_t xuid) {
    const xam::Property* game_type =
        kernel_state()->xam_state()->user_tracker()->GetProperty(
            xuid, XCONTEXT_GAME_TYPE);

    if (game_type) {
      return game_type->get_data()->data.u32;
    }

    return 0;
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

  const bool IsValidModifyFlags(uint32_t flags) const {
    const uint32_t allowed_modify_flags =
        JOIN_IN_PROGRESS_DISABLED | JOIN_VIA_PRESENCE_FRIENDS_ONLY |
        JOIN_VIA_PRESENCE_DISABLED | INVITES_DISABLED | ARBITRATION;

    const uint32_t changed_flags = local_details_.Flags ^ flags;

    return (changed_flags & ~allowed_modify_flags) == 0;
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

  static void FillSessionContext(Memory* memory, uint32_t matchmaking_index,
                                 util::XLastMatchmakingQuery* matchmaking_query,
                                 std::vector<xam::Property> contexts,
                                 uint32_t filter_contexts_count,
                                 xam::XUSER_CONTEXT* filter_contexts_ptr,
                                 XSESSION_SEARCHRESULT* result);

  static void FillSessionProperties(
      Memory* memory, uint32_t matchmaking_index,
      util::XLastMatchmakingQuery* matchmaking_query,
      std::vector<xam::Property> properties, uint32_t filter_properties_count,
      xam::XUSER_PROPERTY* filter_properties_ptr,
      XSESSION_SEARCHRESULT* result);

  // uint64_t migrated_session_id_;
  uint64_t session_id_ = 0;
  uint32_t state_ = 0;

  bool is_systemlink_ = false;

  XSESSION_LOCAL_DETAILS local_details_{};

  std::map<uint64_t, XSESSION_MEMBER> local_members_{};
  std::map<uint64_t, XSESSION_MEMBER> remote_members_{};

  // TODO!
  std::vector<uint8_t> stats_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSESSION_H_