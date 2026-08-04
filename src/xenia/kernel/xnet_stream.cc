/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnet_stream.h"

#include "xenia/base/logging.h"
#include "xenia/kernel/xsocket.h"

#include <chrono>
#include <cstring>

#include "ikcp.h"

namespace xe {
namespace kernel {

namespace {
ikcpcb* AsKcp(void* p) { return static_cast<ikcpcb*>(p); }
}  // namespace

XNetStream& XNetStream::Instance() {
  static XNetStream instance;
  return instance;
}

void XNetStream::Initialize() {
  std::lock_guard lock(mutex_);
  initialized_ = true;
}

void XNetStream::Shutdown() {
  std::lock_guard lock(mutex_);
  for (auto& [id, stream] : streams_) {
    if (stream.kcp) {
      ikcp_release(AsKcp(stream.kcp));
      stream.kcp = nullptr;
    }
  }
  streams_.clear();
  socket_to_stream_.clear();
  listeners_.clear();
  accept_queues_.clear();
  initialized_ = false;
}

uint16_t XNetStream::AllocStreamIdLocked() {
  for (int i = 0; i < 65535; ++i) {
    uint16_t id = next_stream_id_++;
    if (id == 0) {
      id = next_stream_id_++;
    }
    if (streams_.find(id) == streams_.end()) {
      return id;
    }
  }
  return 0;
}

XNetStream::Stream* XNetStream::FindBySocketLocked(XSocket* socket) {
  auto it = socket_to_stream_.find(socket);
  if (it == socket_to_stream_.end()) {
    return nullptr;
  }
  auto sit = streams_.find(it->second);
  return sit == streams_.end() ? nullptr : &sit->second;
}

XNetStream::Stream* XNetStream::FindByIdLocked(uint16_t stream_id) {
  auto it = streams_.find(stream_id);
  return it == streams_.end() ? nullptr : &it->second;
}

int XNetStream::KcpOutput(const char* buf, int len, void* /*kcp*/, void* user) {
  auto* stream = static_cast<Stream*>(user);
  if (!stream) {
    return -1;
  }
  return XNetIce::Instance().SendMux(stream->remote_ip_nbo, kXNetMuxKcp,
                                     stream->stream_id,
                                     reinterpret_cast<const uint8_t*>(buf),
                                     static_cast<size_t>(len));
}

void XNetStream::SendCtrlLocked(uint32_t online_ip_nbo, CtrlType type,
                                uint16_t stream_id, uint16_t dest_port_nbo,
                                uint16_t src_port_nbo) {
  uint8_t payload[7];
  payload[0] = static_cast<uint8_t>(type);
  payload[1] = static_cast<uint8_t>((stream_id >> 8) & 0xff);
  payload[2] = static_cast<uint8_t>(stream_id & 0xff);
  payload[3] = static_cast<uint8_t>((dest_port_nbo >> 8) & 0xff);
  payload[4] = static_cast<uint8_t>(dest_port_nbo & 0xff);
  payload[5] = static_cast<uint8_t>((src_port_nbo >> 8) & 0xff);
  payload[6] = static_cast<uint8_t>(src_port_nbo & 0xff);
  XNetIce::Instance().SendMux(online_ip_nbo, kXNetMuxCtrl, stream_id, payload,
                              sizeof(payload));
}

void XNetStream::OnMuxPacket(const XNetMuxPacket& packet) {
  if (packet.type == kXNetMuxCtrl) {
    HandleCtrl(packet);
  } else if (packet.type == kXNetMuxKcp) {
    HandleKcp(packet);
  }
}

void XNetStream::HandleCtrl(const XNetMuxPacket& packet) {
  if (packet.payload.size() < 7) {
    return;
  }
  const auto type = static_cast<CtrlType>(packet.payload[0]);
  const uint16_t stream_id =
      static_cast<uint16_t>((packet.payload[1] << 8) | packet.payload[2]);
  const uint16_t dest_port =
      static_cast<uint16_t>((packet.payload[3] << 8) | packet.payload[4]);
  const uint16_t src_port =
      static_cast<uint16_t>((packet.payload[5] << 8) | packet.payload[6]);

  std::lock_guard lock(mutex_);
  if (type == CtrlType::kOpen) {
    auto lit = listeners_.find(dest_port);
    if (lit == listeners_.end()) {
      SendCtrlLocked(packet.from_online_ip, CtrlType::kRst, stream_id, src_port,
                     dest_port);
      return;
    }
    Stream stream;
    stream.stream_id = stream_id;
    stream.remote_ip_nbo = packet.from_online_ip;
    stream.remote_port_nbo = src_port;
    stream.local_port_nbo = dest_port;
    stream.connected = true;
    streams_[stream_id] = stream;
    Stream* stored = &streams_[stream_id];
    stored->kcp = ikcp_create(stream_id, stored);
    ikcp_setoutput(AsKcp(stored->kcp),
                   reinterpret_cast<int (*)(const char*, int, ikcpcb*, void*)>(
                       &XNetStream::KcpOutput));
    ikcp_nodelay(AsKcp(stored->kcp), 1, 10, 2, 1);
    SendCtrlLocked(packet.from_online_ip, CtrlType::kAccept, stream_id,
                   src_port, dest_port);
    accept_queues_[lit->second].push(stream_id);
  } else if (type == CtrlType::kAccept) {
    Stream* stream = FindByIdLocked(stream_id);
    if (stream) {
      stream->connected = true;
    }
  } else if (type == CtrlType::kClose || type == CtrlType::kRst) {
    Stream* stream = FindByIdLocked(stream_id);
    if (stream) {
      stream->connected = false;
      if (stream->kcp) {
        ikcp_release(AsKcp(stream->kcp));
        stream->kcp = nullptr;
      }
      if (stream->socket) {
        socket_to_stream_.erase(stream->socket);
      }
      streams_.erase(stream_id);
    }
  }
}

void XNetStream::HandleKcp(const XNetMuxPacket& packet) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindByIdLocked(packet.id);
  if (!stream || !stream->kcp) {
    return;
  }
  ikcp_input(AsKcp(stream->kcp),
             reinterpret_cast<const char*>(packet.payload.data()),
             static_cast<long>(packet.payload.size()));
  char buf[2048];
  int n;
  while ((n = ikcp_recv(AsKcp(stream->kcp), buf, sizeof(buf))) > 0) {
    stream->recv_queue.emplace(buf, buf + n);
  }
}

void XNetStream::Tick() {
  std::lock_guard lock(mutex_);
  const uint32_t now = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  for (auto& [id, stream] : streams_) {
    if (stream.kcp) {
      ikcp_update(AsKcp(stream.kcp), now);
      char buf[2048];
      int n;
      while ((n = ikcp_recv(AsKcp(stream.kcp), buf, sizeof(buf))) > 0) {
        stream.recv_queue.emplace(buf, buf + n);
      }
    }
  }
}

int XNetStream::Connect(XSocket* socket, uint32_t online_ip_nbo,
                        uint16_t port_nbo) {
  XNetIce::Instance().Establish(online_ip_nbo, XNetPeerType::kTitle);
  std::lock_guard lock(mutex_);
  const uint16_t stream_id = AllocStreamIdLocked();
  if (!stream_id) {
    return -1;
  }
  Stream stream;
  stream.stream_id = stream_id;
  stream.remote_ip_nbo = online_ip_nbo;
  stream.remote_port_nbo = port_nbo;
  stream.local_port_nbo = socket->bound_port();
  stream.socket = socket;
  streams_[stream_id] = stream;
  streams_[stream_id].kcp = ikcp_create(stream_id, &streams_[stream_id]);
  ikcp_setoutput(AsKcp(streams_[stream_id].kcp),
                 reinterpret_cast<int (*)(const char*, int, ikcpcb*, void*)>(
                     &XNetStream::KcpOutput));
  ikcp_nodelay(AsKcp(streams_[stream_id].kcp), 1, 10, 2, 1);
  socket_to_stream_[socket] = stream_id;
  SendCtrlLocked(online_ip_nbo, CtrlType::kOpen, stream_id, port_nbo,
                 socket->bound_port());
  return 0;
}

int XNetStream::Listen(XSocket* socket, uint16_t port_nbo) {
  std::lock_guard lock(mutex_);
  listeners_[port_nbo] = socket;
  return 0;
}

bool XNetStream::TakeAccept(XSocket* listen_socket, uint16_t* out_stream_id,
                            uint32_t* out_ip_nbo, uint16_t* out_port_nbo) {
  std::lock_guard lock(mutex_);
  auto qit = accept_queues_.find(listen_socket);
  if (qit == accept_queues_.end() || qit->second.empty()) {
    return false;
  }
  const uint16_t stream_id = qit->second.front();
  qit->second.pop();
  Stream* stream = FindByIdLocked(stream_id);
  if (!stream) {
    return false;
  }
  if (out_stream_id) {
    *out_stream_id = stream_id;
  }
  if (out_ip_nbo) {
    *out_ip_nbo = stream->remote_ip_nbo;
  }
  if (out_port_nbo) {
    *out_port_nbo = stream->remote_port_nbo;
  }
  return true;
}

void XNetStream::AttachSocket(uint16_t stream_id, XSocket* socket) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindByIdLocked(stream_id);
  if (!stream || !socket) {
    return;
  }
  stream->socket = socket;
  socket_to_stream_[socket] = stream_id;
}

