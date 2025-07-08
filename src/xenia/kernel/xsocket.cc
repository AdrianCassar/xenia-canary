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

#include "xenia/base/platform.h"
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
  XELOGI("Socket closing!");

  // Cancel overlap tasks if running
  if (polling_task_.valid()) {
    cancel_overlapped_ = true;
  }

  // Wait for PollWSARecvFrom to complete before closing
  std::unique_lock socket_lock(receive_socket_mutex_);

  int ret = 0;

#if XE_PLATFORM_WIN32
  ret = closesocket(static_cast<SOCKET>(native_handle_));
#else
  ret = close(static_cast<int>(native_handle_));
#endif

  if (ret == X_ERROR_SUCCESS) {
    socket_closed_ = true;
  } else {
    XELOGE("Socket close failed: {}", WSAGetLastError());
  }

  // pending_overlapped_io_.clear();

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
    XELOGE("XSocket::SetOption: failed with error {:08X}", GetLastWSAError());
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
  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

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

int XSocket::Shutdown(int how) { return shutdown(native_handle_, how); }

int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags);
}

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                      XSOCKADDR_IN* from, socklen_t* from_len) {
  sockaddr sa = {};

  if (from) {
    sa = from->to_host();
  }

  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len,
                     flags, from ? &sa : nullptr, from_len);

  // 555307EE expects port even with TCP, include IP anyway.
  // Verified on console.
  if (proto_ == X_IPPROTO_TCP) {
    socklen_t peer_addr_len = sizeof(sockaddr);
    getpeername(native_handle_, &sa, &peer_addr_len);
  }

  if (from) {
    from->to_guest(&sa);
  }

  return ret;
}

int XSocket::WSASendTo(XWSABUF* buffers, uint32_t num_buffers,
                       xe::be<uint32_t>* num_bytes_sent_ptr, uint32_t flags,
                       XSOCKADDR_IN* to_ptr, uint32_t to_len,
                       XWSAOVERLAPPED* overlapped_ptr) {
  if (!buffers || !num_buffers || !num_bytes_sent_ptr || flags ||
      to_ptr && (to_len < sizeof(XSOCKADDR_IN) ||
                 to_ptr->address_family != X_AF_INET)) {
    SetLastWSAError(X_WSAError::X_WSA_INVALID_PARAMETER);
    return -1;
  }

  if (overlapped_ptr) {
    pending_overlapped_io_.insert(overlapped_ptr);
  }

  std::vector<uint8_t> combined_buffer_mem;
  uint32_t combined_buffer_size = 0;
  uint32_t combined_buffer_offset = 0;
  for (uint32_t i = 0; i < num_buffers; i++) {
    combined_buffer_size += buffers[i].len;
    combined_buffer_mem.resize(combined_buffer_size);
    uint8_t* combined_buffer = combined_buffer_mem.data();

    std::memcpy(combined_buffer + combined_buffer_offset,
                kernel_memory()->TranslateVirtual(buffers[i].buf_ptr),
                buffers[i].len);
    combined_buffer_offset += buffers[i].len;
  }

  int result = SendTo(combined_buffer_mem.data(), combined_buffer_size, flags,
                      to_ptr, to_len);

  if (result == -1) {
    uint32_t error = WSAGetLastError();

    if (error == (uint32_t)X_WSAError::X_WSAEWOULDBLOCK) {
      XELOGI("{} is pending...", __func__);
      SetLastWSAError(X_WSAError::X_WSA_IO_PENDING);
    } else {
      XELOGE("{} failed with error {}", __func__, error);
      SetLastWSAError((X_WSAError)error);
    }
  } else {
    if (overlapped_ptr) {
      // Hack
      overlapped_ptr->offset_high = 1;
      overlapped_ptr->internal = result;

      if (overlapped_ptr->event_handle) {
        xboxkrnl::xeNtSetEvent(overlapped_ptr->event_handle, nullptr);
      }
    }

    if (num_bytes_sent_ptr) {
      *num_bytes_sent_ptr = result;
    }
  }

  // Immediately complete overlapped
  return 0;
}

