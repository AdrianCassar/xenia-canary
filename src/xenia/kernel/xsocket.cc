/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xsocket.h"

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <ranges>

#include "xenia/base/platform.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xnet_ice.h"
#include "xenia/kernel/xnet_stream.h"
#include "xenia/base/cvar.h"

DECLARE_bool(ice_enabled);
DECLARE_bool(bind_interface);

DECLARE_bool(logging);

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

namespace {
std::mutex g_udp_demux_mutex;
std::unordered_map<uint16_t, XSocket*> g_udp_demux;  // port_nbo -> socket
}  // namespace

void XNetDeliverUdpMux(const XNetMuxPacket& packet) {
  std::lock_guard lock(g_udp_demux_mutex);
  auto it = g_udp_demux.find(xe::byte_swap(packet.id));
  if (it == g_udp_demux.end() || !it->second) {
    // packet.id is BE logical port already from mux
    it = g_udp_demux.find(packet.id);
  }
  if (it == g_udp_demux.end() || !it->second) {
    return;
  }
  it->second->QueuePacket(packet.from_online_ip, packet.id,
                          packet.payload.data(), packet.payload.size());
}

// Translate socket options to native
// Note:
// SO_DONTLINGER = ~SO_LINGER
// SO_EXCLUSIVEADDRUSE = ~SO_REUSEADDR
// TODO: Check SO_DONTLINGER and SO_EXCLUSIVEADDRUSE usage on linux
const std::map<uint32_t, uint32_t> supported_socket_options = {
    {0x0004, SO_REUSEADDR}, {0x0020, SO_BROADCAST}, {0x0080, SO_LINGER},
    {0x1001, SO_SNDBUF},    {0x1002, SO_RCVBUF},    {0x1005, SO_SNDTIMEO},
    {0x1006, SO_RCVTIMEO},  {~0x0080, ~SO_LINGER},  {~0x0004, ~SO_REUSEADDR}};

// Translate socket TCP options to native
const std::map<uint32_t, uint32_t> supported_tcp_options = {
    {0x0001, TCP_NODELAY}};

// Translate socket levels to native
const std::map<uint32_t, uint32_t> supported_levels = {{0xFFFF, SOL_SOCKET},
                                                       {0x6, IPPROTO_TCP}};

// Translate ioctl commands to native
const std::map<uint32_t, uint32_t> supported_controls = {
    {0x8004667E, FIONBIO}, {0x4004667F, FIONREAD}};

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

  native_handle_ = socket(af, type_, proto_);
  if (native_handle_ == X_INVALID_SOCKET) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

