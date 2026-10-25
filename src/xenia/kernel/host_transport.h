/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_HOST_TRANSPORT_H_
#define XENIA_KERNEL_HOST_TRANSPORT_H_

#include <cstdint>
#include <vector>

#include "xenia/xbox.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
#include "xenia/base/platform.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xe {
namespace kernel {

// A buffer of an overlapped send, already translated to host memory.
struct HostBuffer {
  char* data;
  uint32_t len;
};

// Carries an XSocket's traffic over a host socket. Every call works like its
// Winsock counterpart: it returns a negative value on failure and leaves the
// error in the last error, where XSocket::XWSAGetLastError reads it.
class HostTransport {
 public:
  HostTransport() = default;
  // Wraps a socket returned by Accept.
  HostTransport(uint64_t native_handle, bool datagram);

  uint64_t native_handle() const { return native_handle_; }

  bool Open(int af, int type, int protocol);
  int Close();

  // level and optname are the host's values.
  int GetOption(int level, int optname, void* value, socklen_t* len);
  int SetOption(int level, int optname, const void* value, socklen_t len);
  // cmd and *arg_ptr are the guest's values.
  int IOControl(uint32_t cmd, uint32_t* arg_ptr);
  int EventSelect(uint64_t native_event_handle, uint32_t flags);

  int Bind(const sockaddr* addr, socklen_t len);
  int Connect(const sockaddr* addr);
  int Listen(int backlog);
  uint64_t Accept(sockaddr* addr, socklen_t* len);
  int Shutdown(int how);

  int GetPeerName(sockaddr* addr, socklen_t* len);
  int GetSockName(sockaddr* addr, socklen_t* len);
  // The port the host bound, in network byte order, or 0.
  uint16_t GetBoundPort() const;

  int Recv(uint8_t* buf, uint32_t len);
  int RecvFrom(uint8_t* buf, uint32_t len, sockaddr* from, socklen_t* from_len);
  int Send(const uint8_t* buf, uint32_t len);
  int SendTo(const uint8_t* buf, uint32_t len, const sockaddr* to,
             socklen_t to_len);

  // Overlapped I/O. Both return 0 or X_SOCKET_ERROR.
  int SendBuffers(const std::vector<HostBuffer>& buffers, const sockaddr* to,
                  int to_len, uint32_t* bytes_sent);
  int ReceiveBuffer(const HostBuffer& buffer, sockaddr* from, int* from_len,
                    uint32_t* bytes_received, uint32_t* flags);

  // Waits up to timeout_us for the socket to become readable or writable.
  // Returns the select result.
  int Select(bool write, long timeout_us);

 private:
  void ReportTimeout(int ret, uint32_t timeout_ms) const;
  void RememberPeer();

  uint64_t native_handle_ = X_INVALID_SOCKET;
  bool datagram_ = false;

  bool event_select_active_ = false;
  bool shutdown_send_ = false;

  uint32_t receive_timeout_ms_ = 0;
  uint32_t send_timeout_ms_ = 0;
  // SO_EXCLUSIVEADDRUSE, which Linux doesn't have.
  bool exclusive_address_use_ = false;

  // The connected peer. Linux forgets it once the connection is reset.
  sockaddr_storage peer_ = {};
  socklen_t peer_len_ = 0;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HOST_TRANSPORT_H_