// If wait is true then block until data is available for writing
int XSocket::WSAPollWrite(bool wait, X_WSAError* error) {
  WSAPOLLFD fds = {};
  fds.fd = native_handle_;
  fds.events = POLLOUT;

  int activity = 0;

  do {
    activity = WSAPoll(&fds, 1, wait ? 1000 : 0);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSAError::X_WSA_OPERATION_ABORTED;
        activity = -1;
      }
    }

    // if (wait) {
    //   XELOGI("{} Blocking...", __func__);
    // }
  } while (activity == 0 && wait);

  return activity;
}

// If wait is true then block until data is available for reading
int XSocket::WSAPollRead(bool wait, X_WSAError* error) {
  WSAPOLLFD fds = {};
  fds.fd = native_handle_;
  fds.events = POLLIN;

  int activity = 0;

  do {
    activity = WSAPoll(&fds, 1, wait ? 1000 : 0);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSAError::X_WSA_OPERATION_ABORTED;
        activity = -1;
      }
    }

    // if (wait) {
    //   XELOGI("{} Blocking...", __func__);
    // }
  } while (activity == 0 && wait);

  return activity;
}

int XSocket::PollWSARecvFrom(bool wait, WSARecvFromData receive_async_data) {
  if (wait) {
    receive_async_data.overlapped->internal_high =
        (uint32_t)X_WSAError::X_WSAEWOULDBLOCK;
  }

  X_WSAError poll_read_error = X_WSAError::X_WSA_NO_ERROR;

  int result = WSAPollRead(wait, &poll_read_error);

  if (result == -1) {
    // Checking for available data for reading failed.
    uint32_t error = 0;

    if (poll_read_error != X_WSAError::X_WSA_NO_ERROR) {
      error = (uint32_t)poll_read_error;
    } else {
      error = WSAGetLastError();
    }

    receive_async_data.overlapped->internal_high = error;

    XELOGE("PollRead failed with error {}", error);

    std::unique_lock lock(receive_completion_mutex_);
    receive_cv_.notify_all();
    return -1;
  } else if (result == 0) {
    // There's no available data for reading therefore would block.
    receive_async_data.overlapped->internal_high =
        (uint32_t)X_WSAError::X_WSAEWOULDBLOCK;

    std::unique_lock lock(receive_completion_mutex_);
    receive_cv_.notify_all();
    return -1;
  }

  // critical section - lock until we return
  std::unique_lock<std::mutex> socket_lock;

  if (wait) {
    socket_lock = std::unique_lock(receive_socket_mutex_);
  }

  WSABUF recv_buffer = {};

  recv_buffer.buf = kernel_state()->memory()->TranslateVirtual<CHAR*>(
      receive_async_data.buffers->buf_ptr);
  recv_buffer.len = receive_async_data.buffers->len;

  sockaddr* saddr = nullptr;

  if (receive_async_data.from) {
    saddr = new sockaddr();
  }

  DWORD bytes_received = 0;
  DWORD flags = 0;

  result =
      ::WSARecvFrom(native_handle_, &recv_buffer,
                    receive_async_data.num_buffers, &bytes_received, &flags,
                    saddr, &receive_async_data.from_len, nullptr, nullptr);

  if (saddr) {
    receive_async_data.from->to_guest(saddr);
    delete saddr;
  }

  if (result == -1) {
    XELOGI("WSARecvFrom failed with error {}", GetLastWSAError());

    receive_async_data.overlapped->internal_high = GetLastWSAError();
  } else if (result == 0) {
    receive_async_data.overlapped->internal_high = 0;
    receive_async_data.overlapped->internal = bytes_received;

    *receive_async_data.num_bytes_recv = bytes_received;

    if (wait) {
      if (receive_async_data.overlapped->event_handle) {
        xboxkrnl::xeNtSetEvent(receive_async_data.overlapped->event_handle,
                               nullptr);
      }
    }
  }

  *receive_async_data.flags = flags;

  receive_async_data.overlapped->offset = flags;

  receive_cv_.notify_all();

  return result;
}

