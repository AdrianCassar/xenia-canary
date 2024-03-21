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
#include "xenia/kernel/arbitration_object_json.h"
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

enum class XSESSION_STATE : uint32_t {
  LOBBY,
  REGISTRATION,
  INGAME,
  REPORTING,
  DELETED
};

struct X_KSESSION {
  xe::be<uint32_t> handle;
};
static_assert_size(X_KSESSION, 4);

struct XSESSION_INFO {
  xe::kernel::XNKID sessionID;
  XNADDR hostAddress;
  xe::kernel::XNKEY keyExchangeKey;
};

struct XSESSION_REGISTRANT {
  xe::be<uint64_t> MachineID;
  xe::be<uint32_t> bTrustworthiness;
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
  xe::be<uint32_t> open_priv_slots;
  xe::be<uint32_t> filled_public_slots;
  xe::be<uint32_t> filled_priv_slots;
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
  xe::kernel::XNKID xnkidArbitration;
  // XSESSION_MEMBER* pSessionMembers;
  xe::be<uint32_t> pSessionMembers;
};

struct XSESSION_MEMBER {
  xe::be<uint64_t> xuidOnline;
  xe::be<uint32_t> UserIndex;
  xe::be<uint32_t> Flags;
};

// TODO(Gliniak): Not sure if all these structures should be here.
struct XSessionModify {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> maxPublicSlots;
  xe::be<uint32_t> maxPrivateSlots;
  xe::be<uint32_t> xoverlapped;
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
  xe::be<uint32_t> xoverlapped_ptr;
};

struct XSessionSearchID {
  xe::kernel::XNKID* session_id;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> search_results_ptr;
  xe::be<uint32_t> xoverlapped_ptr;
};

struct XSessionDetails {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> details_buffer_size;
  xe::be<uint32_t> details_buffer;
  xe::be<uint32_t> pXOverlapped;
};

struct XSessionMigate {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> session_info_ptr;
  xe::be<uint32_t> pXOverlapped;
};

struct XSessionArbitrationData {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> unk1;
  xe::be<uint32_t> unk2;
  xe::be<uint32_t> session_nonce;
  xe::be<uint32_t> results_buffer_size;
  xe::be<uint32_t> results;
  xe::be<uint32_t> pXOverlapped;
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
  xe::be<uint32_t> unk1;
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> number_of_leaderboards;
  xe::be<uint32_t> leaderboards_guest_address;
  xe::be<uint32_t> xoverlapped;
};

struct XSessionViewProperties {
  xe::be<uint32_t> leaderboard_id;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> properties_guest_address;
};

struct XSessionJoin {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;
  xe::be<uint32_t> indices_array_ptr;
  xe::be<uint32_t> private_slots_array_ptr;
};

struct XSessionLeave {
  xe::be<uint32_t> obj_ptr;
  xe::be<uint32_t> array_count;
  xe::be<uint32_t> xuid_array_ptr;
  xe::be<uint32_t> indices_array_ptr;
  xe::be<uint32_t> unused;
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
  X_RESULT WriteStats(XSessionWriteStats* data);

  static X_RESULT GetSessions(Memory* memory, XSessionSearch* search_data);
  static X_RESULT GetSessionByID(Memory* memory, XSessionSearchID* search_data);

 private:
  void GenerateExchangeKey(XNKEY* key);
  uint64_t GenerateSessionId();
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

  static void FillSessionSearchResult(
      const std::unique_ptr<SessionObjectJSON>& session_info,
      XSESSION_SEARCHRESULT* result);

  static void FillSessionContext(Memory* memory,
                                 std::map<uint32_t, uint32_t> contexts,
                                 XSESSION_SEARCHRESULT* result);

  static void FillSessionProperties(uint32_t properties_count,
                                    uint32_t properties_ptr,
                                    XSESSION_SEARCHRESULT* result);

  bool is_session_created_ = false;
  XSESSION_STATE session_state = XSESSION_STATE::LOBBY;
  uint64_t session_id_;

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