/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "src/xenia/kernel/xsocket.h"

#include <cstring>
#include <ranges>

#include "xenia/base/platform.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"

#if XE_PLATFORM_WIN32
#include <mswsock.h>
#else
#include <fcntl.h>
#endif

DECLARE_bool(bind_interface);

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

// Translate socket options to native
// Note:
// SO_DONTLINGER = ~SO_LINGER
// SO_EXCLUSIVEADDRUSE = ~SO_REUSEADDR
// TODO: Check SO_DONTLINGER and SO_EXCLUSIVEADDRUSE usage on linux
const std::map<uint32_t, uint32_t> supported_socket_options = {
    {0x0004, SO_REUSEADDR},   {X_SO_BROADCAST, SO_BROADCAST},
    {X_SO_LINGER, SO_LINGER}, {0x1001, SO_SNDBUF},
    {0x1002, SO_RCVBUF},      {0x1005, SO_SNDTIMEO},
    {0x1006, SO_RCVTIMEO},    {~X_SO_LINGER, ~SO_LINGER},
    {~0x0004, ~SO_REUSEADDR}};

// Socket options that can only be read
const std::map<uint32_t, uint32_t> supported_get_socket_options = {
    {0x0002, SO_ACCEPTCONN},
    {0x0004, SO_REUSEADDR},
    {X_SO_BROADCAST, SO_BROADCAST},
    {X_SO_LINGER, SO_LINGER},
    {0x1001, SO_SNDBUF},
    {0x1002, SO_RCVBUF},
    {0x1005, SO_SNDTIMEO},
    {0x1006, SO_RCVTIMEO},
    {0x1008, SO_TYPE},
    {~X_SO_LINGER, ~SO_LINGER},
    {~0x0004, ~SO_REUSEADDR}};

// Translate socket TCP options to native
const std::map<uint32_t, uint32_t> supported_tcp_options = {
    {0x0001, TCP_NODELAY}};

// Translate socket levels to native
const std::map<uint32_t, uint32_t> supported_levels = {
    {X_SOL_SOCKET, SOL_SOCKET}, {0x6, IPPROTO_TCP}};

// Translate ioctl commands to native
const std::map<uint32_t, uint32_t> supported_controls = {
    {X_FIONBIO, FIONBIO}, {X_FIONREAD, FIONREAD}};

// UDP/VDP ports reserved by the system. Titles can't bind them.
constexpr bool IsReservedDatagramPort(uint16_t port) {
  return port == 68 || port == 1256 || port == 1900 || port == 3074 ||
         (port >= 50000 && port <= 50100);
}

// Datagram sockets can't send to ADDR_ANY (stream sockets ignore the
// destination). The host may accept it.
static bool IsAnyAddressDatagram(XSocket::Type type, const XSOCKADDR_IN* to) {
  return type == XSocket::X_SOCK_DGRAM && to && !to->address_ip.s_addr;
}

constexpr std::chrono::microseconds kOverlappedPollInterval = 10ms;

XSocket::XSocket(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

XSocket::XSocket(KernelState* kernel_state, uint64_t native_handle)
    : XObject(kernel_state, kObjectType), native_handle_(native_handle) {}

XSocket::~XSocket() {
  if (!socket_closed_) {
    Close();
  }
}

X_STATUS XSocket::Initialize(AddressFamily af, Type type, Protocol proto) {
  af_ = af;
  type_ = type;
  proto_ = proto;
  vdp_ = false;

  if (!type) {
    if (proto == X_IPPROTO_UDP || proto == X_IPPROTO_VDP) {
      type_ = X_SOCK_DGRAM;
    } else if (proto == X_IPPROTO_TCP) {
      type_ = X_SOCK_STREAM;
    }
  }

  if (!proto) {
    if (type_ == X_SOCK_DGRAM) {
      proto_ = X_IPPROTO_UDP;
    } else if (type_ == X_SOCK_STREAM) {
      proto_ = X_IPPROTO_TCP;
    }
  } else if (proto == X_IPPROTO_VDP) {
    // VDP is a layer on top of UDP.
    proto_ = X_IPPROTO_UDP;
    vdp_ = true;
  }

  if (!type && !proto) {
    type_ = X_SOCK_STREAM;
    proto_ = X_IPPROTO_TCP;
  }

  native_handle_ = socket(af ? af : X_AF_INET, type_, proto_);
  if (native_handle_ == X_INVALID_SOCKET) {
    return X_STATUS_UNSUCCESSFUL;
  }

#if XE_PLATFORM_WIN32
  // 415607FF fails on WSAECONNRESET after sending to a port with no socket.
  if (type_ == X_SOCK_DGRAM) {
    BOOL report_resets = FALSE;
    DWORD bytes_returned = 0;
    WSAIoctl(static_cast<SOCKET>(native_handle_), SIO_UDP_CONNRESET,
             &report_resets, sizeof(report_resets), nullptr, 0, &bytes_returned,
             nullptr, nullptr);
  }
#endif

  return X_STATUS_SUCCESS;
}

int XSocket::Close() {
  WSACancelOverlappedIO();

  int ret = 0;

#if XE_PLATFORM_WIN32
  ret = closesocket(static_cast<SOCKET>(native_handle_));
#else
  ret = close(static_cast<int>(native_handle_));
#endif

  if (ret == X_ERROR_SUCCESS) {
    socket_closed_ = true;
  } else {
    XELOGE("Socket close failed: {}", XWSAGetLastError());
  }

  return ret;
}

X_STATUS XSocket::GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                            uint32_t* optlen) {
  if (!supported_levels.contains(level)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_STATUS_UNSUCCESSFUL;
  }

  const auto& supported_options = level == X_SOL_SOCKET
                                      ? supported_get_socket_options
                                      : supported_tcp_options;

  if (!supported_options.contains(optname)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_STATUS_UNSUCCESSFUL;
  }

  if (level == X_SOL_SOCKET && optname == X_SO_LINGER &&
      type_ == X_SOCK_DGRAM) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_STATUS_UNSUCCESSFUL;
  }

  // LINGER is two 16-bit fields, and the Linux host uses ints, so convert
  // each field instead of byte-swapping the whole option.
  if (level == X_SOL_SOCKET && optname == X_SO_LINGER &&
      *optlen >= sizeof(X_LINGER)) {
    linger native_linger = {};
    socklen_t native_linger_len = sizeof(native_linger);

    if (getsockopt(native_handle_, SOL_SOCKET, SO_LINGER,
                   reinterpret_cast<char*>(&native_linger),
                   &native_linger_len) < 0) {
      return X_STATUS_UNSUCCESSFUL;
    }

    X_LINGER* x_linger = static_cast<X_LINGER*>(optval_ptr);
    x_linger->l_onoff = static_cast<uint16_t>(native_linger.l_onoff);
    x_linger->l_linger = static_cast<uint16_t>(native_linger.l_linger);
    *optlen = sizeof(X_LINGER);

    return X_STATUS_SUCCESS;
  }

  // A buffer shorter than the option gets the low byte, the rest of it is
  // zeroed and optlen is set to 1.
  if (*optlen && *optlen < sizeof(uint32_t)) {
    uint32_t value = 0;
    socklen_t value_len = sizeof(value);

    if (GetNativeOption(supported_levels.at(level),
                        supported_options.at(optname), &value,
                        &value_len) < 0) {
      return X_STATUS_UNSUCCESSFUL;
    }

    std::memset(optval_ptr, 0, *optlen);
    *static_cast<uint8_t*>(optval_ptr) = static_cast<uint8_t>(value);
    *optlen = 1;

    return X_STATUS_SUCCESS;
  }

  int ret =
      GetNativeOption(supported_levels.at(level), supported_options.at(optname),
                      optval_ptr, reinterpret_cast<socklen_t*>(optlen));

  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Because values provided in optval_ptr are in LE we must to somehow save
  // them in BE.
  switch (*optlen) {
    case 1:
      xe::copy_and_swap<uint8_t>((uint8_t*)optval_ptr, (uint8_t*)optval_ptr, 1);
      break;
    case 4:
      xe::copy_and_swap<uint32_t>((uint32_t*)optval_ptr, (uint32_t*)optval_ptr,
                                  1);
      break;
    case 8:
      xe::copy_and_swap<uint64_t>((uint64_t*)optval_ptr, (uint64_t*)optval_ptr,
                                  1);
      break;
    default:
      XELOGE("XSocket::GetOption - Unhandled optlen: {}", *optlen);
      break;
  }

  return X_STATUS_SUCCESS;
}

