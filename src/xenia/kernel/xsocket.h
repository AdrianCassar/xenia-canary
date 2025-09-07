/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSOCKET_H_
#define XENIA_KERNEL_XSOCKET_H_

#include "xenia/base/byte_order.h"

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

enum class X_WSAError : uint32_t {
  X_WSA_INVALID_PARAMETER = 0x0057,
  X_WSA_OPERATION_ABORTED = 0x03E3,
  X_WSA_IO_INCOMPLETE = 0x03E4,
  X_WSA_IO_PENDING = 0x03E5,
  X_WSAEACCES = 0x271D,
  X_WSAEFAULT = 0x271E,
  X_WSAEINVAL = 0x2726,
  X_WSAEWOULDBLOCK = 0x2733,
  X_WSAENOTSOCK = 0x2736,
  X_WSAEMSGSIZE = 0x2738,
  X_WSAENETDOWN = 0x2742,
  X_WSANO_DATA = 0x2AFC,
  X_WSANOTINITIALISED = 0x276D,
  X_WSAEADDRINUSE = 0x2740,
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
  xe::be<uint32_t> internal;
  xe::be<uint32_t> internal_high;
  xe::be<uint32_t> offset;
  xe::be<uint32_t> offset_high;
  xe::be<uint32_t> event_handle;
};
static_assert_size(XWSAOVERLAPPED, 0x14);

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSOCKET_H_
