/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnet_qos.h"

#include "xenia/base/logging.h"
#include "xenia/kernel/xnet_ice.h"

#include <chrono>
#include <cstring>

namespace xe {
namespace kernel {

namespace {
uint64_t NowMsec() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}
}  // namespace

XNetQos& XNetQos::Instance() {
  static XNetQos instance;
  return instance;
}

void XNetQos::Initialize() {
  std::lock_guard lock(mutex_);
  initialized_ = true;
  XNetIce::Instance().SetQosRecvHandler(
      [this](uint32_t from, const uint8_t* data, size_t len) {
        OnIcePacket(from, data, len);
      });
}

void XNetQos::Shutdown() {
  std::lock_guard lock(mutex_);
  listeners_.clear();
  lookups_.clear();
  initialized_ = false;
}

void XNetQos::OnIcePacket(uint32_t from_online_ip, const uint8_t* data,
                          size_t len) {
  std::lock_guard lock(mutex_);
  if (inbound_.size() >= 256) {
    return;
  }
  QueuedPacket packet;
  packet.from_online_ip = from_online_ip;
  packet.data.assign(data, data + len);
  inbound_.push_back(std::move(packet));
}

int XNetQos::Listen(uint64_t session_id, const uint8_t* data, uint32_t data_size,
                    uint32_t bits_per_sec, uint32_t flags) {
  std::lock_guard lock(mutex_);
  auto& listener = listeners_[session_id];
  listener.session_id = session_id;
  if (flags & 0x01) {  // ENABLE
    listener.active = true;
  }
  if (flags & 0x02) {  // DISABLE
    listener.active = false;
  }
  if (flags & 0x04) {  // SET_DATA
    listener.data.assign(data, data + data_size);
    listener.active = true;
  }
  if (flags & 0x08) {  // SET_BITSPERSEC
    listener.bits_per_sec = bits_per_sec ? bits_per_sec : 20480;
  }
  if (flags & 0x10) {  // RELEASE
    listeners_.erase(session_id);
  }
  return 0;
}

uint32_t XNetQos::StartLookup(const std::vector<uint32_t>& online_ips_nbo,
                              const std::vector<uint64_t>& session_ids,
                              uint32_t probes, uint32_t bits_per_sec) {
  std::lock_guard lock(mutex_);
  XNetQosPendingLookup lookup;
  lookup.lookup_id = next_lookup_id_++;
  if (lookup.lookup_id == 0) {
    lookup.lookup_id = next_lookup_id_++;
  }
  lookup.bits_per_sec = bits_per_sec;
  const size_t count = online_ips_nbo.size();
  for (size_t i = 0; i < count; ++i) {
    XNetQosPendingTarget target;
    target.online_ip_nbo = online_ips_nbo[i];
    target.session_id = i < session_ids.size() ? session_ids[i] : 0;
    target.probe_count = probes ? probes : 1;
    target.ice_start_msec = NowMsec();
    if (target.online_ip_nbo) {
      XNetIce::Instance().Establish(target.online_ip_nbo, XNetPeerType::kQos);
    }
    lookup.targets.push_back(std::move(target));
  }
  const uint32_t id = lookup.lookup_id;
  lookups_[id] = std::move(lookup);
  return id;
}

bool XNetQos::GetLookupResults(uint32_t lookup_id,
                               std::vector<XNetQosPendingTarget>* out) {
  std::lock_guard lock(mutex_);
  auto it = lookups_.find(lookup_id);
  if (it == lookups_.end()) {
    return false;
  }
  if (out) {
    *out = it->second.targets;
  }
  return true;
}

bool XNetQos::PeerInUseLocked(uint32_t online_ip_nbo,
                              uint32_t except_lookup_id) const {
  for (const auto& [id, lookup] : lookups_) {
    if (id == except_lookup_id) {
      continue;
    }
    for (const auto& target : lookup.targets) {
      if (target.online_ip_nbo == online_ip_nbo && !target.complete) {
        return true;
      }
    }
  }
  return false;
}

void XNetQos::ReleaseLookup(uint32_t lookup_id) {
  std::vector<uint32_t> close_ips;
  {
    std::lock_guard lock(mutex_);
    auto it = lookups_.find(lookup_id);
    if (it == lookups_.end()) {
      return;
    }
    for (auto& t : it->second.targets) {
      if (t.online_ip_nbo && !PeerInUseLocked(t.online_ip_nbo, lookup_id)) {
        close_ips.push_back(t.online_ip_nbo);
      }
    }
    lookups_.erase(it);
  }
  for (uint32_t ip : close_ips) {
    XNetIce::Instance().ClosePeer(ip, XNetPeerType::kQos);
  }
}

bool XNetQos::GetListenStats(uint64_t session_id, uint32_t* probes_recv,
                             uint32_t* data_replies) {
  std::lock_guard lock(mutex_);
  auto it = listeners_.find(session_id);
  if (it == listeners_.end()) {
    return false;
  }
  if (probes_recv) {
    *probes_recv = it->second.probes_recv;
  }
  if (data_replies) {
    *data_replies = it->second.data_replies;
  }
  return true;
}

void XNetQos::SendProbe(XNetQosPendingLookup& lookup,
                        XNetQosPendingTarget& target) {
  XNetQosPacketRequest req = {};
  req.magic = kMagic;
  req.type = 1;
  req.session_id = target.session_id;
  req.lookup_id = lookup.lookup_id;
  req.probe_id = target.probes_sent;
  XNetIce::Instance().SendQos(target.online_ip_nbo,
                              reinterpret_cast<const uint8_t*>(&req),
                              sizeof(req));
  target.last_send_msec = NowMsec();
  target.probes_sent++;
}

void XNetQos::SendResponse(uint32_t to_online_ip,
                           const XNetQosPacketRequest& req,
                           const XNetQosListener& listener) {
  XNetQosPacketResponseHeader hdr = {};
  hdr.magic = kMagic;
  hdr.type = 2;
  hdr.session_id = req.session_id;
  hdr.lookup_id = req.lookup_id;
  hdr.probe_id = req.probe_id;
  hdr.data_size = static_cast<uint16_t>(
      std::min<size_t>(listener.data.size(), 0xffff));
  hdr.enabled = listener.active ? 1 : 0;
  std::vector<uint8_t> packet(sizeof(hdr) + hdr.data_size);
  std::memcpy(packet.data(), &hdr, sizeof(hdr));
  if (hdr.data_size) {
    std::memcpy(packet.data() + sizeof(hdr), listener.data.data(),
                hdr.data_size);
  }
  XNetIce::Instance().SendQos(to_online_ip, packet.data(), packet.size());
}

void XNetQos::ProcessPacket(uint32_t from_online_ip, const uint8_t* data,
                            size_t len) {
  if (len < sizeof(XNetQosPacketRequest)) {
    return;
  }
  uint32_t magic = 0;
  std::memcpy(&magic, data, sizeof(magic));
  if (magic != kMagic) {
    return;
  }
  const uint8_t type = data[4];
  std::lock_guard lock(mutex_);
  if (type == 1) {
    XNetQosPacketRequest req;
    std::memcpy(&req, data, sizeof(req));
    auto it = listeners_.find(req.session_id);
    if (it == listeners_.end()) {
      // No listener for this xnkid — do not reply (seeker sees unreachable).
      return;
    }
    it->second.probes_recv++;
    if (it->second.active) {
      it->second.data_replies++;
    }
    SendResponse(from_online_ip, req, it->second);
    return;
  }
  if (type == 2) {
    if (len < sizeof(XNetQosPacketResponseHeader)) {
      return;
    }
    XNetQosPacketResponseHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));
    auto lit = lookups_.find(hdr.lookup_id);
    if (lit == lookups_.end()) {
      return;
    }
    for (auto& target : lit->second.targets) {
      // Prefer online IP match; session_id is echoed from our probe.
      if (target.online_ip_nbo != from_online_ip || target.complete) {
        continue;
      }
      target.probes_recv++;
      target.disabled = hdr.enabled == 0;
      const uint64_t rtt = NowMsec() - target.last_send_msec;
      const uint16_t rtt16 =
          static_cast<uint16_t>(std::min<uint64_t>(rtt, 0xffff));
      if (!target.rtt_min_ms || rtt16 < target.rtt_min_ms) {
        target.rtt_min_ms = rtt16;
      }
      target.rtt_med_ms = rtt16;
      if (hdr.enabled && hdr.data_size &&
          len >= sizeof(hdr) + hdr.data_size) {
        target.reply.assign(data + sizeof(hdr),
                            data + sizeof(hdr) + hdr.data_size);
      }
      target.complete = true;
      break;
    }
  }
}