int XSocket::SetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                       uint32_t optlen) {
  if (level == X_SOL_SOCKET &&
      (optname == SO_MARKINSECURE || optname == SO_PRIVATE ||
       optname == SO_GRANTINSECURE)) {
    // Disable socket encryption
    secure_ = false;
    return X_ERROR_SUCCESS;
  }

  if (!supported_levels.contains(level)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  const int native_level = supported_levels.at(level);

  const auto& supported_options =
      level == X_SOL_SOCKET ? supported_socket_options : supported_tcp_options;

  if (!supported_options.contains(optname)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_SOCKET_ERROR;
  }

  if (level == X_SOL_SOCKET && optname == X_SO_LINGER &&
      type_ == X_SOCK_DGRAM) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_SOCKET_ERROR;
  }

  const int native_optname = supported_options.at(optname);

  int ret = 0;

  // LINGER is two 16-bit fields, and the Linux host uses ints, so convert
  // each field instead of byte-swapping the whole option.
  if (level == X_SOL_SOCKET && optname == X_SO_LINGER &&
      optlen >= sizeof(X_LINGER)) {
    const X_LINGER* x_linger = static_cast<const X_LINGER*>(optval_ptr);

    linger native_linger = {};
    native_linger.l_onoff = x_linger->l_onoff;
    native_linger.l_linger = x_linger->l_linger;

    ret = setsockopt(native_handle_, SOL_SOCKET, SO_LINGER,
                     reinterpret_cast<const char*>(&native_linger),
                     sizeof(native_linger));
  } else if (optlen && optlen < sizeof(uint32_t)) {
    // A value shorter than the option is read as its first byte.
    const uint32_t value = *static_cast<uint8_t*>(optval_ptr);

    ret = SetNativeOption(native_level, native_optname, &value, sizeof(value));
  } else {
    void* proper_ptr =
        GetOptValueWithProperEndianness(optval_ptr, optname, optlen);

    ret = SetNativeOption(native_level, native_optname, proper_ptr, optlen);

    // Cheezy way to check if we created some additional allocation.
    if (optval_ptr != proper_ptr) {
      free(proper_ptr);
    }
  }

  if (ret < 0) {
    XELOGE("XSocket::SetOption: failed with error {:08X}", XWSAGetLastError());
    return X_SOCKET_ERROR;
  }

  if (level == X_SOL_SOCKET && optname == X_SO_BROADCAST) {
    broadcast_socket_ = true;
  }

  return X_ERROR_SUCCESS;
}

X_STATUS XSocket::IOControl(uint32_t cmd, uint32_t* arg_ptr) {
  if (!supported_controls.contains(cmd)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_STATUS_UNSUCCESSFUL;
  }

#ifdef XE_PLATFORM_WIN32
  const u_long initial_param = xe::load_and_swap<uint32_t>(arg_ptr);
  u_long param = initial_param;

  // The host refuses to make a socket blocking while WSAEventSelect is
  // active, so cancel its association first. The guest event stays recorded
  // for WSAResetEvent.
  if (cmd == X_FIONBIO && !param && event_select_flags_) {
    ::WSAEventSelect(native_handle_, nullptr, 0);
  }

  int ret = ioctlsocket(native_handle_, cmd, &param);

  // The host counts every queued datagram. Report only the size of the next
  // one, which is what a single receive returns.
  if (!ret && cmd == X_FIONREAD && type_ == X_SOCK_DGRAM && param) {
    std::vector<char> datagram(param);

    const int peeked =
        recvfrom(native_handle_, datagram.data(), static_cast<int>(param),
                 MSG_PEEK, nullptr, nullptr);

    if (peeked >= 0) {
      param = peeked;
    }
  }

  // Parameter was written to therefore byte swap output
  if (initial_param != param) {
    xe::store_and_swap(arg_ptr, static_cast<uint32_t>(param));
  }

  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
#else
  const int native_cmd = supported_controls.at(cmd);

  int param = static_cast<int>(xe::load_and_swap<uint32_t>(arg_ptr));

  int ret = ioctl(native_handle_, native_cmd, &param);

  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  xe::store_and_swap<uint32_t>(arg_ptr, static_cast<uint32_t>(param));

  return X_STATUS_SUCCESS;
#endif
}

X_STATUS XSocket::Connect(const XSOCKADDR_IN* name, int name_len) {
  if (listening_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_STATUS_UNSUCCESSFUL;
  }

  if (!name->address_ip.s_addr) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRNOTAVAIL);
    return X_STATUS_UNSUCCESSFUL;
  }

  XSOCKADDR_IN sa_in = *name;

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    sa_in.address_port = upnp->GetMappedConnectPort(name->address_port);
  }

  sockaddr addr = sa_in.to_host();

  // The guest's length is ignored, the whole address is always read.
  int ret = connect(native_handle_, &addr, sizeof(sockaddr_in));

  const uint32_t connect_error = ret < 0 ? XWSAGetLastError() : 0;

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
    bound_ = true;
  }

  if (ret < 0) {
    if (connect_error != X_WSA_ERROR::X_WSAEWOULDBLOCK) {
      connect_error_ = connect_error;
    }

    XWSASetLastError(connect_error);
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(const XSOCKADDR_IN* name, int name_len) {
  if (type_ == X_SOCK_DGRAM && IsReservedDatagramPort(name->address_port)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRINUSE);
    return X_STATUS_UNSUCCESSFUL;
  }

  XSOCKADDR_IN sa_in = *name;

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    sa_in.address_port = upnp->GetMappedBindPort(name->address_port);
  }

  sockaddr addr = sa_in.to_host();

  // Force socket to bind to the IP of the selected interface
  if (cvars::bind_interface) {
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr);

    const auto network_adapter =
        kernel_state()->emulator()->GetNetworkAdapterManager();

    const in_addr interface_addr =
        network_adapter->GetSelectedAdapterLocalIP().sin_addr;

    // Title wants to bind to and interface but is it our bound interface?
    if (name->address_ip.s_addr) {
      assert_true(name->address_ip.s_addr == interface_addr.s_addr);
    }

    addr_in->sin_addr = interface_addr;
  } else {
    // Check if title tried to bind an interface
    assert_zero(name->address_ip.s_addr);
  }

  int ret = bind(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_port_ = sa_in.address_port;

  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
  }

  bound_ = true;

  return X_STATUS_SUCCESS;
}

uint16_t XSocket::GetImplicitlyBoundPort() const {
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

void XSocket::ReportReceiveTimeout(int ret) const {
#if !XE_PLATFORM_WIN32
  // Linux reports a timed out blocking receive as EAGAIN, the same error as a
  // non-blocking receive with no data.
  if (ret < 0 && errno == EAGAIN && receive_timeout_ms_ &&
      !(fcntl(native_handle_, F_GETFL) & O_NONBLOCK)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAETIMEDOUT);
  }
#endif
}