int XSocket::Close() {
  WSACancelOverlappedIO();

  {
    std::lock_guard demux_lock(g_udp_demux_mutex);
    auto it = g_udp_demux.find(bound_port_);
    if (it != g_udp_demux.end() && it->second == this) {
      g_udp_demux.erase(it);
    }
  }
  if (cvars::ice_enabled) {
    XNetStream::Instance().Close(this);
  }

  std::unique_lock socket_lock(receive_socket_mutex_);

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
  int ret =
      getsockopt(native_handle_, level, optname, static_cast<char*>(optval_ptr),
                 reinterpret_cast<socklen_t*>(optlen));
  if (ret < 0) {
    // TODO: WSAGetLastError()
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
  if (level == 0xFFFF && (optname == SO_MARKINSECURE || optname == SO_PRIVATE ||
                          optname == SO_GRANTINSECURE)) {
    // Disable socket encryption
    secure_ = false;
    return X_ERROR_SUCCESS;
  }

  int native_level = level;

  assert_false(!supported_levels.contains(level));

  if (supported_levels.contains(level)) {
    native_level = supported_levels.at(level);
  }

  int native_optname = optname;

  if (level == 0xFFFF) {
    assert_false(!supported_socket_options.contains(optname));

    if (supported_socket_options.contains(optname)) {
      native_optname = supported_socket_options.at(optname);
    }
  }

  if (level == IPPROTO_TCP) {
    assert_false(!supported_tcp_options.contains(optname));

    if (supported_tcp_options.contains(optname)) {
      native_optname = supported_tcp_options.at(optname);
    }
  }

  void* proper_ptr =
      GetOptValueWithProperEndianness(optval_ptr, optname, optlen);

  int ret = setsockopt(native_handle_, native_level, native_optname,
                       static_cast<const char*>(proper_ptr), optlen);

  // Cheezy way to check if we created some additional allocation.
  if (optval_ptr != proper_ptr) {
    free(proper_ptr);
  }

  if (ret < 0) {
    // TODO: WSAGetLastError()
    XELOGE("XSocket::SetOption: failed with error {:08X}", XWSAGetLastError());
    return X_SOCKET_ERROR;
  }

  if (level == 0xFFFF && optname == 0x0020) {
    broadcast_socket_ = true;
  }

  return X_ERROR_SUCCESS;
}

X_STATUS XSocket::IOControl(uint32_t cmd, uint32_t* arg_ptr) {
#ifdef XE_PLATFORM_WIN32
  const u_long initial_param = xe::load_and_swap<uint32_t>(arg_ptr);
  u_long param = initial_param;

  int ret = ioctlsocket(native_handle_, cmd, &param);

  // Parameter was written to therefore byte swap output
  if (initial_param != param) {
    xe::store_and_swap(arg_ptr, static_cast<uint32_t>(param));
  }

  if (ret < 0) {
    // TODO: Get last error
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
#else
  int native_cmd = cmd;

  assert_false(!supported_controls.contains(cmd));

  if (supported_controls.contains(cmd)) {
    native_cmd = supported_controls.at(cmd);
  }

  int ret = ioctl(native_handle_, native_cmd, arg_ptr);

  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
#endif
}

X_STATUS XSocket::Connect(const XSOCKADDR_IN* name, int name_len) {
  XSOCKADDR_IN sa_in = *name;

  if (cvars::ice_enabled &&
      XNetIce::IsPrivateOnlineAddress(sa_in.address_ip.s_addr) &&
      proto_ == X_IPPROTO_TCP) {
    XNetIce::Instance().Establish(sa_in.address_ip.s_addr, XNetPeerType::kTitle);
    if (XNetStream::Instance().Connect(this, sa_in.address_ip.s_addr,
                                       sa_in.address_port) == 0) {
      bound_port_ = sa_in.address_port;
      bound_ = true;
      return X_STATUS_SUCCESS;
    }
    return X_STATUS_UNSUCCESSFUL;
  }

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    sa_in.address_port = upnp->GetMappedConnectPort(name->address_port);
  }

  sockaddr addr = sa_in.to_host();

  int ret = connect(native_handle_, &addr, name_len);

  // Implicit Bind
  bound_port_ = sa_in.address_port;
  bound_ = true;

  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(const XSOCKADDR_IN* name, int name_len) {
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

  {
    std::lock_guard lock(g_udp_demux_mutex);
    g_udp_demux[bound_port_] = this;
  }

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

  assert_always();
  return 0;
}

X_STATUS XSocket::Listen(int backlog) {
  if (cvars::ice_enabled && proto_ == X_IPPROTO_TCP && bound_) {
    XNetStream::Instance().Listen(this, bound_port_);
  }

  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    // ICE listeners can still accept without a working native listen.
    if (cvars::ice_enabled && proto_ == X_IPPROTO_TCP && bound_) {
      return X_STATUS_SUCCESS;
    }
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(XSOCKADDR_IN* name, int* name_len) {
  if (cvars::ice_enabled && proto_ == X_IPPROTO_TCP &&
      XNetStream::Instance().HasPendingAccept(this)) {
    uint16_t stream_id = 0;
    uint32_t remote_ip = 0;
    uint16_t remote_port = 0;
    if (XNetStream::Instance().TakeAccept(this, &stream_id, &remote_ip,
                                          &remote_port)) {
      const uint64_t socket_handle =
          ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (socket_handle == X_INVALID_SOCKET) {
        return nullptr;
      }
      auto socket =
          object_ref<XSocket>(new XSocket(kernel_state_, socket_handle));
      socket->af_ = af_;
      socket->type_ = type_;
      socket->proto_ = proto_;
      socket->vdp_ = vdp_;
      socket->bound_port_ = bound_port_;
      socket->bound_ = true;
      XNetStream::Instance().AttachSocket(stream_id, socket.get());
      if (name) {
        name->address_family = XSocket::AddressFamily::X_AF_INET;
        name->address_port = remote_port;
        name->address_ip.s_addr = remote_ip;
        if (name_len) {
          *name_len = sizeof(XSOCKADDR_IN);
        }
      }
      return socket;
    }
  }

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

int XSocket::Shutdown(int how) { return shutdown(native_handle_, how); }

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                      XSOCKADDR_IN* from, socklen_t* from_len) {
  {
    std::lock_guard lock(incoming_packet_mutex_);
    if (!incoming_packets_.empty()) {
      auto* raw = incoming_packets_.front();
      incoming_packets_.pop();
      auto* pkt = reinterpret_cast<packet*>(raw);
      const uint32_t copy =
          std::min<uint32_t>(buf_len, pkt->data_len);
      std::memcpy(buf, pkt->data, copy);
      if (from) {
        from->address_family = X_AF_INET;
        from->address_port = pkt->src_port;
        from->address_ip.s_addr = pkt->src_ip;
        // Halo (and MCC) allocate sockaddr_in6 (28). If from_len stays 28,
        // get_transport_address takes the AF_INET6 path and copies sin_zero
        // as an all-zero IPv6 address → transport_address_valid IPV6 warning.
        if (from_len) {
          *from_len = sizeof(XSOCKADDR_IN);
        }
      }
      delete[] raw;
      return static_cast<int>(copy);
    }
  }

  sockaddr sa = {};

  if (from) {
    sa = from->to_host();
  }

  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len,
                     flags, from ? &sa : nullptr, from_len);

  if (proto_ == X_IPPROTO_TCP) {
    socklen_t peer_addr_len = sizeof(sockaddr);
    getpeername(native_handle_, &sa, &peer_addr_len);
  }

  if (from) {
    from->to_guest(&sa);
  }

  return ret;
}

// If wait is true then block until data is available for writing
int XSocket::WSASelectWrite(bool wait, X_WSA_ERROR* error) {
  int activity = 0;

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
    tv.tv_sec = wait ? 1 : 0;
    tv.tv_usec = 0;

    activity = select(nfds, nullptr, &writefds, nullptr, &tv);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSAECANCELLED;
        // *error = X_HRESULT_FROM_WIN32(X_WSA_ERROR::X_WSAECANCELLED);
      }

      activity = X_SOCKET_ERROR;
    }

    if (cvars::logging && wait) {
      XELOGI("{} Blocking...", __func__);
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
      error = static_cast<uint32_t>(poll_write_error);
    } else {
      error = XWSAGetLastError();
    }

    XELOGE("WSASelectWrite failed with error {}", error);

    if (send_async_data.overlapped->internal) {
      send_async_data.overlapped->internal = error;
    }

    return X_SOCKET_ERROR;
  }

  if (data_available == X_ERROR_SUCCESS) {
    // There's no available space for writing therefore return and start
    // pending operation.
    if (send_async_data.overlapped->internal) {
      send_async_data.overlapped->internal = X_STATUS_PENDING;
    }

    return X_SOCKET_ERROR;
  }

  std::vector<WSABUF> buffers(send_async_data.num_buffers);

  for (uint32_t i = 0; i < send_async_data.num_buffers; i++) {
    buffers[i].len = send_async_data.buffers[i].len;
    buffers[i].buf = kernel_state()->memory()->TranslateVirtual<CHAR*>(
        send_async_data.buffers[i].buf_ptr);
  }

  sockaddr saddr = send_async_data.to->to_host();
  sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&saddr);

  DWORD bytes_sent = 0;
  DWORD flags = 0;

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    addr_in->sin_port = upnp->GetMappedBindPort(addr_in->sin_port);
  }

  // Ensure the bound interface can route to the loopback interface/itself
  if (cvars::bind_interface) {
    if (addr_in->sin_addr.s_addr == xe::byte_swap(LOOPBACK)) {
      const auto network_adapter =
          kernel_state()->emulator()->GetNetworkAdapterManager();

      addr_in->sin_addr = network_adapter->GetSelectedAdapterLocalIP().sin_addr;
    }
  }

  const int result = ::WSASendTo(
      native_handle_, buffers.data(), send_async_data.num_buffers, &bytes_sent,
      0, &saddr, send_async_data.to_len, nullptr, nullptr);

  // Implicit Bind
  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
    bound_ = true;
  }

  const bool pending =
      XWSAGetLastError() ==
          static_cast<uint32_t>(X_WSA_ERROR::X_WSA_IO_PENDING) ||
      XWSAGetLastError() ==
          static_cast<uint32_t>(X_WSA_ERROR::X_WSAEWOULDBLOCK);

  if (result == X_SOCKET_ERROR && !pending) {
    XELOGI("WSASendTo failed with error {}", XWSAGetLastError());
  }

  if (send_async_data.overlapped && result == X_SOCKET_ERROR) {
    if (pending) {
      send_async_data.overlapped->internal = X_STATUS_PENDING;
    } else {
      send_async_data.overlapped->internal = XWSAGetLastError();

      // send_async_data.overlapped->internal =
      //     X_HRESULT_FROM_WIN32(XWSAGetLastError());
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
    send_async_data.overlapped->internal =
        uint32_t(X_WSA_ERROR::X_WSA_NO_ERROR);

    xboxkrnl::xeNtSetEvent(send_async_data.overlapped->event_handle, nullptr);
  }

  return result;
}

