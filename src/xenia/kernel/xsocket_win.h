/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSOCKET_WIN_H_
#define XENIA_KERNEL_XSOCKET_WIN_H_

#include <cstring>
#include <future>
#include <queue>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xsocket.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include "xenia/base/platform_win.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xe {
namespace kernel {

class XSocket : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Socket;

  enum AddressFamily {
    X_AF_INET = 2,
  };

  enum Type {
    X_SOCK_STREAM = 1,
    X_SOCK_DGRAM = 2,
  };

  enum Protocol {
    X_IPPROTO_TCP = 6,
    X_IPPROTO_UDP = 17,

    // LIVE Voice and Data Protocol
    // https://blog.csdn.net/baozi3026/article/details/4277227
    // Format: [cbGameData][GameData(encrypted)][VoiceData(unencrypted)]
    X_IPPROTO_VDP = 254,
  };

  XSocket(KernelState* kernel_state);
  ~XSocket();

  uint64_t native_handle() const { return native_handle_; }
  uint16_t bound_port() const { return bound_port_; }

  virtual X_STATUS Initialize(AddressFamily af, Type type, Protocol proto);

  virtual X_STATUS Close();

  virtual X_STATUS GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                             uint32_t* optlen);

  virtual X_STATUS SetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                             uint32_t optlen);

  virtual X_STATUS IOControl(uint32_t cmd, uint8_t* arg_ptr);

  virtual X_STATUS Connect(const XSOCKADDR_IN* name, int name_len);

  virtual X_STATUS Bind(const XSOCKADDR_IN* name, int name_len);

  virtual X_STATUS Listen(int backlog);

  virtual X_STATUS GetPeerName(XSOCKADDR_IN* name, int* name_len);

  virtual X_STATUS GetSockName(XSOCKADDR_IN* buf, int* buf_len);

  virtual object_ref<XSocket> Accept(XSOCKADDR_IN* name, int* name_len);

  virtual int Shutdown(int how);

  virtual int Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags);

  virtual int Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags);

  virtual int RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                       XSOCKADDR_IN* from, uint32_t* from_len);

  virtual int SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                     XSOCKADDR_IN* to, uint32_t to_len);

  virtual int WSAEventSelect(uint64_t socket_handle, uint64_t event_handle,
                             uint32_t flags);

  virtual int WSARecvFrom(XWSABUF* buffers, uint32_t num_buffers,
                          xe::be<uint32_t>* num_bytes_recv_ptr,
                          xe::be<uint32_t>* flags_ptr, XSOCKADDR_IN* from_ptr,
                          xe::be<uint32_t>* fromlen_ptr,
                          XWSAOVERLAPPED* overlapped_ptr);

  virtual bool WSAGetOverlappedResult(XWSAOVERLAPPED* overlapped_ptr,
                                      xe::be<uint32_t>* bytes_transferred,
                                      bool wait, xe::be<uint32_t>* flags_ptr);

  virtual uint32_t GetLastWSAError() const;

 private:
  XSocket(KernelState* kernel_state, uint64_t native_handle);
  uint64_t native_handle_ = -1;

  AddressFamily af_;    // Address family
  Type type_;           // Type (DGRAM/Stream/etc)
  Protocol proto_;      // Protocol (TCP/UDP/etc)
  bool secure_ = true;  // Secure socket (encryption enabled)

  bool bound_ = false;  // Explicitly bound to an IP address?

  // Special exception for port!
  // port is always stored in NBO (Network byte order).
  // which is basically BE.
  xe::be<uint16_t> bound_port_ = 0;

  bool broadcast_socket_ = false;

  std::unique_ptr<xe::threading::Event> event_;
  std::mutex incoming_packet_mutex_;
  std::queue<uint8_t*> incoming_packets_;

  std::future<int> polling_task_;

  std::mutex receive_mutex_;
  std::condition_variable receive_cv_;
  std::mutex receive_socket_mutex_;
  XWSAOVERLAPPED* active_overlapped_ = nullptr;

  int PollWSARecvFrom(bool wait, struct WSARecvFromData data);

  void SetLastWSAError(X_WSAError) const;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSOCKET_WIN_H_