int XSocket::GetNativeOption(int level, int optname, void* value,
                             socklen_t* len) {
#if !XE_PLATFORM_WIN32
  // Linux can round a timeout when it is read back, so report the
  // milliseconds that were set.
  if (level == SOL_SOCKET &&
      (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
      *len >= sizeof(uint32_t)) {
    const uint32_t timeout_ms =
        optname == SO_RCVTIMEO ? receive_timeout_ms_ : send_timeout_ms_;
    std::memcpy(value, &timeout_ms, sizeof(timeout_ms));
    *len = sizeof(timeout_ms);
    return 0;
  }

  if (level == SOL_SOCKET && (optname == SO_RCVBUF || optname == SO_SNDBUF) &&
      *len >= sizeof(uint32_t)) {
    int size = 0;
    socklen_t size_len = sizeof(size);

    if (getsockopt(native_handle_, level, optname, &size, &size_len) < 0) {
      return -1;
    }

    const uint32_t set_size = static_cast<uint32_t>(size) / 2;
    std::memcpy(value, &set_size, sizeof(set_size));
    *len = sizeof(set_size);
    return 0;
  }
#endif

  return getsockopt(native_handle_, level, optname, static_cast<char*>(value),
                    len);
}

int XSocket::SetNativeOption(int level, int optname, const void* value,
                             socklen_t len) {
#if !XE_PLATFORM_WIN32
  // Linux takes a timeval instead of milliseconds.
  if (level == SOL_SOCKET &&
      (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
      len == sizeof(uint32_t)) {
    uint32_t timeout_ms = 0;
    std::memcpy(&timeout_ms, value, sizeof(timeout_ms));

    timeval timeout = {};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int ret =
        setsockopt(native_handle_, level, optname, &timeout, sizeof(timeout));
    if (!ret) {
      (optname == SO_RCVTIMEO ? receive_timeout_ms_ : send_timeout_ms_) =
          timeout_ms;
    }
    return ret;
  }
#endif

  return setsockopt(native_handle_, level, optname,
                    static_cast<const char*>(value), len);
}

X_STATUS XSocket::Listen(int backlog) {
  if (connect_error_) {
    XWSASetLastError(connect_error_);
    return X_STATUS_UNSUCCESSFUL;
  }

  if (!bound_ || type_ == X_SOCK_DGRAM) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_STATUS_UNSUCCESSFUL;
  }

  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  listening_ = true;

  return X_STATUS_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(XSOCKADDR_IN* name, int* name_len) {
  sockaddr sa = {};
  socklen_t addrlen = 0;
  const bool is_name_and_name_len_available = name && name_len;

  if (is_name_and_name_len_available) {
    addrlen = xe::byte_swap(*name_len);
  }

  const uint64_t socket_handle = accept(native_handle_, name ? &sa : nullptr,
                                        name_len ? &addrlen : nullptr);
  if (socket_handle == X_INVALID_SOCKET) {
    return nullptr;
  }

  if (is_name_and_name_len_available) {
    name->to_guest(&sa);
    *name_len = xe::byte_swap(addrlen);
  }

  // Create a kernel object to represent the new socket, and copy parameters
  // over.
  auto socket = object_ref<XSocket>(new XSocket(kernel_state_, socket_handle));
  socket->af_ = af_;
  socket->type_ = type_;
  socket->proto_ = proto_;
  socket->vdp_ = vdp_;

  // Implicit Bind
  socket->bound_port_ = bound_port_ = GetImplicitlyBoundPort();
  socket->bound_ = true;

  return socket;
}

int XSocket::Shutdown(int how) {
  if (how < 0 || how > 2) {
    how = 2;
  }

  const int ret = shutdown(native_handle_, how);

  if (!ret && how != 1) {
    shutdown_receive_ = true;
  }

  if (!ret && how != 0) {
    shutdown_send_ = true;
  }

  return ret;
}

// Receive flags must be 0. They're accepted but ignored, e.g. MSG_PEEK still
// consumes the data, so don't pass them on.
int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  if (type_ == X_SOCK_STREAM && shutdown_receive_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAESHUTDOWN);
    return X_SOCKET_ERROR;
  }

#if XE_PLATFORM_WIN32
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, 0);
#else
  const int ret =
      recv(native_handle_, buf, buf_len, type_ == X_SOCK_DGRAM ? MSG_TRUNC : 0);

  ReportReceiveTimeout(ret);

  if (ret > static_cast<int>(buf_len)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    return X_SOCKET_ERROR;
  }

  return ret;
#endif
}

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                      XSOCKADDR_IN* from, socklen_t* from_len) {
  if (type_ == X_SOCK_DGRAM && !bound_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  if (type_ == X_SOCK_STREAM && shutdown_receive_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAESHUTDOWN);
    return X_SOCKET_ERROR;
  }

  sockaddr sa = {};

  if (from) {
    sa = from->to_host();
  }

#if XE_PLATFORM_WIN32
  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len, 0,
                     from ? &sa : nullptr, from_len);
#else
  int ret = recvfrom(native_handle_, buf, buf_len,
                     type_ == X_SOCK_DGRAM ? MSG_TRUNC : 0,
                     from ? &sa : nullptr, from_len);

  ReportReceiveTimeout(ret);
#endif

  // TCP receives report the peer's address, e.g. 555307EE reads the port.
  if (proto_ == X_IPPROTO_TCP) {
    socklen_t peer_addr_len = sizeof(sockaddr);
    getpeername(native_handle_, &sa, &peer_addr_len);
  }

  if (from) {
    from->to_guest(&sa);
  }

#if !XE_PLATFORM_WIN32
  if (ret > static_cast<int>(buf_len)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    return X_SOCKET_ERROR;
  }
#endif

  return ret;
}

// If wait is true then block until data is available for writing
int XSocket::WSASelectWrite(bool wait, X_WSA_ERROR* error) {
  int activity = 0;

  if (wait) {
    XELOGNETD("{} Blocking...", __func__);
  }

  do {
    fd_set writefds = {};
    FD_ZERO(&writefds);

#if XE_PLATFORM_WIN32
    SOCKET s = static_cast<SOCKET>(native_handle_);
    FD_SET(s, &writefds);
    int nfds = 0;
#else
    int s = static_cast<int>(native_handle_);
    FD_SET(s, &writefds);
    int nfds = s + 1;
#endif

    timeval tv = {};
    tv.tv_usec = wait ? static_cast<long>(kOverlappedPollInterval.count()) : 0;

    activity = select(nfds, nullptr, &writefds, nullptr, &tv);

    if (cancel_overlapped_ || cancel_send_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSAECANCELLED;
      }

      activity = X_SOCKET_ERROR;
    }
  } while (activity == 0 && wait);

  return activity;
}

