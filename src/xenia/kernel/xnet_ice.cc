/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnet_ice.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include "juice/juice.h"

DEFINE_bool(ice_enabled, true, "Use ICE for XboxLive peer connectivity.",
            "Live");
DEFINE_string(stun_server, "stun.l.google.com", "STUN server hostname.",
              "Live");
DEFINE_uint32(stun_port, 19302, "STUN server port.", "Live");

namespace xe {
namespace kernel {

namespace {

const char* PeerTypeName(XNetPeerType type) {
  return type == XNetPeerType::kQos ? "qos" : "title";
}

const char* JuiceStateName(juice_state_t state) {
  switch (state) {
    case JUICE_STATE_DISCONNECTED:
      return "disconnected";
    case JUICE_STATE_GATHERING:
      return "gathering";
    case JUICE_STATE_CONNECTING:
      return "connecting";
    case JUICE_STATE_CONNECTED:
      return "connected";
    case JUICE_STATE_COMPLETED:
      return "completed";
    case JUICE_STATE_FAILED:
      return "failed";
    default:
      return "unknown";
  }
}

const char* IceStatusName(XNetIceConnectStatus status) {
  switch (status) {
    case XNetIceConnectStatus::kIdle:
      return "idle";
    case XNetIceConnectStatus::kPending:
      return "pending";
    case XNetIceConnectStatus::kConnected:
      return "connected";
    case XNetIceConnectStatus::kFailed:
      return "failed";
    default:
      return "unknown";
  }
}

void JuiceLogHandler(juice_log_level_t level, const char* message) {
  switch (level) {
    case JUICE_LOG_LEVEL_FATAL:
    case JUICE_LOG_LEVEL_ERROR:
      XELOGE("juice: {}", message);
      break;
    case JUICE_LOG_LEVEL_WARN:
      XELOGW("juice: {}", message);
      break;
    case JUICE_LOG_LEVEL_INFO:
      XELOGI("juice: {}", message);
      break;
    default:
      XELOGD("juice: {}", message);
      break;
  }
}

}  // namespace

XNetIce& XNetIce::Instance() {
  static XNetIce instance;
  return instance;
}

bool XNetIce::IsPrivateOnlineAddress(uint32_t ip_nbo) {
  // 0.x.x.x in network byte order: first octet is 0.
  return (ip_nbo & 0x000000FFu) == 0 && ip_nbo != 0;
}

std::string XNetIce::OnlineIpToPeerId(uint32_t online_ip_nbo) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&online_ip_nbo);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return buf;
}

uint32_t XNetIce::PeerIdToOnlineIp(const std::string& peer_id) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(peer_id.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
    return 0;
  }
  uint32_t ip = 0;
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&ip);
  bytes[0] = static_cast<uint8_t>(a);
  bytes[1] = static_cast<uint8_t>(b);
  bytes[2] = static_cast<uint8_t>(c);
  bytes[3] = static_cast<uint8_t>(d);
  return ip;
}

void XNetIce::Initialize(XNetSignalling* signalling, const std::string& stun_host,
                         uint16_t stun_port) {
  std::lock_guard lock(mutex_);
  if (initialized_) {
    return;
  }
  signalling_ = signalling;
  stun_host_ = stun_host;
  stun_port_ = stun_port;
  if (signalling_) {
    signalling_->SetHandlers(
        [this](const XNetSignallingOffer& m) { OnOffer(m); },
        [this](const XNetSignallingAnswer& m) { OnAnswer(m); },
        [this](const XNetSignallingCandidate& m) { OnCandidate(m); },
        [this](const XNetSignallingGatheringDone& m) { OnGatheringDone(m); });
  }
  juice_set_log_handler(JuiceLogHandler);
  juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);
  initialized_ = true;
  XELOGI("XNetIce initialized stun={}:{}", stun_host_, stun_port_);
}

