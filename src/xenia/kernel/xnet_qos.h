/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_QOS_H_
#define XENIA_KERNEL_XNET_QOS_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace xe {
namespace kernel {

#pragma pack(push, 1)
struct XNetQosPacketRequest {
  uint32_t magic;  // 'QOS1'
  uint8_t type;    // 1
  uint8_t pad[3];
  uint64_t session_id;
  uint32_t lookup_id;
  uint32_t probe_id;
};

struct XNetQosPacketResponseHeader {
  uint32_t magic;
  uint8_t type;  // 2
  uint8_t pad[3];
  uint64_t session_id;
  uint32_t lookup_id;
  uint32_t probe_id;
  uint16_t data_size;
  uint8_t enabled;
  uint8_t pad2;
};
#pragma pack(pop)

struct XNetQosListener {
  uint64_t session_id = 0;
  bool active = false;
  uint32_t bits_per_sec = 20480;
  std::vector<uint8_t> data;
  uint32_t probes_recv = 0;
  uint32_t data_replies = 0;
};

struct XNetQosPendingTarget {
  uint32_t online_ip_nbo = 0;
  uint64_t session_id = 0;
  uint32_t probes_sent = 0;
  uint32_t probes_recv = 0;
  uint32_t probe_count = 0;
  uint64_t last_send_msec = 0;
  uint64_t ice_start_msec = 0;
  bool complete = false;
  // True only when a response arrived with enabled=0 (XNetQosListen disabled).
  // Timeouts / no reply must NOT set this — that is unreachable, not refused.
  bool disabled = false;
  uint16_t rtt_min_ms = 0;
  uint16_t rtt_med_ms = 0;
  std::vector<uint8_t> reply;
};

struct XNetQosPendingLookup {
  uint32_t lookup_id = 0;
  uint32_t bits_per_sec = 0;
  std::vector<XNetQosPendingTarget> targets;
};

class XNetQos {
 public:
  static constexpr uint32_t kMagic = 0x31534F51;  // 'QOS1' LE
  static constexpr uint16_t kPort = 1005;

  static XNetQos& Instance();

  void Initialize();
  void Shutdown();
  void Tick();

  void OnIcePacket(uint32_t from_online_ip, const uint8_t* data, size_t len);

  int Listen(uint64_t session_id, const uint8_t* data, uint32_t data_size,
             uint32_t bits_per_sec, uint32_t flags);
  // Returns lookup_id (>0) or 0 on failure.
  uint32_t StartLookup(const std::vector<uint32_t>& online_ips_nbo,
                       const std::vector<uint64_t>& session_ids,
                       uint32_t probes, uint32_t bits_per_sec);
  bool GetLookupResults(uint32_t lookup_id,
                        std::vector<XNetQosPendingTarget>* out);
  void ReleaseLookup(uint32_t lookup_id);

  bool GetListenStats(uint64_t session_id, uint32_t* probes_recv,
                      uint32_t* data_replies);

 private:
  XNetQos() = default;
  void ProcessPacket(uint32_t from_online_ip, const uint8_t* data, size_t len);
  void SendProbe(XNetQosPendingLookup& lookup, XNetQosPendingTarget& target);
  void SendResponse(uint32_t to_online_ip, const XNetQosPacketRequest& req,
                    const XNetQosListener& listener);
  bool PeerInUseLocked(uint32_t online_ip_nbo, uint32_t except_lookup_id) const;

  bool UseIceForSession(uint64_t session_id) const;
  bool EnsureSocketLocked();
  void CloseSocketLocked();
  void ReceiveUdpPackets();
  bool SendUdp(uint32_t ip_nbo, const uint8_t* data, size_t len);

  struct QueuedPacket {
    uint32_t from_online_ip = 0;
    std::vector<uint8_t> data;
  };

  std::mutex mutex_;
  bool initialized_ = false;
  std::unordered_map<uint64_t, XNetQosListener> listeners_;
  std::unordered_map<uint32_t, XNetQosPendingLookup> lookups_;
  uint32_t next_lookup_id_ = 1;
  std::vector<QueuedPacket> inbound_;

  // Native UDP socket (INVALID_SOCKET / -1 when closed).
  intptr_t socket_ = -1;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_QOS_H_