int XSocket::PollWSASendTo(bool wait, WSASendToData send_async_data,
                           uint32_t* num_bytes_sent) {
  // critical section - lock until we return
  std::unique_lock<std::mutex> lock;

  X_WSA_ERROR poll_write_error = X_WSA_ERROR::X_WSA_NO_ERROR;

  // If overlapped block until data is available for writing
  const int data_available = send_async_data.overlapped
                                 ? WSASelectWrite(wait, &poll_write_error)
                                 : true;

  if (wait) {
    // Sync is already locked, therefore only lock for async
    lock = std::unique_lock<std::mutex>(send_socket_mutex_);
  }

  if (data_available == X_SOCKET_ERROR) {
    uint32_t error = 0;

    if (poll_write_error != X_WSA_ERROR::X_WSA_NO_ERROR) {
      error = poll_write_error;
    } else {
      error = XWSAGetLastError();
    }

    XELOGE("WSASelectWrite failed with error {}", error);

    if (send_async_data.overlapped) {
      send_async_data.overlapped->internal = error;
    }

    return X_SOCKET_ERROR;
  }

  if (data_available == X_ERROR_SUCCESS) {
    // There's no available space for writing therefore return and start
    // pending operation.
    if (send_async_data.overlapped) {
      send_async_data.overlapped->internal = X_STATUS_PENDING;
    }

    return X_SOCKET_ERROR;
  }

#if XE_PLATFORM_WIN32
  std::vector<WSABUF> buffers(send_async_data.num_buffers);

  for (uint32_t i = 0; i < send_async_data.num_buffers; i++) {
    buffers[i].len = send_async_data.buffers[i].len;
    buffers[i].buf = kernel_state()->memory()->TranslateVirtual<char*>(
        send_async_data.buffers[i].buf_ptr);
  }
#else
  std::vector<iovec> buffers(send_async_data.num_buffers);

  for (uint32_t i = 0; i < send_async_data.num_buffers; i++) {
    buffers[i].iov_len = send_async_data.buffers[i].len;
    buffers[i].iov_base = kernel_state()->memory()->TranslateVirtual<char*>(
        send_async_data.buffers[i].buf_ptr);
  }
#endif

  sockaddr saddr = {};
  sockaddr* saddr_ptr = nullptr;

  int saddr_len = 0;

  if (send_async_data.to) {
    XSOCKADDR_IN target = *send_async_data.to;

    const auto upnp = kernel_state()->emulator()->GetUPnP();

    if (upnp) {
      target.address_port = upnp->GetMappedBindPort(target.address_port);
    }

    saddr = target.to_host();
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&saddr);

    // Ensure the bound interface can route to the loopback interface/itself
    if (cvars::bind_interface) {
      if (addr_in->sin_addr.s_addr == xe::byte_swap(LOOPBACK)) {
        const auto network_adapter =
            kernel_state()->emulator()->GetNetworkAdapterManager();

        addr_in->sin_addr =
            network_adapter->GetSelectedAdapterLocalIP().sin_addr;
      }
    }

    saddr_ptr = &saddr;
    saddr_len = sizeof(sockaddr_in);
  }

  uint32_t bytes_sent = 0;

  int result = 0;

#if XE_PLATFORM_WIN32
  result =
      ::WSASendTo(native_handle_, buffers.data(), send_async_data.num_buffers,
                  reinterpret_cast<DWORD*>(&bytes_sent), 0, &saddr, saddr_len,
                  nullptr, nullptr);
#else
  msghdr message = {};

  message.msg_name = &saddr;
  message.msg_namelen = saddr_len;

  // Set scatter-gather buffers (Equivalent to WSABUF array)
  message.msg_iov = const_cast<iovec*>(buffers.data());
  message.msg_iovlen = buffers.size();

  ssize_t sent_bytes = sendmsg(native_handle_, &message, 0);

  if (sent_bytes >= 0) {
    bytes_sent = sent_bytes;
    result = X_ERROR_SUCCESS;
  } else {
    result = X_SOCKET_ERROR;
  }
#endif

  const uint32_t send_error = result == X_SOCKET_ERROR ? XWSAGetLastError() : 0;

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
    bound_ = true;
  }

  // getsockname resets the last error.
  if (result == X_SOCKET_ERROR) {
    XWSASetLastError(send_error);
  }

  const bool pending = XWSAGetLastError() == X_WSA_ERROR::X_WSA_IO_PENDING ||
                       XWSAGetLastError() == X_WSA_ERROR::X_WSAEWOULDBLOCK;

  if (result == X_SOCKET_ERROR && !pending) {
    XELOGI("WSASendTo failed with error {}", XWSAGetLastError());
  }

  if (send_async_data.overlapped && result == X_SOCKET_ERROR) {
    if (pending) {
      send_async_data.overlapped->internal = X_STATUS_PENDING;
    } else {
      send_async_data.overlapped->internal = XWSAGetLastError();
    }
  }

  if (result == X_ERROR_SUCCESS) {
    if (send_async_data.overlapped) {
      send_async_data.overlapped->internal_high = bytes_sent;
    }

    if (num_bytes_sent) {
      *num_bytes_sent = bytes_sent;
    }
  }

  if (send_async_data.overlapped) {
    send_async_data.overlapped->offset = 0;
  }

  if (send_async_data.overlapped && result == X_ERROR_SUCCESS) {
    // If no event handle is provided then title can check for completion via
    // internal on change.
    send_async_data.overlapped->internal = X_WSA_ERROR::X_WSA_NO_ERROR;

    xboxkrnl::xeNtSetEvent(send_async_data.overlapped->event_handle, nullptr);
  }

  return result;
}

int XSocket::WSASendTo(XWSABUF* buffers, uint32_t num_buffers,
                       xe::be<uint32_t>* num_bytes_sent_ptr, uint32_t flags,
                       XSOCKADDR_IN* to_ptr, uint32_t to_len,
                       XWSAOVERLAPPED* overlapped_ptr) {
  // Wait for WSASendTo to finish writing to overlapped_ptr
  std::lock_guard socket_lock(send_socket_mutex_);

  if (!buffers || !num_buffers || !num_bytes_sent_ptr || flags ||
      to_ptr && to_ptr->address_family != X_AF_INET) {
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return X_SOCKET_ERROR;
  }

  if (IsAnyAddressDatagram(type_, to_ptr) ||
      (type_ == X_SOCK_DGRAM && to_ptr && !to_ptr->address_port)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRNOTAVAIL);
    return X_SOCKET_ERROR;
  }

  WSASendToData send_async_data = {};

  // These may have been on the stack - copy them.
  send_async_data.buffers = std::make_shared<XWSABUF[]>(num_buffers);
  std::memcpy(send_async_data.buffers.get(), buffers,
              num_buffers * sizeof(XWSABUF));

  send_async_data.num_buffers = num_buffers;
  send_async_data.to = to_ptr;

  send_async_data.overlapped = overlapped_ptr;

  if (overlapped_ptr) {
    const uint32_t overlapped_address =
        kernel_state()->memory()->HostToGuestVirtual(
            std::to_address(overlapped_ptr));

    if (!send_polling_tasks_.contains(overlapped_ptr)) {
      XELOGNETD("{} new overlapped task {:08X}", __func__, overlapped_address);
    } else {
      XELOGNETD("{} overlapped task {:08X} is pending!", __func__,
                overlapped_address);
    }

    // Overlapped is being reused for a new request.
    std::lock_guard lock(cancelled_overlapped_io_mutex_);
    cancelled_overlapped_io_.erase(overlapped_address);
  }

  uint32_t num_bytes_sent = 0;

  const uint32_t previous_internal =
      overlapped_ptr ? overlapped_ptr->internal.get() : 0;

  // Check for immediate completion.
  const int result = PollWSASendTo(false, send_async_data, &num_bytes_sent);

  if (!overlapped_ptr) {
    if (result == X_ERROR_SUCCESS && num_bytes_sent_ptr) {
      *num_bytes_sent_ptr = num_bytes_sent;
    }

    return result;
  }

  if (result == X_ERROR_SUCCESS) {
    XELOGNETD("{} completed immediately", __func__);

    if (num_bytes_sent_ptr) {
      *num_bytes_sent_ptr = num_bytes_sent;
    }

    return result;
  }

  // Perform overlapped completion.

  const uint32_t wsa_error = send_async_data.overlapped->internal;

  if (overlapped_ptr && wsa_error == X_STATUS_PENDING) {
    xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);

    send_polling_tasks_[send_async_data.overlapped] =
        std::async(std::launch::async, &XSocket::PollWSASendTo, this, true,
                   send_async_data, nullptr);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    overlapped_ptr->internal = previous_internal;

    XWSASetLastError(wsa_error);

    // An error occurred that's not X_STATUS_PENDING
    XELOGI("{}:: failed with error code {}", __func__, wsa_error);

    if (wsa_error == X_WSA_ERROR::X_WSAECANCELLED) {
      XELOGD("{}:: Operation Cancelled!", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSAECANCELLED);
    }
  }

  return result;
}