void XNetIce::Shutdown() {
  std::vector<juice_agent*> to_destroy;
  {
    std::lock_guard lock(mutex_);
    for (auto& type_peers : peers_) {
      for (auto& peer : type_peers) {
        if (peer.agent) {
          to_destroy.push_back(peer.agent);
          peer.agent = nullptr;
        }
        peer = {};
      }
    }
    initialized_ = false;
    signalling_ = nullptr;
  }
  for (auto* agent : to_destroy) {
    juice_destroy(agent);
  }
}

void XNetIce::SetLocalPeerId(const std::string& peer_id) {
  std::lock_guard lock(mutex_);
  local_peer_id_ = peer_id;
}

void XNetIce::SetTitleRecvHandler(TitleRecvHandler handler) {
  std::lock_guard lock(mutex_);
  title_recv_ = std::move(handler);
}

void XNetIce::SetQosRecvHandler(QosRecvHandler handler) {
  std::lock_guard lock(mutex_);
  qos_recv_ = std::move(handler);
}

XNetIce::Peer* XNetIce::FindPeerLocked(uint32_t online_ip_nbo,
                                       XNetPeerType type) {
  auto& table = peers_[static_cast<size_t>(type)];
  for (auto& peer : table) {
    if (peer.in_use && peer.online_ip_nbo == online_ip_nbo) {
      return &peer;
    }
  }
  return nullptr;
}

XNetIce::Peer* XNetIce::AllocPeerLocked(uint32_t online_ip_nbo,
                                        XNetPeerType type) {
  if (auto* existing = FindPeerLocked(online_ip_nbo, type)) {
    return existing;
  }
  auto& table = peers_[static_cast<size_t>(type)];
  for (auto& peer : table) {
    if (!peer.in_use) {
      peer = {};
      peer.in_use = true;
      peer.online_ip_nbo = online_ip_nbo;
      peer.peer_id = OnlineIpToPeerId(online_ip_nbo);
      peer.type = type;
      peer.status = XNetIceConnectStatus::kPending;
      return &peer;
    }
  }
  return nullptr;
}

void XNetIce::FlushPendingCandidatesLocked(Peer* peer) {
  if (!peer || !peer->agent) {
    return;
  }
  for (const auto& candidate : peer->pending_candidates) {
    juice_add_remote_candidate(peer->agent, candidate.c_str());
  }
  peer->pending_candidates.clear();
  if (peer->remote_gathering_done) {
    juice_set_remote_gathering_done(peer->agent);
  }
}

juice_agent* XNetIce::DetachPeerAgentLocked(Peer* peer) {
  if (!peer) {
    return nullptr;
  }
  juice_agent* agent = peer->agent;
  peer->agent = nullptr;
  peer->we_are_offerer = false;
  peer->status = XNetIceConnectStatus::kPending;
  // Keep pending_candidates / remote_gathering_done for a replacement agent.
  return agent;
}

void XNetIce::StartOfferLocked(Peer* peer) {
  if (!peer || peer->agent) {
    return;
  }
  juice_config_t config = {};
  config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
  config.stun_server_host = stun_host_.c_str();
  config.stun_server_port = stun_port_;
  config.cb_state_changed =
      reinterpret_cast<juice_cb_state_changed_t>(OnJuiceStateChanged);
  config.cb_candidate = reinterpret_cast<juice_cb_candidate_t>(OnJuiceCandidate);
  config.cb_gathering_done =
      reinterpret_cast<juice_cb_gathering_done_t>(OnJuiceGatheringDone);
  config.cb_recv = reinterpret_cast<juice_cb_recv_t>(OnJuiceRecv);
  config.user_ptr = peer;

  juice_agent_t* agent = juice_create(&config);
  if (!agent) {
    peer->status = XNetIceConnectStatus::kFailed;
    XELOGW("XNetIce create agent failed peer={} type={}", peer->peer_id,
           PeerTypeName(peer->type));
    return;
  }
  peer->agent = reinterpret_cast<juice_agent*>(agent);
  peer->we_are_offerer = true;
  peer->status = XNetIceConnectStatus::kPending;
  XELOGI("XNetIce offer start peer={} type={}", peer->peer_id,
         PeerTypeName(peer->type));
  if (juice_gather_candidates(agent) < 0) {
    peer->agent = nullptr;
    juice_destroy(agent);
    peer->status = XNetIceConnectStatus::kFailed;
    XELOGW("XNetIce gather_candidates failed peer={}", peer->peer_id);
    return;
  }
  char sdp[JUICE_MAX_SDP_STRING_LEN];
  if (juice_get_local_description(agent, sdp, sizeof(sdp)) == 0 && signalling_) {
    XELOGI("XNetIce send offer peer={} sdp_len={}", peer->peer_id,
           std::strlen(sdp));
    signalling_->SendOffer(peer->peer_id, peer->type, sdp);
  } else {
    XELOGW("XNetIce get_local_description/send offer failed peer={}",
           peer->peer_id);
  }
}