int XNetStream::Send(XSocket* socket, const uint8_t* data, size_t len) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindBySocketLocked(socket);
  if (!stream || !stream->kcp || !stream->connected) {
    return -1;
  }
  int rc = ikcp_send(AsKcp(stream->kcp), reinterpret_cast<const char*>(data),
                     static_cast<int>(len));
  return rc < 0 ? -1 : static_cast<int>(len);
}

int XNetStream::Recv(XSocket* socket, uint8_t* data, size_t len) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindBySocketLocked(socket);
  if (!stream) {
    return -1;
  }
  if (stream->recv_queue.empty()) {
    return -2;  // would block
  }
  auto& front = stream->recv_queue.front();
  const size_t copy = std::min(len, front.size());
  std::memcpy(data, front.data(), copy);
  if (copy == front.size()) {
    stream->recv_queue.pop();
  } else {
    front.erase(front.begin(), front.begin() + static_cast<std::ptrdiff_t>(copy));
  }
  return static_cast<int>(copy);
}

void XNetStream::Close(XSocket* socket) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindBySocketLocked(socket);
  if (!stream) {
    // Maybe a listener
    for (auto it = listeners_.begin(); it != listeners_.end();) {
      if (it->second == socket) {
        it = listeners_.erase(it);
      } else {
        ++it;
      }
    }
    accept_queues_.erase(socket);
    return;
  }
  SendCtrlLocked(stream->remote_ip_nbo, CtrlType::kClose, stream->stream_id,
                 stream->remote_port_nbo, stream->local_port_nbo);
  if (stream->kcp) {
    ikcp_release(AsKcp(stream->kcp));
  }
  const uint16_t id = stream->stream_id;
  socket_to_stream_.erase(socket);
  streams_.erase(id);
}

bool XNetStream::HasPendingAccept(XSocket* listen_socket) {
  std::lock_guard lock(mutex_);
  auto it = accept_queues_.find(listen_socket);
  return it != accept_queues_.end() && !it->second.empty();
}

bool XNetStream::HasRecvData(XSocket* socket) {
  std::lock_guard lock(mutex_);
  Stream* stream = FindBySocketLocked(socket);
  return stream && !stream->recv_queue.empty();
}

}  // namespace kernel
}  // namespace xe