uint32_t flags = 0;
uint32_t bytes_recv = 0;

int XSocket::WSARecvFrom(XWSABUF* buffers, uint32_t num_buffers,
                         xe::be<uint32_t>* num_bytes_recv_ptr,
                         xe::be<uint32_t>* flags_ptr, XSOCKADDR_IN* from_ptr,
                         xe::be<uint32_t>* fromlen_ptr,
                         XWSAOVERLAPPED* overlapped_ptr) {
  if (!buffers || !flags_ptr || (from_ptr && !fromlen_ptr)) {
    SetLastWSAError(X_WSAError::X_WSA_INVALID_PARAMETER);
    return -1;
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
  receive_async_data.flags = &flags;
  receive_async_data.num_bytes_recv = &bytes_recv;
  receive_async_data.from = from_ptr;
  receive_async_data.from_len = *fromlen_ptr;

  if (!overlapped_ptr) {
    XELOGI("{}:: without overlapped_ptr!", __func__);
  }

  // Wait for PollWSARecvFrom to finish writing to overlapped_ptr
  std::unique_lock socket_lock = std::unique_lock(receive_socket_mutex_);

  XWSAOVERLAPPED tmp_overlapped = {};
  receive_async_data.overlapped =
      overlapped_ptr ? overlapped_ptr : &tmp_overlapped;

  if (overlapped_ptr) {
    pending_overlapped_io_.insert(receive_async_data.overlapped);
  }

  // Check for immediate completion, otherwise perform overlapped completion
  uint32_t result = PollWSARecvFrom(false, receive_async_data);

  if (result == 0) {
    XELOGI("{} completed immediately", __func__);

    if (num_bytes_recv_ptr) {
      *num_bytes_recv_ptr = *receive_async_data.num_bytes_recv;
    }

    *flags_ptr = *receive_async_data.flags;

    return result;
  }

  X_WSAError wsa_error =
      (X_WSAError)receive_async_data.overlapped->internal_high.get();

  if (!overlapped_ptr && wsa_error == X_WSAError::X_WSAEWOULDBLOCK) {
    SetLastWSAError(X_WSAError::X_WSAEWOULDBLOCK);
    return result;
  }

  SetLastWSAError(wsa_error);

  if (overlapped_ptr && wsa_error == X_WSAError::X_WSAEWOULDBLOCK) {
    if (!polling_task_.valid()) {
      if (overlapped_ptr->event_handle) {
        xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);
      }

      polling_task_ = std::async(std::launch::async, &XSocket::PollWSARecvFrom,
                                 this, true, receive_async_data);
    } else {
      std::future_status status = polling_task_.wait_for(0ms);

      if (status == std::future_status::ready) {
        uint32_t result = polling_task_.get();

        uint32_t error_code = GetLastWSAError();

        if (error_code != (uint32_t)X_WSAError::X_WSAEWOULDBLOCK) {
          XELOGI("{} Async:: failed with error code {}", __func__, error_code);
        }
      }
    }

    SetLastWSAError(X_WSAError::X_WSA_IO_PENDING);
  } else {
    // An error occurred that's not X_WSAEWOULDBLOCK
    XELOGI("{}:: failed!", __func__);

    // Check WSA error is not corrupted!
    if (wsa_error !=
        (X_WSAError)receive_async_data.overlapped->internal_high.get()) {
      XELOGI("{}:: Overlapped Corruption!!", __func__);
    }

    if (wsa_error == X_WSAError::X_WSA_OPERATION_ABORTED) {
      XELOGI("{}:: Operation Aborted!", __func__);
      SetLastWSAError(X_WSAError::X_WSAECANCELLED);
    }
  }

  return result;
}