void XNetIce::Establish(uint32_t online_ip_nbo, XNetPeerType type) {
  if (!cvars::ice_enabled || !online_ip_nbo) {
    return;
  }
  {
    std::lock_guard lock(mutex_);
    if (!initialized_) {
      XELOGW("XNetIce Establish before init peer={}",
             OnlineIpToPeerId(online_ip_nbo));
      return;
    }
    Peer* peer = AllocPeerLocked(online_ip_nbo, type);
    if (!peer) {
      XELOGW("XNetIce peer table full");
      return;
    }
    XELOGI("XNetIce Establish peer={} type={} has_agent={} status={}",
           peer->peer_id, PeerTypeName(type), peer->agent != nullptr,
           IceStatusName(peer->status));
    if (!peer->agent) {
      StartOfferLocked(peer);
    }
  }
}

void XNetIce::ClosePeer(uint32_t online_ip_nbo, XNetPeerType type) {
  juice_agent* to_destroy = nullptr;
  {
    std::lock_guard lock(mutex_);
    Peer* peer = FindPeerLocked(online_ip_nbo, type);
    if (!peer) {
      return;
    }
    XELOGI("XNetIce ClosePeer peer={} type={} status={}", peer->peer_id,
           PeerTypeName(type), IceStatusName(peer->status));
    to_destroy = peer->agent;
    peer->agent = nullptr;
    *peer = {};
  }
  if (to_destroy) {
    juice_destroy(to_destroy);
  }
}

bool XNetIce::HasPeer(uint32_t online_ip_nbo, XNetPeerType type) {
  std::lock_guard lock(mutex_);
  return FindPeerLocked(online_ip_nbo, type) != nullptr;
}

XNetIceConnectStatus XNetIce::GetConnectStatus(uint32_t online_ip_nbo,
                                               XNetPeerType type) {
  std::lock_guard lock(mutex_);
  Peer* peer = FindPeerLocked(online_ip_nbo, type);
  return peer ? peer->status : XNetIceConnectStatus::kIdle;
}

int XNetIce::SendMux(uint32_t online_ip_nbo, uint8_t type, uint16_t id,
                     const uint8_t* data, size_t len) {
  std::lock_guard lock(mutex_);
  Peer* peer = FindPeerLocked(online_ip_nbo, XNetPeerType::kTitle);
  if (!peer || !peer->agent || peer->status != XNetIceConnectStatus::kConnected) {
    XELOGW("XNetIce SendMux drop peer={} status={} type={} id={} len={}",
           OnlineIpToPeerId(online_ip_nbo),
           peer ? IceStatusName(peer->status) : "missing", type, id, len);
    return -1;
  }
  std::vector<uint8_t> packet(4 + len);
  packet[0] = type;
  packet[1] = 0;
  packet[2] = static_cast<uint8_t>((id >> 8) & 0xff);
  packet[3] = static_cast<uint8_t>(id & 0xff);
  if (len) {
    std::memcpy(packet.data() + 4, data, len);
  }
  int rc = juice_send(peer->agent, reinterpret_cast<const char*>(packet.data()),
                      packet.size());
  if (rc != 0) {
    XELOGW("XNetIce juice_send failed peer={} rc={} len={}", peer->peer_id, rc,
           packet.size());
  }
  return rc == 0 ? static_cast<int>(len) : -1;
}

