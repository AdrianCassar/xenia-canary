/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "src/xenia/kernel/xsocket.h"

#include <cstring>
#include <ranges>

#include "xenia/base/platform.h"
#include "xenia/kernel/host_transport.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"

DECLARE_bool(bind_interface);

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

// Translate socket options to native
// Note:
// SO_DONTLINGER = ~SO_LINGER
// SO_EXCLUSIVEADDRUSE = ~SO_REUSEADDR
// Linux has neither, so HostTransport emulates them.
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
    : XObject(kernel_state, kObjectType),
      transport_(std::make_unique<HostTransport>()) {}

XSocket::XSocket(KernelState* kernel_state,
                 std::unique_ptr<SocketTransport> transport)
    : XObject(kernel_state, kObjectType), transport_(std::move(transport)) {}

XSocket::~XSocket() {
  if (!socket_closed_) {
    Close();
  }
}

int XSocket::Initialize(AddressFamily af, Type type, Protocol proto) {
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

  if (!transport_->Open(af ? af : X_AF_INET, type_, proto_)) {
    return X_SOCKET_ERROR;
  }

  return X_ERROR_SUCCESS;
}

int XSocket::Close() {
  // A pending send ends with WSAESHUTDOWN rather than WSAECANCELLED.
  CancelOverlappedIO(X_WSA_ERROR::X_WSAESHUTDOWN);

  int ret = transport_->Close();

  if (ret == X_ERROR_SUCCESS) {
    socket_closed_ = true;
  } else {
    XELOGE("Socket close failed: {}", XWSAGetLastError());
  }

  return ret;
}

int XSocket::GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                       uint32_t* optlen) {
  if (!supported_levels.contains(level)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  const auto& supported_options = level == X_SOL_SOCKET
                                      ? supported_get_socket_options
                                      : supported_tcp_options;

  if (!supported_options.contains(optname)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_SOCKET_ERROR;
  }

  // SO_LINGER and SO_DONTLINGER only apply to stream sockets.
  if (level == X_SOL_SOCKET &&
      (optname == X_SO_LINGER || optname == ~X_SO_LINGER) &&
      type_ == X_SOCK_DGRAM) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_SOCKET_ERROR;
  }

  // LINGER is two 16-bit fields, and the Linux host uses ints, so convert
  // each field instead of byte-swapping the whole option.
  if (level == X_SOL_SOCKET && optname == X_SO_LINGER &&
      *optlen >= sizeof(X_LINGER)) {
    linger native_linger = {};
    socklen_t native_linger_len = sizeof(native_linger);

    if (transport_->GetOption(SOL_SOCKET, SO_LINGER, &native_linger,
                              &native_linger_len) < 0) {
      return X_SOCKET_ERROR;
    }

    X_LINGER* x_linger = static_cast<X_LINGER*>(optval_ptr);
    x_linger->l_onoff = static_cast<uint16_t>(native_linger.l_onoff);
    x_linger->l_linger = static_cast<uint16_t>(native_linger.l_linger);
    *optlen = sizeof(X_LINGER);

    return X_ERROR_SUCCESS;
  }

  // A buffer shorter than the option gets the low byte, the rest of it is
  // zeroed and optlen is set to 1.
  if (*optlen && *optlen < sizeof(uint32_t)) {
    uint32_t value = 0;
    socklen_t value_len = sizeof(value);

    if (transport_->GetOption(supported_levels.at(level),
                              supported_options.at(optname), &value,
                              &value_len) < 0) {
      return X_SOCKET_ERROR;
    }

    std::memset(optval_ptr, 0, *optlen);
    *static_cast<uint8_t*>(optval_ptr) = static_cast<uint8_t>(value);
    *optlen = 1;

    return X_ERROR_SUCCESS;
  }

  int ret = transport_->GetOption(supported_levels.at(level),
                                  supported_options.at(optname), optval_ptr,
                                  reinterpret_cast<socklen_t*>(optlen));

  if (ret < 0) {
    return X_SOCKET_ERROR;
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

  return X_ERROR_SUCCESS;
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

  // SO_LINGER and SO_DONTLINGER only apply to stream sockets.
  if (level == X_SOL_SOCKET &&
      (optname == X_SO_LINGER || optname == ~X_SO_LINGER) &&
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

    ret = transport_->SetOption(SOL_SOCKET, SO_LINGER, &native_linger,
                                sizeof(native_linger));
  } else if (optlen && optlen < sizeof(uint32_t)) {
    // A value shorter than the option is read as its first byte.
    const uint32_t value = *static_cast<uint8_t*>(optval_ptr);

    ret = transport_->SetOption(native_level, native_optname, &value,
                                sizeof(value));
  } else {
    void* proper_ptr =
        GetOptValueWithProperEndianness(optval_ptr, optname, optlen);

    ret =
        transport_->SetOption(native_level, native_optname, proper_ptr, optlen);

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

int XSocket::IOControl(uint32_t cmd, uint32_t* arg_ptr) {
  if (!supported_controls.contains(cmd)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAENOPROTOOPT);
    return X_SOCKET_ERROR;
  }

  if (transport_->IOControl(supported_controls.at(cmd), arg_ptr) < 0) {
    return X_SOCKET_ERROR;
  }

  return X_ERROR_SUCCESS;
}

int XSocket::Connect(const XSOCKADDR_IN* name, int name_len) {
  if (listening_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  if (!name->address_ip.s_addr) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRNOTAVAIL);
    return X_SOCKET_ERROR;
  }

  XSOCKADDR_IN sa_in = *name;

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    sa_in.address_port = upnp->GetMappedConnectPort(name->address_port);
  }

  sockaddr addr = sa_in.to_host();

  int ret = transport_->Connect(&addr);

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = transport_->GetBoundPort();
    bound_ = true;
  }

  if (ret < 0) {
    const uint32_t connect_error = XWSAGetLastError();

    if (connect_error != X_WSA_ERROR::X_WSAEWOULDBLOCK) {
      connect_error_ = connect_error;
    }

    return X_SOCKET_ERROR;
  }

  return X_ERROR_SUCCESS;
}

