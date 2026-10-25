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

#include <atomic>
#include <thread>

#include "xenia/kernel/socket_transport.h"

namespace xe {
namespace kernel {

// Carries an XSocket's traffic over a host socket.
class HostTransport final : public SocketTransport {
 public:
  HostTransport() = default;
  // Wraps a socket returned by Accept.
  HostTransport(uint64_t native_handle, bool datagram);
  ~HostTransport() override;

  uint64_t native_handle() const { return native_handle_; }

  bool Open(int af, int type, int protocol) override;
  int Close() override;

  int GetOption(int level, int optname, void* value, socklen_t* len) override;
  int SetOption(int level, int optname, const void* value,
                socklen_t len) override;
  int IOControl(uint32_t cmd, uint32_t* arg_ptr) override;
  int EventSelect(uint64_t native_event_handle, uint32_t flags,
                  std::function<void()> signal) override;

  int Bind(const sockaddr* addr, socklen_t len) override;
  int Connect(const sockaddr* addr) override;
  int Listen(int backlog) override;
  std::unique_ptr<SocketTransport> Accept(sockaddr* addr,
                                          socklen_t* len) override;
  int Shutdown(int how) override;

  int GetPeerName(sockaddr* addr, socklen_t* len) override;
  int GetSockName(sockaddr* addr, socklen_t* len) override;
  uint16_t GetBoundPort() const override;

  int Recv(uint8_t* buf, uint32_t len) override;
  int RecvFrom(uint8_t* buf, uint32_t len, sockaddr* from,
               socklen_t* from_len) override;
  int Send(const uint8_t* buf, uint32_t len) override;
  int SendTo(const uint8_t* buf, uint32_t len, const sockaddr* to,
             socklen_t to_len) override;

  int SendBuffers(const std::vector<HostBuffer>& buffers, const sockaddr* to,
                  int to_len, uint32_t* bytes_sent) override;
  int ReceiveBuffer(const HostBuffer& buffer, sockaddr* from, int* from_len,
                    uint32_t* bytes_received, uint32_t* flags) override;

  int Select(bool write, long timeout_us) override;

 private:
  void ReportTimeout(int ret, uint32_t timeout_ms) const;
  void RememberPeer();
  void StopEventWatcher();
  void WatchEvents();

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

  // Linux has no WSAEventSelect, so a thread signals the event instead.
  std::thread event_watcher_;
  std::atomic<bool> stop_event_watcher_ = false;
  uint32_t event_flags_ = 0;
  std::function<void()> event_signal_;
  // Cleared once the event is signalled, and set again by a receive or once
  // the socket has nothing left to read.
  std::atomic<bool> read_armed_ = true;
  // Cleared once the event is signalled, and set again by a send that
  // couldn't complete.
  std::atomic<bool> write_armed_ = true;
  // Cleared once the peer's close is signalled.
  std::atomic<bool> close_armed_ = true;
  std::atomic<bool> connect_pending_ = false;
  std::atomic<bool> connected_ = false;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HOST_TRANSPORT_H_