int XSocket::WSASendTo(XWSABUF* buffers, uint32_t num_buffers,
                       xe::be<uint32_t>* num_bytes_sent_ptr, uint32_t flags,
                       XSOCKADDR_IN* to_ptr, uint32_t to_len,
                       XWSAOVERLAPPED* overlapped_ptr) {
  if (!buffers || !num_buffers || !num_bytes_sent_ptr || flags ||
      to_ptr && (to_len < sizeof(XSOCKADDR_IN) ||
                 to_ptr->address_family != X_AF_INET)) {
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return X_SOCKET_ERROR;
  }

  WSASendToData send_async_data = {};

  // These may have been on the stack - copy them.
  send_async_data.buffers = std::make_shared<XWSABUF[]>(num_buffers);
  std::memcpy(send_async_data.buffers.get(), buffers,
              num_buffers * sizeof(XWSABUF));

  send_async_data.num_buffers = num_buffers;
  send_async_data.to = to_ptr;
  send_async_data.to_len = to_len;

  send_async_data.overlapped = overlapped_ptr;

  if (overlapped_ptr && cvars::logging) {
    const uint32_t overlapped_address =
        kernel_state()->memory()->HostToGuestVirtual(
            std::to_address(overlapped_ptr));

    if (!send_polling_tasks_.contains(overlapped_ptr)) {
      XELOGI("{} new overlapped task {:08X}", __func__, overlapped_address);
    } else {
      XELOGI("{} overlapped task {:08X} is pending!", __func__,
             overlapped_address);
    }
  }

  uint32_t num_bytes_sent = 0;

  // Check for immediate completion.
  const int result = PollWSASendTo(false, send_async_data, &num_bytes_sent);

  if (!overlapped_ptr) {
    if (result == X_ERROR_SUCCESS && num_bytes_sent_ptr) {
      *num_bytes_sent_ptr = num_bytes_sent;
    }

    return result;
  }

  if (result == X_ERROR_SUCCESS) {
    if (cvars::logging) {
      XELOGI("{} completed immediately", __func__);
    }

    if (num_bytes_sent_ptr) {
      *num_bytes_sent_ptr = num_bytes_sent;
    }

    return result;
  }

  // Perform overlapped completion.

  const X_WSA_ERROR wsa_error =
      X_WSA_ERROR(send_async_data.overlapped->internal.get());

  if (overlapped_ptr && wsa_error == X_WSA_ERROR(X_STATUS_PENDING)) {
    xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);

    send_polling_tasks_[send_async_data.overlapped] =
        std::async(std::launch::async, &XSocket::PollWSASendTo, this, true,
                   send_async_data, nullptr);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    XWSASetLastError(wsa_error);

    // An error occurred that's not X_STATUS_PENDING
    XELOGI("{}:: failed with error code {}", __func__,
           static_cast<uint32_t>(wsa_error));

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
    tv.tv_sec = wait ? 1 : 0;
    tv.tv_usec = 0;

    activity = select(nfds, &readfds, nullptr, nullptr, &tv);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSAECANCELLED;
        // *error = X_HRESULT_FROM_WIN32(X_WSA_ERROR::X_WSAECANCELLED);
      }

      activity = X_SOCKET_ERROR;
    }

    if (cvars::logging && wait) {
      XELOGI("{} Blocking...", __func__);
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
      error = static_cast<uint32_t>(poll_read_error);
    } else {
      error = XWSAGetLastError();
    }

    XELOGE("WSASelectRead failed with error {}", error);

    if (receive_async_data.overlapped) {
      receive_async_data.overlapped->internal = error;
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

  WSABUF recv_buffer = {};

  recv_buffer.buf = kernel_state()->memory()->TranslateVirtual<CHAR*>(
      receive_async_data.buffers->buf_ptr);
  recv_buffer.len = receive_async_data.buffers->len;

  sockaddr saddr = {};

  if (receive_async_data.from) {
    saddr = receive_async_data.from->to_host();
  }

  DWORD bytes_received = 0;
  DWORD flags = 0;
  int from_len = *receive_async_data.from_len;

  // num_bytes_recv and flags are only updated on immediate completion.
  if (!wait && receive_async_data.flags) {
    flags = receive_async_data.flags->get();
  }

  const int result = ::WSARecvFrom(
      native_handle_, &recv_buffer, receive_async_data.num_buffers,
      &bytes_received, &flags, receive_async_data.from ? &saddr : nullptr,
      receive_async_data.from_len ? &from_len : nullptr, nullptr, nullptr);

  const bool pending =
      XWSAGetLastError() ==
          static_cast<uint32_t>(X_WSA_ERROR::X_WSA_IO_PENDING) ||
      XWSAGetLastError() ==
          static_cast<uint32_t>(X_WSA_ERROR::X_WSAEWOULDBLOCK);

  if (result == X_SOCKET_ERROR && !pending) {
    XELOGI("WSARecvFrom failed with error {}", XWSAGetLastError());
  }

  if (receive_async_data.overlapped && result == X_SOCKET_ERROR) {
    if (pending) {
      receive_async_data.overlapped->internal = X_STATUS_PENDING;
    } else {
      receive_async_data.overlapped->internal = XWSAGetLastError();

      // receive_async_data.overlapped->internal =
      //     X_HRESULT_FROM_WIN32(XWSAGetLastError());
    }
  }

  if (result == X_ERROR_SUCCESS) {
    if (receive_async_data.from) {
      // Copy behavior from recvfrom.
      // Verified on console.
      if (proto_ == X_IPPROTO_TCP) {
        socklen_t peer_addar_len = sizeof(sockaddr);
        getpeername(native_handle_, &saddr, &peer_addar_len);
      }

      receive_async_data.from->to_guest(&saddr);
    }

    if (receive_async_data.from_len) {
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
    receive_async_data.overlapped->internal =
        static_cast<uint32_t>(X_WSA_ERROR::X_WSA_NO_ERROR);

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
  if (!buffers || !flags_ptr || (from_ptr && !fromlen_ptr)) {
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return X_SOCKET_ERROR;
  }

  // On win32 we could pipe all this directly to WSARecvFrom.
  // We would however need find a way to call the completion callback without
  // relying on the caller to set the "alertable" flag to true when waiting. We
  // also need to do our own async handling anyway for Linux so we might as well
  // make the code paths the same to improve symmetry in behavior.

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

  if (overlapped_ptr && cvars::logging) {
    const uint32_t overlapped_address =
        kernel_state()->memory()->HostToGuestVirtual(
            std::to_address(overlapped_ptr));

    if (!receive_polling_tasks_.contains(overlapped_ptr)) {
      XELOGI("{} new overlapped task {:08X}", __func__, overlapped_address);
    } else {
      XELOGI("{} overlapped task {:08X} is pending!", __func__,
             overlapped_address);
    }
  }

  // Check for immediate completion.
  int result = PollWSARecvFrom(false, receive_async_data);

  if (!overlapped_ptr) {
    return result;
  }

  if (result == X_ERROR_SUCCESS) {
    if (cvars::logging) {
      XELOGI("{} completed immediately", __func__);
    }

    return result;
  }

  // Perform overlapped completion.

  const X_WSA_ERROR wsa_error =
      X_WSA_ERROR(receive_async_data.overlapped->internal.get());

  if (wsa_error == X_WSA_ERROR(X_STATUS_PENDING)) {
    xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);

    receive_polling_tasks_[receive_async_data.overlapped] =
        std::async(std::launch::async, &XSocket::PollWSARecvFrom, this, true,
                   receive_async_data);

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    XWSASetLastError(wsa_error);

    // An error occurred that's not X_STATUS_PENDING
    XELOGI("{}:: failed with error code {}", __func__,
           static_cast<uint32_t>(wsa_error));

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
      X_WSA_ERROR internal_result =
          X_WSA_ERROR(X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get()));
      XWSASetLastError(internal_result);
      return false;
    }
  }

  const bool pending_io = sending_io || receiving_io;

  // 4D530808 does this.
  if (!pending_io) {
    if (cvars::logging) {
      XELOGI("Overlap not in operation!");
    }

    *bytes_transferred = overlapped_ptr->internal_high;
    *flags_ptr = overlapped_ptr->offset;

    return true;
  }

  std::future<int32_t>* current_task_ptr = nullptr;

  if (sending_io) {
    auto it = send_polling_tasks_.find(overlapped_ptr);
    if (it != send_polling_tasks_.end()) {
      current_task_ptr = &it->second;
    }
  } else if (receiving_io) {
    auto it = receive_polling_tasks_.find(overlapped_ptr);
    if (it != receive_polling_tasks_.end()) {
      current_task_ptr = &it->second;
    }
  }

  X_WSA_ERROR internal_result =
      X_WSA_ERROR(X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get()));

  bool valid_hresult =
      X_HRESULT_FACILITY(overlapped_ptr->internal.get()) == X_FACILITY_WIN32;

  // If result is invalid HRESULT then recover it.
  // if (!valid_hresult) {
  //   internal_result = X_WSA_ERROR(XThread::GetLastError());
  // }

  if (wait) {
    if (cvars::logging) {
      if (sending_io) {
        XELOGI("{}:: WSASendTo blocking until completion!", __func__);
      } else if (receiving_io) {
        XELOGI("{}:: WSARecvFrom Blocking until completion!", __func__);
      }
    }

    auto event = kernel_state()->object_table()->LookupObject<XEvent>(
        overlapped_ptr->event_handle);

    bool pending = static_cast<uint32_t>(internal_result) == X_STATUS_PENDING;

    if (pending) {
      // Unlock so overlapped task can update internal result after completion.
      task_mutex.unlock();
    }

    if (event) {
      // https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-overlapped-input-and-output
      // 434D07D8 uses an auto reset event which causes signaled event
      // notifications to be missed causing an infinite wait deadlock.
      // As a result we check if task is completed before deciding to wait for
      // event.
      if (pending) {
        event->Wait(3, 1, 0, nullptr);
      }
    } else {
      // If we don't have an event then we must wait for the future to complete.
      // This should never happen because overlapped_ptr->event_handle should
      // always have a valid event handle.
      assert_always();
      if (pending && current_task_ptr && current_task_ptr->valid()) {
        current_task_ptr->wait();
      }
    }

    if (pending) {
      task_mutex.lock();

      // Update result after task completion.
      internal_result =
          X_WSA_ERROR(X_WIN32_FROM_HRESULT(overlapped_ptr->internal.get()));
    }
  }

  bool result = false;

  switch (internal_result) {
    case X_WSA_ERROR::X_WSA_NO_ERROR: {
      if (cvars::logging) {
        const uint32_t overlapped_address =
            kernel_state()->memory()->HostToGuestVirtual(
                std::to_address(overlapped_ptr));

        std::string operation = sending_io ? "WSASendTo" : "WSARecvFrom";
        std::string action = sending_io ? "sent" : "received";

        XELOGI(
            "{}:: {} overlapped task {:08X} {} {} bytes with "
            "status {}!",
            __func__, operation, overlapped_address, action,
            overlapped_ptr->internal_high.get(),
            overlapped_ptr->internal.get());
      }

      *bytes_transferred = overlapped_ptr->internal_high;
      *flags_ptr = overlapped_ptr->offset;

      xboxkrnl::xeNtSetEvent(overlapped_ptr->event_handle, nullptr);

      if (sending_io) {
        send_polling_tasks_.erase(overlapped_ptr);
      } else if (receiving_io) {
        receive_polling_tasks_.erase(overlapped_ptr);
      }

      current_task_ptr = nullptr;

      result = true;
    } break;
    case X_WSA_ERROR::X_WSAECANCELLED: {
      XELOGD("{}:: Operation Cancelled!", __func__);
      XWSASetLastError(internal_result);
      result = false;
    } break;
    case X_WSA_ERROR(X_STATUS_PENDING):
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
        X_HRESULT_FROM_WIN32(uint32_t(X_WSA_ERROR::X_WSAECANCELLED));

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

  XWSASetLastError(X_WSA_ERROR::X_WSA_NO_ERROR);

  return 0;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  if (cvars::ice_enabled && proto_ == X_IPPROTO_TCP) {
    int sent = XNetStream::Instance().Send(this, buf, buf_len);
    if (sent >= 0) {
      return sent;
    }
  }
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len,
              flags);
}