int XSocket::Bind(const XSOCKADDR_IN* name, int name_len) {
  if (type_ == X_SOCK_DGRAM && IsReservedDatagramPort(name->address_port)) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEADDRINUSE);
    return X_SOCKET_ERROR;
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

  if (transport_->Bind(&addr, name_len) < 0) {
    return X_SOCKET_ERROR;
  }

  bound_port_ = sa_in.address_port;

  if (!bound_port_) {
    bound_port_ = transport_->GetBoundPort();
  }

  bound_ = true;

  return X_ERROR_SUCCESS;
}

int XSocket::Listen(int backlog) {
  if (connect_error_) {
    XWSASetLastError(connect_error_);
    return X_SOCKET_ERROR;
  }

  if (!bound_ || type_ == X_SOCK_DGRAM) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  if (transport_->Listen(backlog) < 0) {
    return X_SOCKET_ERROR;
  }

  listening_ = true;

  return X_ERROR_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(XSOCKADDR_IN* name, int* name_len) {
  sockaddr sa = {};
  socklen_t addrlen = 0;
  const bool is_name_and_name_len_available = name && name_len;

  if (is_name_and_name_len_available) {
    addrlen = xe::byte_swap(*name_len);
  }

  auto accepted =
      transport_->Accept(name ? &sa : nullptr, name_len ? &addrlen : nullptr);
  if (!accepted) {
    return nullptr;
  }

  if (is_name_and_name_len_available) {
    name->to_guest(&sa);
    *name_len = xe::byte_swap(addrlen);
  }

  // Create a kernel object to represent the new socket, and copy parameters
  // over.
  auto socket =
      object_ref<XSocket>(new XSocket(kernel_state_, std::move(accepted)));
  socket->af_ = af_;
  socket->type_ = type_;
  socket->proto_ = proto_;
  socket->vdp_ = vdp_;

  // Implicit Bind
  socket->bound_port_ = bound_port_ = transport_->GetBoundPort();
  socket->bound_ = true;

  return socket;
}

int XSocket::Shutdown(int how) {
  if (how < 0 || how > 2) {
    how = 2;
  }

  const int ret = transport_->Shutdown(how);

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

  return transport_->Recv(buf, buf_len);
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

  int ret = transport_->RecvFrom(buf, buf_len, from ? &sa : nullptr, from_len);

  // TCP receives report the peer's address, e.g. 555307EE reads the port.
  if (proto_ == X_IPPROTO_TCP) {
    socklen_t peer_addr_len = sizeof(sockaddr);
    transport_->GetPeerName(&sa, &peer_addr_len);
  }

  if (from) {
    from->to_guest(&sa);
  }

  return ret;
}

// If wait is true then block until data is available for writing
int XSocket::WSASelectWrite(bool wait, X_WSA_ERROR* error) {
  int activity = 0;

  if (wait) {
    XELOGNETD("{} Blocking...", __func__);
  }

  do {
    activity = transport_->Select(
        true, wait ? static_cast<long>(kOverlappedPollInterval.count()) : 0);

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

    // A cancelled send keeps the error it was cancelled with, which closing
    // the socket sets to WSAESHUTDOWN.
    if (send_async_data.overlapped &&
        poll_write_error != X_WSA_ERROR::X_WSAECANCELLED) {
      send_async_data.overlapped->internal = X_HRESULT_FROM_WIN32(error);
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

  std::vector<HostBuffer> buffers(send_async_data.num_buffers);

  for (uint32_t i = 0; i < send_async_data.num_buffers; i++) {
    buffers[i].len = send_async_data.buffers[i].len;
    buffers[i].data = kernel_state()->memory()->TranslateVirtual<char*>(
        send_async_data.buffers[i].buf_ptr);
  }

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

  const int result =
      transport_->SendBuffers(buffers, saddr_ptr, saddr_len, &bytes_sent);

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = transport_->GetBoundPort();
    bound_ = true;
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
      send_async_data.overlapped->internal =
          X_HRESULT_FROM_WIN32(XWSAGetLastError());
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
  }

  // A pending send also completes when it ends in an error, e.g. the peer
  // resets the connection.
  if (send_async_data.overlapped &&
      (result == X_ERROR_SUCCESS || (wait && !pending))) {
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

  const uint32_t wsa_error =
      X_WIN32_FROM_HRESULT(send_async_data.overlapped->internal.get());

  if (wsa_error == X_STATUS_PENDING) {
    xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);

    send_polling_tasks_[send_async_data.overlapped] =
        std::async(std::launch::async, &XSocket::PollWSASendTo, this, true,
                   send_async_data, nullptr);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    overlapped_ptr->internal = previous_internal;

    XWSASetLastError(wsa_error);

    XELOGI("{}:: failed with error code {}", __func__, wsa_error);
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
    activity = transport_->Select(
        false, wait ? static_cast<long>(kOverlappedPollInterval.count()) : 0);

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

  HostBuffer recv_buffer = {};

  recv_buffer.data = kernel_state()->memory()->TranslateVirtual<char*>(
      receive_async_data.buffers->buf_ptr);
  recv_buffer.len = receive_async_data.buffers->len;

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
  const int result = transport_->ReceiveBuffer(
      recv_buffer, has_from ? &saddr : nullptr, has_from ? &from_len : nullptr,
      &bytes_received, &flags);

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
        transport_->GetPeerName(&saddr, &peer_addar_len);
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

    if (listening_ || transport_->GetPeerName(
                          reinterpret_cast<sockaddr*>(&peer), &peer_len) != 0) {
      XWSASetLastError(X_WSA_ERROR::X_WSAENOTCONN);
      return X_SOCKET_ERROR;
    }
  }

  // Only one overlapped receive can be pending per socket.
  if (overlapped_ptr && ReceivePending()) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEWOULDBLOCK);
    return X_SOCKET_ERROR;
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

    XELOGNETD("{} new overlapped task {:08X}", __func__, overlapped_address);
  }

  const uint32_t previous_internal =
      overlapped_ptr ? overlapped_ptr->internal.get() : 0;

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

    receive_overlapped_ = overlapped_ptr;
    receive_task_ = std::async(std::launch::async, &XSocket::PollWSARecvFrom,
                               this, true, receive_async_data);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    // Only a truncated datagram is reported through the overlapped struct.
    if (wsa_error != X_WSA_ERROR::X_WSAEMSGSIZE) {
      overlapped_ptr->internal = previous_internal;
    }

    XWSASetLastError(wsa_error);

    XELOGI("{}:: failed with error code {}", __func__, wsa_error);
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

  std::unique_lock<std::mutex> task_mutex;

  bool sending_io = false;
  bool receiving_io = false;

  {
    std::lock_guard lock(send_socket_mutex_);
    sending_io = send_polling_tasks_.contains(overlapped_ptr);
  }

  {
    std::lock_guard lock(receive_socket_mutex_);
    receiving_io =
        receive_overlapped_ == overlapped_ptr && receive_task_.valid();
  }

  if (sending_io) {
    task_mutex = std::unique_lock(send_socket_mutex_);
  } else if (receiving_io) {
    task_mutex = std::unique_lock(receive_socket_mutex_);
  }

  // A cancelled request has no task left, and its error is read from Internal.
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
      std::atomic<bool>& cancel = sending_io ? cancel_send_ : cancel_receive_;

      std::future<int32_t> task;
      if (sending_io) {
        auto it = send_polling_tasks_.find(overlapped_ptr);
        if (it != send_polling_tasks_.end()) {
          task = std::move(it->second);
          send_polling_tasks_.erase(it);
        }
      } else {
        task = std::move(receive_task_);
        receive_overlapped_ = nullptr;
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
        receive_overlapped_ = nullptr;
        receive_task_ = {};
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
  return CancelOverlappedIO(X_WSA_ERROR::X_WSAECANCELLED);
}

int XSocket::CancelOverlappedIO(uint32_t send_error) {
  cancel_overlapped_ = true;

  // The tasks are removed, so WSAGetOverlappedResult reads the error from
  // Internal. It is written before the event is signalled, so a waiter never
  // wakes to find the request still pending.
  auto set_cancelled_state = [](XWSAOVERLAPPED* overlapped_ptr,
                                uint32_t error) {
    overlapped_ptr->internal = X_HRESULT_FROM_WIN32(error);

    xboxkrnl::xeNtSetEvent(overlapped_ptr->event_handle, nullptr);
  };

  std::future<int32_t> receive_task;

  {
    std::lock_guard receive_lock(receive_socket_mutex_);

    // A receive that already finished keeps its result.
    if (ReceivePending()) {
      set_cancelled_state(receive_overlapped_, X_WSA_ERROR::X_WSAECANCELLED);
    }

    receive_task = std::move(receive_task_);
    receive_overlapped_ = nullptr;
  }

  // The task takes the mutex to finish.
  if (receive_task.valid()) {
    receive_task.wait();
  }

  std::map<XWSAOVERLAPPED*, std::future<int32_t>> send_tasks;

  {
    std::lock_guard send_lock(send_socket_mutex_);

    for (auto& [overlapped_ptr, task] : send_polling_tasks_) {
      // A send that already finished keeps its result.
      if (task.valid() && task.wait_for(0s) != std::future_status::ready) {
        set_cancelled_state(overlapped_ptr, send_error);
      }
    }

    send_tasks = std::move(send_polling_tasks_);
    send_polling_tasks_.clear();
  }

  // The tasks take the mutex to finish.
  for (auto& [overlapped_ptr, task] : send_tasks) {
    if (task.valid()) {
      task.wait();
    }
  }

  // Only pending requests are cancelled, allow new requests.
  cancel_overlapped_ = false;

  XWSASetLastError(X_WSA_ERROR::X_WSA_NO_ERROR);

  return 0;
}

// Send flags must be 0. They're accepted but ignored, e.g. MSG_OOB data is
// sent as normal data, so don't pass them on.
int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return transport_->Send(buf, buf_len);
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
  // Flags are ignored, like send.
  int ret =
      transport_->SendTo(buf, buf_len, addr_ptr, to ? sizeof(sockaddr_in) : 0);

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = transport_->GetBoundPort();
    bound_ = true;
  }

  return ret;
}