// If wait is true then block until data is available for reading
int XSocket::WSASelectRead(bool wait, X_WSA_ERROR* error) {
  int activity = 0;

  if (wait) {
    XELOGNETD("{} Blocking...", __func__);
  }

  do {
    fd_set readfds = {};
    FD_ZERO(&readfds);

#if XE_PLATFORM_WIN32
    SOCKET s = static_cast<SOCKET>(native_handle_);
    FD_SET(s, &readfds);
    int nfds = 0;
#else
    int s = static_cast<int>(native_handle_);
    FD_SET(s, &readfds);
    int nfds = s + 1;
#endif

    timeval tv = {};
    tv.tv_usec = wait ? static_cast<long>(kOverlappedPollInterval.count()) : 0;

    activity = select(nfds, &readfds, nullptr, nullptr, &tv);

    if (cancel_overlapped_ || cancel_receive_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSAECANCELLED;
      }

      activity = X_SOCKET_ERROR;
    }
  } while (activity == 0 && wait);

  return activity;
}

int XSocket::PollWSARecvFrom(bool wait, WSARecvFromData receive_async_data) {
  // critical section - lock until we return
  std::unique_lock<std::mutex> lock;

  X_WSA_ERROR poll_read_error = X_WSA_ERROR::X_WSA_NO_ERROR;

  // If overlapped block until data is available for reading
  const int data_available = receive_async_data.overlapped
                                 ? WSASelectRead(wait, &poll_read_error)
                                 : true;

  if (wait) {
    // Sync is already locked, therefore only lock for async
    lock = std::unique_lock<std::mutex>(receive_socket_mutex_);
  }

  if (data_available == X_SOCKET_ERROR) {
    uint32_t error = 0;

    if (poll_read_error != X_WSA_ERROR::X_WSA_NO_ERROR) {
      error = poll_read_error;
    } else {
      error = XWSAGetLastError();
    }

    XELOGE("WSASelectRead failed with error {}", error);

    if (receive_async_data.overlapped) {
      receive_async_data.overlapped->internal = X_HRESULT_FROM_WIN32(error);
    }

    return X_SOCKET_ERROR;
  }

  if (data_available == X_ERROR_SUCCESS) {
    // There's no available data for reading therefore return and start
    // pending operation.
    if (receive_async_data.overlapped) {
      receive_async_data.overlapped->internal = X_STATUS_PENDING;
    }

    return X_SOCKET_ERROR;
  }

#if XE_PLATFORM_WIN32
  WSABUF recv_buffer = {};

  recv_buffer.buf = kernel_state()->memory()->TranslateVirtual<char*>(
      receive_async_data.buffers->buf_ptr);
  recv_buffer.len = receive_async_data.buffers->len;
#else
  iovec recv_buffer = {};

  recv_buffer.iov_base = kernel_state()->memory()->TranslateVirtual<char*>(
      receive_async_data.buffers->buf_ptr);
  recv_buffer.iov_len = receive_async_data.buffers->len;
#endif

  sockaddr saddr = {};

  if (receive_async_data.from) {
    saddr = receive_async_data.from->to_host();
  }

  uint32_t bytes_received = 0;

  // Input flags must be 0 and are ignored, like recv (see XSocket::Recv).
  // flags is only used for output.
  uint32_t flags = 0;

  // lpFromlen is only used when lpFrom is provided. The host rejects a length
  // without an address with WSAEFAULT.
  const bool has_from = receive_async_data.from && receive_async_data.from_len;
  int from_len = has_from ? receive_async_data.from_len->get() : 0;

  // Only the first buffer is filled, the rest of a stream is left for the next
  // receive.
  int result = 0;

#if XE_PLATFORM_WIN32
  result = ::WSARecvFrom(native_handle_, &recv_buffer, 1,
                         reinterpret_cast<DWORD*>(&bytes_received),
                         reinterpret_cast<DWORD*>(&flags),
                         has_from ? &saddr : nullptr,
                         has_from ? &from_len : nullptr, nullptr, nullptr);
#else
  sockaddr_in sender_addr = {};
  msghdr message = {};

  message.msg_name = has_from ? &saddr : nullptr;
  message.msg_namelen = has_from ? from_len : 0;

  // Setup scatter-gather destination buffers (Equivalent to WSABUF array)
  message.msg_iov = &recv_buffer;
  message.msg_iovlen = 1;

  ssize_t received_bytes = recvmsg(native_handle_, &message, flags);

  if (received_bytes >= 0 && (message.msg_flags & MSG_TRUNC)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    result = X_SOCKET_ERROR;
  } else if (received_bytes >= 0) {
    bytes_received = received_bytes;
    result = X_ERROR_SUCCESS;
  } else {
    result = X_SOCKET_ERROR;
  }
#endif

  const bool pending = XWSAGetLastError() == X_WSA_ERROR::X_WSA_IO_PENDING ||
                       XWSAGetLastError() == X_WSA_ERROR::X_WSAEWOULDBLOCK;

  if (result == X_SOCKET_ERROR && !pending) {
    XELOGI("WSARecvFrom failed with error {}", XWSAGetLastError());
  }

  // A truncated datagram also sets MSG_PARTIAL. The host only reports
  // WSAEMSGSIZE.
  const bool truncated = result == X_SOCKET_ERROR && type_ == X_SOCK_DGRAM &&
                         XWSAGetLastError() == X_WSA_ERROR::X_WSAEMSGSIZE;

  if (truncated) {
    flags |= X_MSG_PARTIAL;
  }

  if (receive_async_data.overlapped && result == X_SOCKET_ERROR) {
    if (pending) {
      receive_async_data.overlapped->internal = X_STATUS_PENDING;
    } else {
      receive_async_data.overlapped->internal =
          X_HRESULT_FROM_WIN32(XWSAGetLastError());
    }

    if (truncated) {
      receive_async_data.overlapped->internal_high =
          receive_async_data.buffers->len;
    }
  }

  if (result == X_ERROR_SUCCESS) {
    if (receive_async_data.from) {
      // Copy behavior from recvfrom.
      if (proto_ == X_IPPROTO_TCP) {
        socklen_t peer_addar_len = sizeof(sockaddr);
        getpeername(native_handle_, &saddr, &peer_addar_len);
      }

      receive_async_data.from->to_guest(&saddr);
    }

    if (receive_async_data.from_len && receive_async_data.from) {
      *receive_async_data.from_len = from_len;
    }

    if (!wait && receive_async_data.num_bytes_recv) {
      *receive_async_data.num_bytes_recv = bytes_received;
    }

    if (receive_async_data.overlapped) {
      receive_async_data.overlapped->internal_high = bytes_received;
    }
  }

  if (!wait && receive_async_data.flags) {
    *receive_async_data.flags = flags;
  }

  if (receive_async_data.overlapped) {
    receive_async_data.overlapped->offset = flags;
  }

  if (receive_async_data.overlapped && result == X_ERROR_SUCCESS) {
    // If no event handle is provided then title can check for completion via
    // internal on change.
    receive_async_data.overlapped->internal = X_WSA_ERROR::X_WSA_NO_ERROR;
  }

  // A pending receive also completes when it ends in an error, e.g. the peer
  // resets the connection.
  if (receive_async_data.overlapped &&
      (result == X_ERROR_SUCCESS || (wait && !pending))) {
    xboxkrnl::xeNtSetEvent(receive_async_data.overlapped->event_handle,
                           nullptr);
  }

  return result;
}