bool XSocket::WSAGetOverlappedResult(XWSAOVERLAPPED* overlapped_ptr,
                                     xe::be<uint32_t>* bytes_transferred,
                                     bool wait, xe::be<uint32_t>* flags_ptr) {
  if (!overlapped_ptr || !bytes_transferred || !flags_ptr) {
    SetLastWSAError(X_WSAError::X_WSA_INVALID_PARAMETER);
    return false;
  }

  if (!pending_overlapped_io_.contains(overlapped_ptr)) {
    XELOGI("Overlap not in operation!");

    *bytes_transferred = 0;
    *flags_ptr = 0;

    return true;
  }

  if (wait) {
    std::unique_lock lock(receive_completion_mutex_);

    XELOGI("{}:: Blocking until completion!", __func__);
    receive_cv_.wait(lock);
  }

  X_WSAError wsa_error = (X_WSAError)overlapped_ptr->internal_high.get();

  switch (wsa_error) {
    case X_WSAError::X_WSA_OPERATION_ABORTED: {
      XELOGI("{}:: Operation Aborted!", __func__);
      SetLastWSAError(X_WSAError::X_WSAECANCELLED);
      return false;
    } break;
    case X_WSAError::X_WSAECANCELLED: {
      XELOGI("{}:: Operation Cancelled!", __func__);
      SetLastWSAError(X_WSAError::X_WSAECANCELLED);
      return false;
    } break;
    case X_WSAError::X_WSAEWOULDBLOCK: {
      SetLastWSAError(X_WSAError::X_WSA_IO_INCOMPLETE);
      return false;
    } break;
    default:
      break;
  }

  if (overlapped_ptr->offset_high == 1) {
    XELOGI("{}:: WSASendTo bytes sent {} with status {}!", __func__,
           overlapped_ptr->internal.get(), overlapped_ptr->internal_high.get());
  } else {
    XELOGI("{}:: WSARecvFrom bytes received {} with status {}!", __func__,
           overlapped_ptr->internal.get(), overlapped_ptr->internal_high.get());
  }

  if (static_cast<uint32_t>(wsa_error) == 0) {
    if (overlapped_ptr->internal == 0) {
      XELOGI("{}:: bytes sent 0!", __func__);
      SetLastWSAError(X_WSAError::X_WSA_IO_INCOMPLETE);
      return false;
    }

    *bytes_transferred = overlapped_ptr->internal;
    *flags_ptr = overlapped_ptr->offset;
  } else {
    XELOGI("{}:: failed with error code {}", __func__,
           overlapped_ptr->internal_high.get());

    SetLastWSAError(X_WSAError::X_WSA_IO_INCOMPLETE);
    return false;
  }

  return true;
}

int XSocket::WSACancelOverlappedIO() {
  if (polling_task_.valid()) {
    cancel_overlapped_ = true;
  }

  for (auto& overlapped_ptr : pending_overlapped_io_) {
    if (overlapped_ptr->event_handle) {
      xboxkrnl::xeNtSetEvent(overlapped_ptr->event_handle, nullptr);
    }

    overlapped_ptr->internal_high = (uint32_t)X_WSAError::X_WSAECANCELLED;
  }

  pending_overlapped_io_.clear();

  SetLastWSAError(X_WSAError::X_WSAECANCELLED);

  return 0;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len,
              flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                    XSOCKADDR_IN* to, uint32_t to_len) {
  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    to->address_port = upnp->GetMappedBindPort(to->address_port);
  }

  sockaddr addr = to->to_host();

  // Ensure the bound interface can route to the loopback interface/itself
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

  // Implicit Bind
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

uint32_t XSocket::GetLastWSAError() {
  // Todo(Gliniak): Provide error mapping table
  // Xbox error codes might not match with what we receive from OS
#ifdef XE_PLATFORM_WIN32
  return WSAGetLastError();
#endif
  return errno;
}

void XSocket::SetLastWSAError(X_WSAError error) const {
#ifdef XE_PLATFORM_WIN32
  WSASetLastError((int)error);
#endif
  errno = (int)error;
}

}  // namespace kernel
}  // namespace xe