int XNetIce::SendQos(uint32_t online_ip_nbo, const uint8_t* data, size_t len) {
  std::lock_guard lock(mutex_);
  Peer* peer = FindPeerLocked(online_ip_nbo, XNetPeerType::kQos);
  if (!peer || !peer->agent || peer->status != XNetIceConnectStatus::kConnected) {
    return -1;
  }
  // QoS uses 2-byte BE logical port 1005 then payload.
  std::vector<uint8_t> packet(2 + len);
  packet[0] = 0x03;
  packet[1] = 0xED;
  if (len) {
    std::memcpy(packet.data() + 2, data, len);
  }
  int rc = juice_send(peer->agent, reinterpret_cast<const char*>(packet.data()),
                      packet.size());
  return rc == 0 ? static_cast<int>(len) : -1;
}

void XNetIce::OnOffer(const XNetSignallingOffer& msg) {
  const uint32_t ip = PeerIdToOnlineIp(msg.from_peer_id);
  char answer_sdp[JUICE_MAX_SDP_STRING_LEN] = {};
  bool send_answer = false;
  XNetPeerType type = msg.from_peer_type;
  std::string target = msg.from_peer_id;
  std::vector<juice_agent*> destroy_agents;

  XELOGI("XNetIce recv offer from={} type={} sdp_len={}", msg.from_peer_id,
         PeerTypeName(type), msg.sdp.size());

  {
    std::lock_guard lock(mutex_);
    Peer* peer = AllocPeerLocked(ip, type);
    if (!peer) {
      XELOGW("XNetIce OnOffer peer table full from={}", msg.from_peer_id);
      return;
    }
    // If we already started as offerer (glare) or a prior offer failed mid-way,
    // drop the old agent and become the answerer.
    if (peer->agent) {
      XELOGW("XNetIce OnOffer replacing existing agent from={} was_offerer={}",
             msg.from_peer_id, peer->we_are_offerer);
      if (juice_agent* old = DetachPeerAgentLocked(peer)) {
        destroy_agents.push_back(old);
      }
    }
    juice_config_t config = {};
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
    config.stun_server_host = stun_host_.c_str();
    config.stun_server_port = stun_port_;
    config.cb_state_changed =
        reinterpret_cast<juice_cb_state_changed_t>(OnJuiceStateChanged);
    config.cb_candidate =
        reinterpret_cast<juice_cb_candidate_t>(OnJuiceCandidate);
    config.cb_gathering_done =
        reinterpret_cast<juice_cb_gathering_done_t>(OnJuiceGatheringDone);
    config.cb_recv = reinterpret_cast<juice_cb_recv_t>(OnJuiceRecv);
    config.user_ptr = peer;
    peer->agent = juice_create(&config);
    peer->we_are_offerer = false;
    if (!peer->agent) {
      XELOGW("XNetIce OnOffer create agent failed from={}", msg.from_peer_id);
    } else if (juice_set_remote_description(peer->agent, msg.sdp.c_str()) < 0) {
      XELOGW("XNetIce OnOffer set_remote_description failed from={}",
             msg.from_peer_id);
      if (juice_agent* failed = DetachPeerAgentLocked(peer)) {
        destroy_agents.push_back(failed);
      }
    } else if (juice_gather_candidates(peer->agent) < 0) {
      XELOGW("XNetIce OnOffer gather_candidates failed from={}",
             msg.from_peer_id);
      if (juice_agent* failed = DetachPeerAgentLocked(peer)) {
        destroy_agents.push_back(failed);
      }
    } else {
      FlushPendingCandidatesLocked(peer);
      if (juice_get_local_description(peer->agent, answer_sdp,
                                      sizeof(answer_sdp)) == 0) {
        send_answer = true;
      }
    }
  }
  for (juice_agent* agent : destroy_agents) {
    juice_destroy(agent);
  }
  if (send_answer && signalling_) {
    XELOGI("XNetIce send answer to={} sdp_len={}", target,
           std::strlen(answer_sdp));
    signalling_->SendAnswer(target, type, answer_sdp);
  } else if (!send_answer) {
    XELOGW("XNetIce OnOffer could not send answer to={}", target);
  }
}