int XSocket::WSARecvFrom(XWSABUF* buffers, uint32_t num_buffers,
                         xe::be<uint32_t>* num_bytes_recv_ptr,
                         xe::be<uint32_t>* flags_ptr, XSOCKADDR_IN* from_ptr,
                         xe::be<uint32_t>* fromlen_ptr,
                         XWSAOVERLAPPED* overlapped_ptr) {
  // Wait for WSARecvFrom to finish writing to overlapped_ptr
  std::lock_guard socket_lock(receive_socket_mutex_);

  if (!buffers || !num_buffers || !flags_ptr || (from_ptr && !fromlen_ptr)) {
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return X_SOCKET_ERROR;
  }

  if (!bound_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  // The host would leave a receive on an unconnected stream pending forever.
  if (type_ == X_SOCK_STREAM) {
    sockaddr_storage peer = {};
    socklen_t peer_len = sizeof(peer);

    if (listening_ ||
        getpeername(native_handle_, reinterpret_cast<sockaddr*>(&peer),
                    &peer_len) != 0) {
      XWSASetLastError(X_WSA_ERROR::X_WSAENOTCONN);
      return X_SOCKET_ERROR;
    }
  }

  // Only one overlapped receive can be pending per socket.
  if (overlapped_ptr) {
    for (auto& [pending_overlapped_ptr, task] : receive_polling_tasks_) {
      if (task.valid() && task.wait_for(0s) != std::future_status::ready) {
        XWSASetLastError(X_WSA_ERROR::X_WSAEWOULDBLOCK);
        return X_SOCKET_ERROR;
      }
    }
  }

  WSARecvFromData receive_async_data = {};

  // These may have been on the stack - copy them.
  receive_async_data.buffers = std::make_shared<XWSABUF>();
  std::memcpy(receive_async_data.buffers.get(), buffers, sizeof(XWSABUF));

  receive_async_data.num_buffers = num_buffers;
  receive_async_data.flags = flags_ptr;
  receive_async_data.num_bytes_recv = num_bytes_recv_ptr;
  receive_async_data.from = from_ptr;
  receive_async_data.from_len = fromlen_ptr;
  receive_async_data.overlapped = overlapped_ptr;

  if (overlapped_ptr) {
    const uint32_t overlapped_address =
        kernel_state()->memory()->HostToGuestVirtual(
            std::to_address(overlapped_ptr));

    if (!receive_polling_tasks_.contains(overlapped_ptr)) {
      XELOGNETD("{} new overlapped task {:08X}", __func__, overlapped_address);
    } else {
      XELOGNETD("{} overlapped task {:08X} is pending!", __func__,
                overlapped_address);
    }

    // Overlapped is being reused for a new request.
    std::lock_guard lock(cancelled_overlapped_io_mutex_);
    cancelled_overlapped_io_.erase(overlapped_address);
  }

  // Check for immediate completion.
  int result = PollWSARecvFrom(false, receive_async_data);

  if (!overlapped_ptr) {
    return result;
  }

  if (result == X_ERROR_SUCCESS) {
    XELOGNETD("{} completed immediately", __func__);

    return result;
  }

  // Perform overlapped completion.

  const uint32_t wsa_error =
      X_WIN32_FROM_HRESULT(receive_async_data.overlapped->internal.get());

  if (wsa_error == X_STATUS_PENDING) {
    xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);

    receive_polling_tasks_[receive_async_data.overlapped] =
        std::async(std::launch::async, &XSocket::PollWSARecvFrom, this, true,
                   receive_async_data);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    XWSASetLastError(wsa_error);

    // An error occurred that's not X_STATUS_PENDING
    XELOGI("{}:: failed with error code {}", __func__, wsa_error);

    if (wsa_error == X_WSA_ERROR::X_WSAECANCELLED) {
      XELOGD("{}:: Operation Cancelled!", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSAECANCELLED);
    }
  }

  return result;
}

bool XSocket::WSAGetOverlappedResult(XWSAOVERLAPPED* overlapped_ptr,
                                     xe::be<uint32_t>* bytes_transferred,
                                     bool wait, xe::be<uint32_t>* flags_ptr) {
  if (!overlapped_ptr || !bytes_transferred || !flags_ptr) {
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return false;
  }

  const uint32_t overlapped_address =
      kernel_state()->memory()->HostToGuestVirtual(
          std::to_address(overlapped_ptr));

  std::unique_lock<std::mutex> task_mutex;

  bool sending_io = false;
  bool receiving_io = false;

  {
    std::lock_guard lock(send_socket_mutex_);
    sending_io = send_polling_tasks_.contains(overlapped_ptr);
  }

  {
    std::lock_guard lock(receive_socket_mutex_);
    receiving_io = receive_polling_tasks_.contains(overlapped_ptr);
  }

  if (sending_io) {
    task_mutex = std::unique_lock(send_socket_mutex_);
  } else if (receiving_io) {
    task_mutex = std::unique_lock(receive_socket_mutex_);
  }

  {
    std::lock_guard lock(cancelled_overlapped_io_mutex_);
    if (cancelled_overlapped_io_.contains(overlapped_address)) {
      XELOGD("{}:: Operation Cancelled!", __func__);
      const uint32_t internal_result =
          X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get());
      XWSASetLastError(internal_result);
      return false;
    }
  }

  const bool pending_io = sending_io || receiving_io;

  // 4D530808 does this.
  if (!pending_io) {
    XELOGNETD("Overlap not in operation!");

    const uint32_t internal = overlapped_ptr->internal;

    if (internal == X_STATUS_PENDING) {
      XWSASetLastError(X_WSA_ERROR::X_WSA_IO_INCOMPLETE);
      return false;
    }

    if (X_HRESULT_FACILITY(internal) == X_FACILITY_WIN32 && XFAILED(internal)) {
      XWSASetLastError(X_WIN32_FROM_HRESULT(internal));
      return false;
    }

    *bytes_transferred = overlapped_ptr->internal_high;
    *flags_ptr = overlapped_ptr->offset;

    return true;
  }

  uint32_t internal_result =
      X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get());

  if (wait) {
    if (sending_io) {
      XELOGNETD("{}:: WSASendTo blocking until completion!", __func__);
    } else if (receiving_io) {
      XELOGNETD("{}:: WSARecvFrom Blocking until completion!", __func__);
    }

    auto event = kernel_state()->object_table()->LookupObject<XEvent>(
        overlapped_ptr->event_handle);

    bool pending = internal_result == X_STATUS_PENDING;

    // With no event there's nothing to wait on, so the request is cancelled.
    if (pending && !event) {
      auto& tasks = sending_io ? send_polling_tasks_ : receive_polling_tasks_;
      std::atomic<bool>& cancel = sending_io ? cancel_send_ : cancel_receive_;

      std::future<int32_t> task;
      auto it = tasks.find(overlapped_ptr);
      if (it != tasks.end()) {
        task = std::move(it->second);
        tasks.erase(it);
      }

      // The task takes the mutex to finish.
      task_mutex.unlock();

      cancel = true;
      if (task.valid()) {
        task.wait();
      }
      cancel = false;

      overlapped_ptr->internal =
          X_HRESULT_FROM_WIN32(X_WSA_ERROR::X_WSAECANCELLED);

      {
        std::lock_guard lock(cancelled_overlapped_io_mutex_);
        cancelled_overlapped_io_.insert(overlapped_address);
      }

      XWSASetLastError(X_WSA_ERROR::X_WSAECANCELLED);
      return false;
    }

    if (pending) {
      // Unlock so overlapped task can update internal result after completion.
      task_mutex.unlock();

      // https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-overlapped-input-and-output
      // 434D07D8 uses an auto reset event which causes signaled event
      // notifications to be missed causing an infinite wait deadlock.
      // As a result we check if task is completed before deciding to wait for
      // event.
      event->Wait(3, 1, 0, nullptr);
    }

    if (pending) {
      task_mutex.lock();

      // Update result after task completion.
      internal_result = X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get());
    }
  }

  bool result = false;

  switch (internal_result) {
    case X_WSA_ERROR::X_WSA_NO_ERROR: {
      if (xe::logging::ShouldLog(xe::LogLevel::Debug, xe::LogSrc::Network)) {
        const uint32_t overlapped_address =
            kernel_state()->memory()->HostToGuestVirtual(
                std::to_address(overlapped_ptr));

        std::string operation = sending_io ? "WSASendTo" : "WSARecvFrom";
        std::string action = sending_io ? "sent" : "received";

        XELOGNETD(
            "{}:: {} overlapped task {:08X} {} {} bytes with "
            "status {}!",
            __func__, operation, overlapped_address, action,
            overlapped_ptr->internal_high.get(),
            overlapped_ptr->internal.get());
      }

      *bytes_transferred = overlapped_ptr->internal_high;
      *flags_ptr = overlapped_ptr->offset;

      if (sending_io) {
        send_polling_tasks_.erase(overlapped_ptr);
      } else if (receiving_io) {
        receive_polling_tasks_.erase(overlapped_ptr);
      }

      result = true;
    } break;
    case X_WSA_ERROR::X_WSAECANCELLED: {
      XELOGD("{}:: Operation Cancelled!", __func__);
      XWSASetLastError(internal_result);
      result = false;
    } break;
    case X_STATUS_PENDING:
    case X_WSA_ERROR::X_WSAEWOULDBLOCK: {
      XWSASetLastError(X_WSA_ERROR::X_WSA_IO_INCOMPLETE);
      result = false;
    } break;
    default: {
      XWSASetLastError(internal_result);
      XELOGI("{}:: failed with error code {}", __func__,
             overlapped_ptr->internal.get());
      result = false;
    } break;
  }

  return result;
}

