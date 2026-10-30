/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/host_transport.h"

#include <algorithm>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xsocket.h"

// The calls that differ between hosts are in host_transport_win.cc and
// host_transport_posix.cc.

namespace xe {
namespace kernel {

namespace {

// Every transport is a HostTransport until another kind exists.
uint64_t HandleOf(const SocketTransport* transport) {
  return static_cast<const HostTransport*>(transport)->native_handle();
}

// Winsock's FD_ISSET takes a non-const set.
bool IsSet(uint64_t handle, const fd_set& set) {
  return FD_ISSET(handle, const_cast<fd_set*>(&set));
}

}  // namespace

int ReportHostError(int ret) {
  if (ret < 0) {
    XSocket::XWSASetLastError(HostLastError());
  }

  return ret;
}

std::unique_ptr<SocketTransport> HostTransport::Accept(sockaddr* addr,
                                                       socklen_t* len) {
  // FD_ACCEPT is signalled again for the next pending connection.
  read_armed_ = true;

  const uint64_t accepted = accept(native_handle_, addr, len);
  if (accepted == X_INVALID_SOCKET) {
    ReportHostError(X_SOCKET_ERROR);
    return nullptr;
  }

  return std::make_unique<HostTransport>(accepted, datagram_);
}

int HostTransport::Shutdown(int how) {
  const int ret = ReportHostError(shutdown(native_handle_, how));

  if (!ret && how != 0) {
    shutdown_send_ = true;
  }

  return ret;
}

int HostTransport::GetSockName(sockaddr* addr, socklen_t* len) {
  return ReportHostError(getsockname(native_handle_, addr, len));
}

void HostTransport::RememberPeer() {
  peer_len_ = sizeof(peer_);

  if (getpeername(native_handle_, reinterpret_cast<sockaddr*>(&peer_),
                  &peer_len_) < 0) {
    peer_len_ = 0;
  }
}

// getsockname is called directly, not through ReportHostError, so the last
// error is left alone.
uint16_t HostTransport::GetBoundPort() const {
  sockaddr_storage storage = {};
  socklen_t addr_len = sizeof(storage);

  if (getsockname(native_handle_, reinterpret_cast<sockaddr*>(&storage),
                  &addr_len) == 0) {
    if (storage.ss_family == AF_INET) {
      const sockaddr_in* sockaddr = reinterpret_cast<sockaddr_in*>(&storage);
      return xe::byte_swap(sockaddr->sin_port);
    }
  }

  return 0;
}

int HostTransport::SendTo(const uint8_t* buf, uint32_t len, const sockaddr* to,
                          socklen_t to_len) {
  const int ret = ReportHostError(sendto(
      native_handle_, reinterpret_cast<const char*>(buf), len, 0, to, to_len));

  if (ret < 0) {
    write_armed_ = true;
  }

  ReportTimeout(ret, send_timeout_ms_);

  return ret;
}

void HostTransport::EventReset() { last_queued_bytes_ = QueuedReceiveBytes(); }

void HostTransport::StopEventWatcher() {
  stop_event_watcher_ = true;

  if (event_watcher_.joinable()) {
    event_watcher_.join();
  }
}

TransportSelect::TransportSelect(bool use_read, bool use_write, bool use_except)
    : use_read_(use_read), use_write_(use_write), use_except_(use_except) {
  FD_ZERO(&read_);
  FD_ZERO(&write_);
  FD_ZERO(&except_);
  FD_ZERO(&errored_);
}

void TransportSelect::AddRead(SocketTransport* transport) {
  const uint64_t handle = HandleOf(transport);
  FD_SET(handle, &read_);
  highest_ = std::max(highest_, static_cast<int>(handle));
}

void TransportSelect::AddWrite(SocketTransport* transport) {
  const uint64_t handle = HandleOf(transport);
  FD_SET(handle, &write_);
  FD_SET(handle, &except_);
  highest_ = std::max(highest_, static_cast<int>(handle));
}

void TransportSelect::AddWriteWake(SocketTransport* transport) {
  const uint64_t handle = HandleOf(transport);
  FD_SET(handle, &write_);
  highest_ = std::max(highest_, static_cast<int>(handle));
}

void TransportSelect::AddExcept(SocketTransport* transport) {
  const uint64_t handle = HandleOf(transport);
  FD_SET(handle, &except_);
  except_handles_.push_back(handle);
  highest_ = std::max(highest_, static_cast<int>(handle));
}

bool TransportSelect::IsReadable(SocketTransport* transport) const {
  return IsSet(HandleOf(transport), read_);
}

// A refused connect is writable as well as in the except set.
bool TransportSelect::IsWritable(SocketTransport* transport) const {
  const uint64_t handle = HandleOf(transport);
  return IsSet(handle, write_) || IsSet(handle, except_);
}

bool TransportSelect::IsExcepted(SocketTransport* transport) const {
  const uint64_t handle = HandleOf(transport);
  return IsSet(handle, except_) || IsSet(handle, errored_);
}

}  // namespace kernel
}  // namespace xe