void XNetQos::Tick() {
  std::vector<QueuedPacket> inbound;
  {
    std::lock_guard lock(mutex_);
    inbound.swap(inbound_);
  }
  for (auto& packet : inbound) {
    ProcessPacket(packet.from_online_ip, packet.data.data(),
                  packet.data.size());
  }

  std::lock_guard lock(mutex_);
  const uint64_t now = NowMsec();
  for (auto& [id, lookup] : lookups_) {
    for (auto& target : lookup.targets) {
      if (target.complete) {
        continue;
      }
      const auto status = XNetIce::Instance().GetConnectStatus(
          target.online_ip_nbo, XNetPeerType::kQos);
      if (status != XNetIceConnectStatus::kConnected) {
        // ICE failure / connect timeout → complete with no contact (unreachable),
        // never TARGET_DISABLED.
        if (status == XNetIceConnectStatus::kFailed ||
            now - target.ice_start_msec > 10000) {
          target.complete = true;
        }
        continue;
      }
      if (target.probes_sent < target.probe_count) {
        if (!target.last_send_msec || now - target.last_send_msec >= 500) {
          SendProbe(lookup, target);
        }
      } else if (now - target.last_send_msec > 5000) {
        // Probes sent, no reply → unreachable, not refused.
        target.complete = true;
      }
    }
  }
}

}  // namespace kernel
}  // namespace xe