int XSocket::WSACancelOverlappedIO() {
  cancel_overlapped_ = true;

  auto set_cancelled_state = [this](XWSAOVERLAPPED* overlapped_ptr) {
    xboxkrnl::xeNtSetEvent(overlapped_ptr->event_handle, nullptr);

    overlapped_ptr->internal =
        X_HRESULT_FROM_WIN32(X_WSA_ERROR::X_WSAECANCELLED);

    const uint32_t overlapped_address =
        kernel_state()->memory()->HostToGuestVirtual(
            std::to_address(overlapped_ptr));

    std::lock_guard lock(cancelled_overlapped_io_mutex_);
    cancelled_overlapped_io_.insert(overlapped_address);
  };

  // Future pointers are safe because WSAGetOverlappedResult will not erase
  // entries instead it returns early if overlapped address is in
  // cancelled_overlapped_io_.

  std::vector<std::future<int32_t>*> receive_future_ptrs;

  {
    std::lock_guard receive_lock(receive_socket_mutex_);

    for (auto& [overlapped_ptr, task] : receive_polling_tasks_) {
      set_cancelled_state(overlapped_ptr);

      receive_future_ptrs.emplace_back(&task);
    }
  }

  for (auto* task_ptr : receive_future_ptrs) {
    if (task_ptr && task_ptr->valid()) {
      task_ptr->wait();
    }
  }

  {
    std::lock_guard receive_lock(receive_socket_mutex_);
    receive_polling_tasks_.clear();
  }

  std::vector<std::future<int32_t>*> send_future_ptrs;

  {
    std::lock_guard send_lock(send_socket_mutex_);

    for (auto& [overlapped_ptr, task] : send_polling_tasks_) {
      set_cancelled_state(overlapped_ptr);
      send_future_ptrs.emplace_back(&task);
    }
  }

  for (auto* task_ptr : send_future_ptrs) {
    if (task_ptr && task_ptr->valid()) {
      task_ptr->wait();
    }
  }

  {
    std::lock_guard send_lock(send_socket_mutex_);
    send_polling_tasks_.clear();
  }

  // Only pending requests are cancelled, allow new requests.
  cancel_overlapped_ = false;

  XWSASetLastError(X_WSA_ERROR::X_WSA_NO_ERROR);

  return 0;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  const int ret =
      send(native_handle_, reinterpret_cast<const char*>(buf), buf_len, flags);

#if !XE_PLATFORM_WIN32
  if (ret < 0 && errno == EPIPE && !shutdown_send_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOTCONN);
  }
#endif

  return ret;
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                    XSOCKADDR_IN* to, uint32_t to_len) {
  // 415607FF sends to port 0.
  if (IsAnyAddressDatagram(type_, to) ||
      (type_ == X_SOCK_DGRAM && to && !to->address_port)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRNOTAVAIL);
    return X_SOCKET_ERROR;
  }

  sockaddr addr = {};
  sockaddr* addr_ptr = nullptr;

  if (to) {
    XSOCKADDR_IN target = *to;
    const auto upnp = kernel_state()->emulator()->GetUPnP();

    if (upnp) {
      target.address_port = upnp->GetMappedBindPort(target.address_port);
    }

    addr = target.to_host();

    // Ensure the bound interface can route to the loopback interface/itself
    if (cvars::bind_interface) {
      sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr);

      if (addr_in->sin_addr.s_addr == xe::byte_swap(LOOPBACK)) {
        const auto network_adapter =
            kernel_state()->emulator()->GetNetworkAdapterManager();

        addr_in->sin_addr =
            network_adapter->GetSelectedAdapterLocalIP().sin_addr;
      }
    }

    addr_ptr = &addr;
  }

  // The guest's length is ignored, the whole address is always read.
  int ret = sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                   addr_ptr, to ? sizeof(sockaddr_in) : 0);

  const uint32_t send_error = ret < 0 ? XWSAGetLastError() : 0;

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
    bound_ = true;
  }

  // getsockname resets the last error.
  if (ret < 0) {
    XWSASetLastError(send_error);
  }

  return ret;
}

int XSocket::WSAEventSelect(uint32_t event_handle, uint64_t native_event_handle,
                            uint32_t flags) {
#if XE_PLATFORM_WIN32
  const HANDLE hEvent =
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(native_event_handle));
  const int ret = ::WSAEventSelect(native_handle_, hEvent, flags);

  if (ret == X_ERROR_SUCCESS) {
    event_select_handle_ = flags ? event_handle : 0;
    event_select_flags_ = flags;
  }

  return ret;
#else
  int non_blocking = 1;

  if (ioctl(native_handle_, FIONBIO, &non_blocking) < 0) {
    return X_SOCKET_ERROR;
  }

  event_select_handle_ = flags ? event_handle : 0;
  event_select_flags_ = flags;

  return X_ERROR_SUCCESS;
#endif
}