int XSocket::WSAEventSelect(uint32_t event_handle, uint64_t native_event_handle,
                            uint32_t flags) {
  const int ret = transport_->EventSelect(
      native_event_handle, flags,
      [event_handle] { xboxkrnl::xeNtSetEvent(event_handle, nullptr); });

  if (ret == X_ERROR_SUCCESS) {
    event_select_handle_ = flags ? event_handle : 0;
  }

  return ret;
}

void XSocket::EventReset(uint32_t event_handle) {
  if (!socket_closed_ && event_handle && event_select_handle_ == event_handle) {
    transport_->EventReset();
  }
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

int XSocket::GetPeerName(XSOCKADDR_IN* name, socklen_t* name_len) {
  sockaddr addr = name->to_host();

  int ret = transport_->GetPeerName(&addr, name_len);
  if (ret < 0) {
    return X_SOCKET_ERROR;
  }

  name->to_guest(&addr);
  return X_ERROR_SUCCESS;
}

int XSocket::GetSockName(XSOCKADDR_IN* name, socklen_t* name_len) {
  if (!bound_) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEINVAL);
    return X_SOCKET_ERROR;
  }

  sockaddr addr = name->to_host();

  int ret = transport_->GetSockName(&addr, name_len);
  if (ret < 0) {
    return X_SOCKET_ERROR;
  }

  name->to_guest(&addr);
  return X_ERROR_SUCCESS;
}

// The last error belongs to the calling thread, and is kept apart from the
// host's so the host can't change it before it reaches the guest.
namespace {
thread_local uint32_t last_error = 0;
}  // namespace

uint32_t XSocket::XWSAGetLastError() { return last_error; }

void XSocket::XWSASetLastError(uint32_t error) { last_error = error; }

}  // namespace kernel
}  // namespace xe