int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  if (cvars::ice_enabled && proto_ == X_IPPROTO_TCP) {
    int got = XNetStream::Instance().Recv(this, buf, buf_len);
    if (got == -2) {
      SetLastWSAError(X_WSAError::X_WSAEWOULDBLOCK);
      return -1;
    }
    if (got >= 0) {
      return got;
    }
  }
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                    XSOCKADDR_IN* to, uint32_t to_len) {
  if (to && cvars::ice_enabled &&
      XNetIce::IsPrivateOnlineAddress(to->address_ip.s_addr)) {
    XNetIce::Instance().Establish(to->address_ip.s_addr, XNetPeerType::kTitle);
    if (XNetIce::Instance().HasPeer(to->address_ip.s_addr,
                                    XNetPeerType::kTitle)) {
      if (!bound_) {
        bound_port_ = GetImplicitlyBoundPort();
        bound_ = true;
        std::lock_guard lock(g_udp_demux_mutex);
        g_udp_demux[bound_port_] = this;
      }
      int sent = XNetIce::Instance().SendMux(
          to->address_ip.s_addr, kXNetMuxUdp, bound_port_, buf, buf_len);
      if (sent >= 0) {
        return sent;
      }
    }
  }

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp && to) {
    to->address_port = upnp->GetMappedBindPort(to->address_port);
  }

  sockaddr addr = to->to_host();

  if (cvars::bind_interface) {
    sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(&addr);

    if (addr_in->sin_addr.s_addr == xe::byte_swap(LOOPBACK)) {
      const auto network_adapter =
          kernel_state()->emulator()->GetNetworkAdapterManager();

      addr_in->sin_addr = network_adapter->GetSelectedAdapterLocalIP().sin_addr;
    }
  }

  int ret = sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                   to ? &addr : nullptr, to_len);

  if (!bound_port_) {
    bound_port_ = GetImplicitlyBoundPort();
    bound_ = true;
  }

  return ret;
}