bool XSocket::IsEventSelectPending(uint32_t event_handle) {
  constexpr uint32_t X_FD_READ = 0x01;
  constexpr uint32_t X_FD_ACCEPT = 0x08;

  if (socket_closed_ || !event_handle || event_select_handle_ != event_handle ||
      !(event_select_flags_ & (X_FD_READ | X_FD_ACCEPT))) {
    return false;
  }

  return WSASelectRead(false, nullptr) > 0;
}

bool XSocket::QueuePacket(uint32_t src_ip, uint16_t src_port,
                          const uint8_t* buf, size_t len) {
  packet* pkt = reinterpret_cast<packet*>(new uint8_t[sizeof(packet) + len]);
  pkt->src_ip = src_ip;
  pkt->src_port = src_port;

  pkt->data_len = (uint16_t)len;
  std::memcpy(pkt->data, buf, len);

  std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
  incoming_packets_.push((uint8_t*)pkt);

  // TODO: Limit on number of incoming packets?
  return true;
}

X_STATUS XSocket::GetPeerName(XSOCKADDR_IN* name, socklen_t* name_len) {
  sockaddr addr = name->to_host();

  int ret = getpeername(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  name->to_guest(&addr);
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(XSOCKADDR_IN* name, socklen_t* name_len) {
  if (!bound_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_STATUS_UNSUCCESSFUL;
  }

  sockaddr addr = name->to_host();

  int ret = getsockname(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  name->to_guest(&addr);
  return X_STATUS_SUCCESS;
}

bool XSocket::XWSAIsKnownError(uint32_t wsa_error) {
  // Xbox error codes might not match with what we receive from OS
  switch (wsa_error) {
    case X_WSA_ERROR::X_WSA_NO_ERROR:
    case X_WSA_ERROR::X_WSA_INVALID_HANDLE:
    case X_WSA_ERROR::X_WSA_INVALID_PARAMETER:
    case X_WSA_ERROR::X_WSA_OPERATION_ABORTED:
    case X_WSA_ERROR::X_WSA_IO_INCOMPLETE:
    case X_WSA_ERROR::X_WSA_IO_PENDING:
    case X_WSA_ERROR::X_WSAEACCES:
    case X_WSA_ERROR::X_WSAEFAULT:
    case X_WSA_ERROR::X_WSAEINVAL:
    case X_WSA_ERROR::X_WSAEWOULDBLOCK:
    case X_WSA_ERROR::X_WSAEINPROGRESS:
    case X_WSA_ERROR::X_WSAEALREADY:
    case X_WSA_ERROR::X_WSAENOTSOCK:
    case X_WSA_ERROR::X_WSAEMSGSIZE:
    case X_WSA_ERROR::X_WSAENOPROTOOPT:
    case X_WSA_ERROR::X_WSAEPROTONOSUPPORT:
    case X_WSA_ERROR::X_WSAESOCKTNOSUPPORT:
    case X_WSA_ERROR::X_WSAEAFNOSUPPORT:
    case X_WSA_ERROR::X_WSAEADDRINUSE:
    case X_WSA_ERROR::X_WSAEADDRNOTAVAIL:
    case X_WSA_ERROR::X_WSAENETDOWN:
    case X_WSA_ERROR::X_WSAECONNRESET:
    case X_WSA_ERROR::X_WSAENOBUFS:
    case X_WSA_ERROR::X_WSAEISCONN:
    case X_WSA_ERROR::X_WSAENOTCONN:
    case X_WSA_ERROR::X_WSAESHUTDOWN:
    case X_WSA_ERROR::X_WSAETIMEDOUT:
    case X_WSA_ERROR::X_WSAECONNREFUSED:
    case X_WSA_ERROR::X_WSAEHOSTUNREACH:
    case X_WSA_ERROR::X_WSASYSNOTREADY:
    case X_WSA_ERROR::X_WSAVERNOTSUPPORTED:
    case X_WSA_ERROR::X_WSANOTINITIALISED:
    case X_WSA_ERROR::X_WSAECANCELLED:
    case X_WSA_ERROR::X_WSASYSCALLFAILURE:
    case X_WSA_ERROR::X_WSAHOST_NOT_FOUND:
    case X_WSA_ERROR::X_WSATRY_AGAIN:
    case X_WSA_ERROR::X_WSANO_DATA:
      return true;
      break;
    default:
      XELOGE("Unsupported X_WSA_ERROR: {}", wsa_error);
      break;
  }

  return false;
}

uint32_t XSocket::XWSAGetLastError() {
  uint32_t last_error = 0;

#ifdef XE_PLATFORM_WIN32
  last_error = WSAGetLastError();
#else
  last_error = errno;

  switch (last_error) {
    case EWOULDBLOCK:
    case EINPROGRESS:
      last_error = X_WSA_ERROR::X_WSAEWOULDBLOCK;
      break;
    case EALREADY:
      last_error = X_WSA_ERROR::X_WSAEALREADY;
      break;
    case EACCES:
      last_error = X_WSA_ERROR::X_WSAEACCES;
      break;
    case EFAULT:
      last_error = X_WSA_ERROR::X_WSAEFAULT;
      break;
    case EINVAL:
      last_error = X_WSA_ERROR::X_WSAEINVAL;
      break;
    case EBADF:
    case ENOTSOCK:
      last_error = X_WSA_ERROR::X_WSAENOTSOCK;
      break;
    case EMSGSIZE:
      last_error = X_WSA_ERROR::X_WSAEMSGSIZE;
      break;
    case ENOPROTOOPT:
      last_error = X_WSA_ERROR::X_WSAENOPROTOOPT;
      break;
    case EPROTONOSUPPORT:
      last_error = X_WSA_ERROR::X_WSAEPROTONOSUPPORT;
      break;
    case ESOCKTNOSUPPORT:
      last_error = X_WSA_ERROR::X_WSAESOCKTNOSUPPORT;
      break;
    case EAFNOSUPPORT:
      last_error = X_WSA_ERROR::X_WSAEAFNOSUPPORT;
      break;
    case EADDRINUSE:
      last_error = X_WSA_ERROR::X_WSAEADDRINUSE;
      break;
    case EADDRNOTAVAIL:
      last_error = X_WSA_ERROR::X_WSAEADDRNOTAVAIL;
      break;
    case ENETDOWN:
      last_error = X_WSA_ERROR::X_WSAENETDOWN;
      break;
    case ECONNRESET:
      last_error = X_WSA_ERROR::X_WSAECONNRESET;
      break;
    case ENOBUFS:
      last_error = X_WSA_ERROR::X_WSAENOBUFS;
      break;
    case EISCONN:
      last_error = X_WSA_ERROR::X_WSAEISCONN;
      break;
    case ENOTCONN:
    case EDESTADDRREQ:
      last_error = X_WSA_ERROR::X_WSAENOTCONN;
      break;
    case ESHUTDOWN:
    case EPIPE:
      last_error = X_WSA_ERROR::X_WSAESHUTDOWN;
      break;
    case ETIMEDOUT:
      last_error = X_WSA_ERROR::X_WSAETIMEDOUT;
      break;
    case ECONNREFUSED:
      last_error = X_WSA_ERROR::X_WSAECONNREFUSED;
      break;
    case EHOSTUNREACH:
      last_error = X_WSA_ERROR::X_WSAEHOSTUNREACH;
      break;
  }
#endif

  bool known = XWSAIsKnownError(last_error);

  return last_error;
}

void XSocket::XWSASetLastError(uint32_t error) const {
  bool known = XWSAIsKnownError(error);

#ifdef XE_PLATFORM_WIN32
  WSASetLastError((int)error);
#endif
  errno = (int)error;
}

}  // namespace kernel
}  // namespace xe
