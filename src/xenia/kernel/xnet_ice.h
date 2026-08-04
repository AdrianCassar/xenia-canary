/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_ICE_H_
#define XENIA_KERNEL_XNET_ICE_H_

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/kernel/xnet_signalling.h"

struct juice_agent;

namespace xe {
namespace kernel {

enum class XNetIceConnectStatus : uint32_t {
  kIdle = 0,
  kPending = 1,
  kConnected = 2,
  kFailed = 3,
};

// Mux types on the title ICE data channel.
constexpr uint8_t kXNetMuxUdp = 0;
constexpr uint8_t kXNetMuxKcp = 1;
constexpr uint8_t kXNetMuxCtrl = 2;

struct XNetMuxPacket {
  uint8_t type = 0;
  uint16_t id = 0;
  std::vector<uint8_t> payload;
  uint32_t from_online_ip = 0;  // network byte order
};

class XNetIce {
 public:
  using TitleRecvHandler = std::function<void(const XNetMuxPacket&)>;
  using QosRecvHandler =
      std::function<void(uint32_t from_online_ip, const uint8_t* data,
                         size_t len)>;

  static XNetIce& Instance();

  void Initialize(XNetSignalling* signalling, const std::string& stun_host,
                  uint16_t stun_port);
  void Shutdown();

  void SetLocalPeerId(const std::string& peer_id);
  void SetTitleRecvHandler(TitleRecvHandler handler);
  void SetQosRecvHandler(QosRecvHandler handler);

  void Establish(uint32_t online_ip_nbo, XNetPeerType type);
  void ClosePeer(uint32_t online_ip_nbo, XNetPeerType type);
  bool HasPeer(uint32_t online_ip_nbo, XNetPeerType type);
  XNetIceConnectStatus GetConnectStatus(uint32_t online_ip_nbo,
                                        XNetPeerType type);

  int SendMux(uint32_t online_ip_nbo, uint8_t type, uint16_t id,
              const uint8_t* data, size_t len);
  int SendQos(uint32_t online_ip_nbo, const uint8_t* data, size_t len);

  static std::string OnlineIpToPeerId(uint32_t online_ip_nbo);
  static uint32_t PeerIdToOnlineIp(const std::string& peer_id);
  static bool IsPrivateOnlineAddress(uint32_t ip_nbo);

 private:
  struct Peer {
    bool in_use = false;
    uint32_t online_ip_nbo = 0;
    std::string peer_id;
    XNetPeerType type = XNetPeerType::kTitle;
    juice_agent* agent = nullptr;
    bool we_are_offerer = false;
    XNetIceConnectStatus status = XNetIceConnectStatus::kIdle;
    // Trickle candidates that arrived before the remote description.
    std::vector<std::string> pending_candidates;
    bool remote_gathering_done = false;
  };

  static constexpr size_t kMaxPeersPerType = 64;

  XNetIce() = default;

  Peer* FindPeerLocked(uint32_t online_ip_nbo, XNetPeerType type);
  Peer* AllocPeerLocked(uint32_t online_ip_nbo, XNetPeerType type);
  void StartOfferLocked(Peer* peer);
  void FlushPendingCandidatesLocked(Peer* peer);
  // Detaches agent for juice_destroy outside the lock. Returns old agent or null.
  juice_agent* DetachPeerAgentLocked(Peer* peer);

  void OnOffer(const XNetSignallingOffer& msg);
  void OnAnswer(const XNetSignallingAnswer& msg);
  void OnCandidate(const XNetSignallingCandidate& msg);
  void OnGatheringDone(const XNetSignallingGatheringDone& msg);

  static void OnJuiceStateChanged(juice_agent* agent, int state, void* user);
  static void OnJuiceCandidate(juice_agent* agent, const char* sdp, void* user);
  static void OnJuiceGatheringDone(juice_agent* agent, void* user);
  static void OnJuiceRecv(juice_agent* agent, const char* data, size_t size,
                          void* user);

  std::mutex mutex_;
  bool initialized_ = false;
  XNetSignalling* signalling_ = nullptr;
  std::string local_peer_id_;
  std::string stun_host_ = "stun.l.google.com";
  uint16_t stun_port_ = 19302;
  std::array<std::array<Peer, kMaxPeersPerType>, 2> peers_{};
  TitleRecvHandler title_recv_;
  QosRecvHandler qos_recv_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_ICE_H_