void XNetIce::OnAnswer(const XNetSignallingAnswer& msg) {
  const uint32_t ip = PeerIdToOnlineIp(msg.from_peer_id);
  XELOGI("XNetIce recv answer from={} type={} sdp_len={}", msg.from_peer_id,
         PeerTypeName(msg.from_peer_type), msg.sdp.size());
  std::lock_guard lock(mutex_);
  Peer* peer = FindPeerLocked(ip, msg.from_peer_type);
  if (!peer || !peer->agent) {
    XELOGW("XNetIce OnAnswer ignored, no peer/agent from={}", msg.from_peer_id);
    return;
  }
  if (juice_set_remote_description(peer->agent, msg.sdp.c_str()) < 0) {
    XELOGW("XNetIce OnAnswer set_remote_description failed from={}",
           msg.from_peer_id);
    return;
  }
  FlushPendingCandidatesLocked(peer);
}

void XNetIce::OnCandidate(const XNetSignallingCandidate& msg) {
  const uint32_t ip = PeerIdToOnlineIp(msg.from_peer_id);
  XELOGI("XNetIce recv candidate from={} type={} mid={} cand={}",
         msg.from_peer_id, PeerTypeName(msg.from_peer_type), msg.mid,
         msg.candidate);
  std::lock_guard lock(mutex_);
  Peer* peer = AllocPeerLocked(ip, msg.from_peer_type);
  if (!peer) {
    XELOGW("XNetIce OnCandidate peer table full from={}", msg.from_peer_id);
    return;
  }
  if (!peer->agent) {
    peer->pending_candidates.push_back(msg.candidate);
    XELOGI("XNetIce buffered candidate from={} (no remote description yet)",
           msg.from_peer_id);
    return;
  }
  juice_add_remote_candidate(peer->agent, msg.candidate.c_str());
}

void XNetIce::OnGatheringDone(const XNetSignallingGatheringDone& msg) {
  const uint32_t ip = PeerIdToOnlineIp(msg.from_peer_id);
  XELOGI("XNetIce recv gathering_done from={} type={}", msg.from_peer_id,
         PeerTypeName(msg.from_peer_type));
  std::lock_guard lock(mutex_);
  Peer* peer = AllocPeerLocked(ip, msg.from_peer_type);
  if (!peer) {
    return;
  }
  peer->remote_gathering_done = true;
  if (!peer->agent) {
    return;
  }
  juice_set_remote_gathering_done(peer->agent);
}

