/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSOCKET_H_
#define XENIA_KERNEL_XSOCKET_H_

#include <cstring>
#include <future>
#include <queue>
#include <set>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xobject.h"

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
enum class X_WSA_ERROR : uint32_t {
  X_WSA_NO_ERROR = 0,
  X_WSA_INVALID_PARAMETER = 87,
  X_WSA_OPERATION_ABORTED = 995,
  X_WSA_IO_INCOMPLETE = 996,
  X_WSA_IO_PENDING = 997,
  X_WSAEACCES = 10013,
  X_WSAEFAULT = 10014,
  X_WSAEINVAL = 10022,
  X_WSAEWOULDBLOCK = 10035,
  X_WSAEINPROGRESS = 10036,
  X_WSAEALREADY = 10037,
  X_WSAENOTSOCK = 10038,
  X_WSAEMSGSIZE = 10040,
  X_WSAENOPROTOOPT = 10042,
  X_WSAEPROTONOSUPPORT = 10043,
  X_WSAESOCKTNOSUPPORT = 10044,
  X_WSAEAFNOSUPPORT = 10047,
  X_WSAEADDRINUSE = 10048,
  X_WSAEADDRNOTAVAIL = 10049,
  X_WSAENETDOWN = 10050,
  X_WSAECONNRESET = 10054,
  X_WSAENOBUFS = 10055,
  X_WSAEISCONN = 10056,
  X_WSAENOTCONN = 10057,
  X_WSAESHUTDOWN = 10058,
  X_WSAETIMEDOUT = 10060,
  X_WSAECONNREFUSED = 10061,
  X_WSAEHOSTUNREACH = 10065,
  X_WSASYSNOTREADY = 10091,
  X_WSAVERNOTSUPPORTED = 10092,
  X_WSANOTINITIALISED = 10093,
  X_WSAECANCELLED = 10103,
  X_WSASYSCALLFAILURE = 10107,
  X_WSAHOST_NOT_FOUND = 11001,
  X_WSATRY_AGAIN = 11002,
  X_WSANO_DATA = 11004,
};

struct XSOCKADDR {
  xe::be<uint16_t> address_family;
  char sa_data[14];
};
static_assert_size(XSOCKADDR, 0x10);

struct XSOCKADDR_IN {
  xe::be<uint16_t> address_family;
  xe::be<uint16_t> address_port;
  in_addr address_ip;
  char sa_zero[8];

  const sockaddr to_host() const {
    sockaddr sa = {};
    std::memcpy(&sa, this, sizeof(sockaddr));

    sa.sa_family = xe::byte_swap(sa.sa_family);
    // port is already in correct endianness
    return sa;
  }

  void to_guest(const sockaddr* host) {
    std::memcpy(this, host, sizeof(sockaddr));
    address_family = host->sa_family;
  }
};

struct XWSABUF {
  xe::be<uint32_t> len;
  xe::be<uint32_t> buf_ptr;
};
static_assert_size(XWSABUF, 0x8);

struct XWSAOVERLAPPED {
  xe::be<uint32_t> internal;       // Status Code
  xe::be<uint32_t> internal_high;  // The amount of bytes sent/recv
  xe::be<uint32_t> offset;         // Flags
  xe::be<uint32_t> offset_high;
  xe::be<uint32_t> event_handle;
};
static_assert_size(XWSAOVERLAPPED, 0x14);

struct WSARecvFromData {
  std::shared_ptr<XWSABUF> buffers;
  uint32_t num_buffers;
  uint32_t* num_bytes_recv;
  uint32_t* flags;
  XSOCKADDR_IN* from;
  int from_len;
  XWSAOVERLAPPED* overlapped;
};

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

  X_STATUS Initialize(AddressFamily af, Type type, Protocol proto);
  X_STATUS Close();

  X_STATUS GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                     uint32_t* optlen);
  X_STATUS SetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                     uint32_t optlen);
  X_STATUS IOControl(uint32_t cmd, uint8_t* arg_ptr);

  X_STATUS Connect(const XSOCKADDR_IN* name, int name_len);
  X_STATUS Bind(const XSOCKADDR_IN* name, int name_len);
  X_STATUS Listen(int backlog);
  X_STATUS GetPeerName(XSOCKADDR_IN* name, int* name_len);
  X_STATUS GetSockName(XSOCKADDR_IN* buf, int* buf_len);

  object_ref<XSocket> Accept(XSOCKADDR_IN* name, int* name_len);
  int Shutdown(int how);

  int Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags);
  int Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags);

  int RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
               XSOCKADDR_IN* from, uint32_t* from_len);
  int SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags, XSOCKADDR_IN* to,
             uint32_t to_len);

  int WSAEventSelect(uint64_t socket_handle, uint64_t event_handle,
                     uint32_t flags);

  int WSASendTo(XWSABUF* buffers, uint32_t num_buffers,
                xe::be<uint32_t>* num_bytes_sent_ptr, uint32_t flags,
                XSOCKADDR_IN* to_ptr, uint32_t to_len,
                XWSAOVERLAPPED* overlapped_ptr);

  int WSAPollWrite(bool wait, X_WSA_ERROR* error);

  int WSAPollRead(bool wait, X_WSA_ERROR* error);

  int WSARecvFrom(XWSABUF* buffers, uint32_t num_buffers,
                  xe::be<uint32_t>* num_bytes_recv_ptr,
                  xe::be<uint32_t>* flags_ptr, XSOCKADDR_IN* from_ptr,
                  xe::be<uint32_t>* fromlen_ptr,
                  XWSAOVERLAPPED* overlapped_ptr);

  bool WSAGetOverlappedResult(XWSAOVERLAPPED* overlapped_ptr,
                              xe::be<uint32_t>* bytes_transferred, bool wait,
                              xe::be<uint32_t>* flags_ptr);

  int WSACancelOverlappedIO();

  static bool XWSAIsKnownError(X_WSA_ERROR error);

  static uint32_t XWSAGetLastError();

  struct packet {
    // These values are in network byte order.
    xe::be<uint16_t> src_port;
    xe::be<uint32_t> src_ip;

    uint16_t data_len;
    uint8_t data[1];
  };

  // Queue a packet into our internal buffer.
  bool QueuePacket(uint32_t src_ip, uint16_t src_port, const uint8_t* buf,
                   size_t len);

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

  std::mutex receive_completion_mutex_;
  std::condition_variable receive_cv_;
  std::mutex receive_socket_mutex_;

  std::atomic<bool> cancel_overlapped_ = false;
  std::set<XWSAOVERLAPPED*> pending_overlapped_io_;

  int PollWSARecvFrom(bool wait, WSARecvFromData data);

  void XWSASetLastError(X_WSA_ERROR) const;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSOCKET_H_