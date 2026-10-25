/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/host_transport.h"

#include "xenia/base/byte_order.h"

// The calls that differ between hosts are in host_transport_win.cc and
// host_transport_posix.cc.

namespace xe {
namespace kernel {

uint64_t HostTransport::Accept(sockaddr* addr, socklen_t* len) {
  return accept(native_handle_, addr, len);
}

int HostTransport::Shutdown(int how) {
  const int ret = shutdown(native_handle_, how);

  if (!ret && how != 0) {
    shutdown_send_ = true;
  }

  return ret;
}

int HostTransport::GetSockName(sockaddr* addr, socklen_t* len) {
  return getsockname(native_handle_, addr, len);
}

void HostTransport::RememberPeer() {
  peer_len_ = sizeof(peer_);

  if (getpeername(native_handle_, reinterpret_cast<sockaddr*>(&peer_),
                  &peer_len_) < 0) {
    peer_len_ = 0;
  }
}

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
  const int ret = sendto(native_handle_, reinterpret_cast<const char*>(buf),
                         len, 0, to, to_len);

  ReportTimeout(ret, send_timeout_ms_);

  return ret;
}

}  // namespace kernel
}  // namespace xe