void XNetIce::OnJuiceStateChanged(juice_agent* agent, int state, void* user) {
  auto* peer = static_cast<Peer*>(user);
  if (!peer) {
    return;
  }
  const auto juice_state = static_cast<juice_state_t>(state);
  auto& self = Instance();
  juice_agent* to_destroy = nullptr;
  XNetIceConnectStatus prev = XNetIceConnectStatus::kIdle;
  XNetIceConnectStatus next = XNetIceConnectStatus::kIdle;
  std::string peer_id;
  XNetPeerType type = XNetPeerType::kTitle;
  bool offerer = false;

  {
    std::lock_guard lock(self.mutex_);
    if (!peer->in_use) {
      return;
    }
    // Ignore stale callbacks after the agent was replaced or torn down.
    if (peer->agent != agent) {
      return;
    }

    prev = peer->status;
    peer_id = peer->peer_id;
    type = peer->type;
    offerer = peer->we_are_offerer;

    if (juice_state == JUICE_STATE_CONNECTED ||
        juice_state == JUICE_STATE_COMPLETED) {
      peer->status = XNetIceConnectStatus::kConnected;
    } else if (juice_state == JUICE_STATE_FAILED ||
               juice_state == JUICE_STATE_DISCONNECTED) {
      // Keep the slot + kFailed for STATUS_LOST, but drop
      // the agent so Establish/StartOfferLocked can retry.
      peer->status = XNetIceConnectStatus::kFailed;
      to_destroy = peer->agent;
      peer->agent = nullptr;
      peer->we_are_offerer = false;
      peer->pending_candidates.clear();
      peer->remote_gathering_done = false;
      offerer = false;
    } else if (juice_state == JUICE_STATE_CONNECTING ||
               juice_state == JUICE_STATE_GATHERING) {
      peer->status = XNetIceConnectStatus::kPending;
    }
    next = peer->status;
  }

  // State transitions are the key join debug signal.
  XELOGI("XNetIce state peer={} type={} juice={} status {} -> {} offerer={}",
         peer_id, PeerTypeName(type), JuiceStateName(juice_state),
         IceStatusName(prev), IceStatusName(next), offerer);

  if (to_destroy) {
    // juice_destroy joins the agent thread — never call from this callback.
    std::thread([to_destroy]() {
      juice_destroy(reinterpret_cast<juice_agent_t*>(to_destroy));
    }).detach();
  }
}

void XNetIce::OnJuiceCandidate(juice_agent* agent, const char* sdp, void* user) {
  auto* peer = static_cast<Peer*>(user);
  auto& self = Instance();
  if (!peer || !self.signalling_ || !sdp) {
    return;
  }
  XELOGI("XNetIce local candidate peer={} type={} cand={}", peer->peer_id,
         PeerTypeName(peer->type), sdp);
  self.signalling_->SendCandidate(peer->peer_id, peer->type, sdp, "0");
}

void XNetIce::OnJuiceGatheringDone(juice_agent* agent, void* user) {
  auto* peer = static_cast<Peer*>(user);
  auto& self = Instance();
  if (!peer || !self.signalling_) {
    return;
  }
  XELOGI("XNetIce local gathering_done peer={} type={}", peer->peer_id,
         PeerTypeName(peer->type));
  self.signalling_->SendGatheringComplete(peer->peer_id, peer->type);
}

void XNetIce::OnJuiceRecv(juice_agent* agent, const char* data, size_t size,
                          void* user) {
  auto* peer = static_cast<Peer*>(user);
  auto& self = Instance();
  if (!peer || !data || size == 0) {
    return;
  }

  if (peer->type == XNetPeerType::kQos) {
    if (size < 2) {
      return;
    }
    XELOGD("XNetIce recv qos peer={} len={}", peer->peer_id, size);
    QosRecvHandler handler;
    {
      std::lock_guard lock(self.mutex_);
      handler = self.qos_recv_;
    }
    if (handler) {
      handler(peer->online_ip_nbo,
              reinterpret_cast<const uint8_t*>(data + 2), size - 2);
    }
    return;
  }

  if (size < 4) {
    return;
  }
  XNetMuxPacket packet;
  packet.type = static_cast<uint8_t>(data[0]);
  packet.id = static_cast<uint16_t>((static_cast<uint8_t>(data[2]) << 8) |
                                    static_cast<uint8_t>(data[3]));
  packet.from_online_ip = peer->online_ip_nbo;
  packet.payload.assign(reinterpret_cast<const uint8_t*>(data + 4),
                        reinterpret_cast<const uint8_t*>(data + size));

  XELOGD("XNetIce recv mux peer={} type={} id={} len={}", peer->peer_id,
         packet.type, packet.id, packet.payload.size());

  TitleRecvHandler handler;
  {
    std::lock_guard lock(self.mutex_);
    handler = self.title_recv_;
  }
  if (handler) {
    handler(packet);
  }
}

}  // namespace kernel
}  // namespace xe