int XSocket::WSAEventSelect(uint64_t socket_handle, uint64_t event_handle,
                            uint32_t flags) {
  const HANDLE hEvent =
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(event_handle));
  return ::WSAEventSelect(socket_handle, hEvent, flags);
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

X_STATUS XSocket::GetPeerName(XSOCKADDR_IN* name, int* name_len) {
  sockaddr addr = name->to_host();

  int ret = getpeername(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  name->to_guest(&addr);
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(XSOCKADDR_IN* name, int* name_len) {
  sockaddr addr = name->to_host();

  int ret = getsockname(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  name->to_guest(&addr);
  return X_STATUS_SUCCESS;
}

bool XSocket::XWSAIsKnownError(X_WSA_ERROR wsa_error) {
  // Xbox error codes might not match with what we receive from OS
  switch (wsa_error) {
    case X_WSA_ERROR::X_WSA_NO_ERROR:
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
      XELOGE("Unsupported X_WSA_ERROR: {}", static_cast<uint32_t>(wsa_error));
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
#endif

  bool known = XWSAIsKnownError(static_cast<X_WSA_ERROR>(last_error));

  return last_error;
}

void XSocket::XWSASetLastError(X_WSA_ERROR error) const {
  bool known = XWSAIsKnownError(error);

#ifdef XE_PLATFORM_WIN32
  WSASetLastError((int)error);
#endif
  errno = (int)error;
}

}  // namespace kernel
}  // namespace xe
