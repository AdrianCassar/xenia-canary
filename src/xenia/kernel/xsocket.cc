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

#include "xenia/kernel/XLiveAPI.h"

DECLARE_bool(logging);

using namespace std::chrono_literals;

namespace xe {
namespace kernel {

XSocket::XSocket(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

XSocket::XSocket(KernelState* kernel_state, uint64_t native_handle)
    : XObject(kernel_state, kObjectType), native_handle_(native_handle) {}

XSocket::~XSocket() { Close(); }

X_STATUS XSocket::Initialize(AddressFamily af, Type type, Protocol proto) {
  af_ = af;
  type_ = type;
  proto_ = proto;

  if (proto == Protocol::X_IPPROTO_VDP) {
    // VDP is a layer on top of UDP.
    proto = Protocol::X_IPPROTO_UDP;
  }

  native_handle_ = socket(af, type, proto);
  if (native_handle_ == X_INVALID_SOCKET) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Close() {
  // Cancel overlap tasks if running
  if (polling_task_.valid()) {
    cancel_overlapped_ = true;
  }

  // Wait for PollWSARecvFrom to complete before closing
  std::unique_lock socket_lock(receive_socket_mutex_);

  int ret = closesocket(native_handle_);

  if (ret != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                            uint32_t* optlen) {
  int ret =
      getsockopt(native_handle_, level, optname, static_cast<char*>(optval_ptr),
                 reinterpret_cast<socklen_t*>(optlen));

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

  if (ret < 0) {
    // TODO: WSAGetLastError()
    return X_STATUS_UNSUCCESSFUL;
  }
  return X_STATUS_SUCCESS;
}
X_STATUS XSocket::SetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                            uint32_t optlen) {
  if (level == 0xFFFF && (optname == 0x5801 || optname == 0x5802)) {
    // Disable socket encryption
    secure_ = false;
    return X_STATUS_SUCCESS;
  }

  void* proper_ptr =
      GetOptValueWithProperEndianness(optval_ptr, optname, optlen);

  int ret = setsockopt(native_handle_, level, optname, (const char*)proper_ptr,
                       optlen);

  // Cheezy way to check if we created some additional allocation.
  if (optval_ptr != proper_ptr) {
    free(proper_ptr);
  }

  if (ret < 0) {
    // TODO: WSAGetLastError()
    return X_STATUS_UNSUCCESSFUL;
  }

  // SO_BROADCAST
  if (level == 0xFFFF && optname == 0x0020) {
    broadcast_socket_ = true;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::IOControl(uint32_t cmd, uint8_t* arg_ptr) {
#ifdef XE_PLATFORM_WIN32
  int ret = ioctlsocket(native_handle_, cmd, (u_long*)arg_ptr);
  if (ret < 0) {
    // TODO: Get last error
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
#elif XE_PLATFORM_LINUX
  return X_STATUS_UNSUCCESSFUL;
#endif
}

X_STATUS XSocket::Connect(const XSOCKADDR_IN* name, int name_len) {
  XSOCKADDR_IN sa_in = XSOCKADDR_IN();
  memcpy(&sa_in, name, sizeof(XSOCKADDR_IN));

  sa_in.address_port =
      XLiveAPI::upnp_handler->GetMappedConnectPort(name->address_port);

  sockaddr addr = sa_in.to_host();

  int ret = connect(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(const XSOCKADDR_IN* name, int name_len) {
  XSOCKADDR_IN sa_in = XSOCKADDR_IN();
  memcpy(&sa_in, name, sizeof(XSOCKADDR_IN));

  sa_in.address_port =
      XLiveAPI::upnp_handler->GetMappedBindPort(name->address_port);

  sockaddr addr = sa_in.to_host();

  int ret = bind(native_handle_, &addr, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_port_ = sa_in.address_port;

  if (!bound_port_) {
    XSOCKADDR_IN sa = *name;

    if (!GetSockName(&sa, &name_len)) {
      bound_port_ = sa.address_port;
    }
  }

  bound_ = true;

  return X_STATUS_SUCCESS;
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
  int addrlen = 0;
  const bool is_name_and_name_len_available = name && name_len;

  if (is_name_and_name_len_available) {
    addrlen = byte_swap(*name_len);
  }

  const uint64_t ret = accept(native_handle_, name ? &sa : nullptr,
                              name_len ? &addrlen : nullptr);
  if (ret == X_INVALID_SOCKET) {
    return nullptr;
  }

  if (is_name_and_name_len_available) {
    name->to_guest(&sa);
    *name_len = byte_swap(addrlen);
  }
  // Create a kernel object to represent the new socket, and copy parameters
  // over.
  auto socket = object_ref<XSocket>(new XSocket(kernel_state_, ret));
  socket->af_ = af_;
  socket->type_ = type_;
  socket->proto_ = proto_;

  return socket;
}

int XSocket::Shutdown(int how) { return shutdown(native_handle_, how); }

int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags);
}

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                      XSOCKADDR_IN* from, uint32_t* from_len) {
  sockaddr sa{};

  if (from) {
    sa = from->to_host();
  }

  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len,
                     flags, from ? &sa : nullptr, (int*)from_len);

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
    XWSASetLastError(X_WSA_ERROR::X_WSA_INVALID_PARAMETER);
    return X_SOCKET_ERROR;
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

  if (result == X_SOCKET_ERROR) {
    uint32_t error = XWSAGetLastError();

    if (error == (uint32_t)X_WSA_ERROR::X_WSAEWOULDBLOCK) {
      XELOGI("{} is pending...", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
    } else {
      XELOGE("{} failed with error {}", __func__, error);
      XWSASetLastError((X_WSA_ERROR)error);
    }
  } else {
    if (overlapped_ptr) {
      // Hack
      overlapped_ptr->offset_high = 1;
      overlapped_ptr->internal_high = result;

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
int XSocket::WSAPollWrite(bool wait, X_WSA_ERROR* error) {
  WSAPOLLFD fds = {};
  fds.fd = native_handle_;
  fds.events = POLLOUT;

  int activity = 0;

  do {
    activity = WSAPoll(&fds, 1, wait ? 1000 : 0);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSA_OPERATION_ABORTED;
        activity = X_SOCKET_ERROR;
      }
    }

    if (cvars::logging) {
      if (wait) {
        XELOGI("{} Blocking...", __func__);
      }
    }
  } while (activity == 0 && wait);

  return activity;
}

// If wait is true then block until data is available for reading
int XSocket::WSAPollRead(bool wait, X_WSA_ERROR* error) {
  WSAPOLLFD fds = {};
  fds.fd = native_handle_;
  fds.events = POLLIN;

  int activity = 0;

  do {
    activity = WSAPoll(&fds, 1, wait ? 1000 : 0);

    if (cancel_overlapped_) {
      if (error) {
        *error = X_WSA_ERROR::X_WSA_OPERATION_ABORTED;
        activity = X_SOCKET_ERROR;
      }
    }

    if (cvars::logging) {
      if (wait) {
        XELOGI("{} Blocking...", __func__);
      }
    }
  } while (activity == 0 && wait);

  return activity;
}

int XSocket::PollWSARecvFrom(bool wait, WSARecvFromData receive_async_data) {
  if (wait) {
    // Change?
    receive_async_data.overlapped->internal =
        (uint32_t)X_WSA_ERROR::X_WSAEWOULDBLOCK;
  }

  X_WSA_ERROR poll_read_error = X_WSA_ERROR::X_WSA_NO_ERROR;

  int result = WSAPollRead(wait, &poll_read_error);

  if (result == X_SOCKET_ERROR) {
    // Checking for available data for reading failed.
    uint32_t error = 0;

    if (poll_read_error != X_WSA_ERROR::X_WSA_NO_ERROR) {
      error = (uint32_t)poll_read_error;
    } else {
      error = XWSAGetLastError();
    }

    receive_async_data.overlapped->internal = error;

    XELOGE("WSAPollRead failed with error {}", error);

    std::unique_lock lock(receive_completion_mutex_);
    receive_cv_.notify_all();
    return X_SOCKET_ERROR;
  } else if (result == 0) {
    // There's no available data for reading therefore would block.
    receive_async_data.overlapped->internal =
        (uint32_t)X_WSA_ERROR::X_WSAEWOULDBLOCK;

    std::unique_lock lock(receive_completion_mutex_);
    receive_cv_.notify_all();
    return X_SOCKET_ERROR;
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

  if (result == X_SOCKET_ERROR) {
    XELOGI("WSARecvFrom failed with error {}", XWSAGetLastError());

    receive_async_data.overlapped->internal = XWSAGetLastError();
  } else if (result == 0) {
    receive_async_data.overlapped->internal =
        (uint32_t)X_WSA_ERROR::X_WSA_NO_ERROR;
    receive_async_data.overlapped->internal_high = bytes_received;

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

uint32_t WSARecvFrom_flags = 0;
uint32_t WSARecvFrom_bytes_recv = 0;

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
  receive_async_data.flags = &WSARecvFrom_flags;
  receive_async_data.num_bytes_recv = &WSARecvFrom_bytes_recv;
  receive_async_data.from = from_ptr;
  receive_async_data.from_len = *fromlen_ptr;

  if (!overlapped_ptr) {
    XELOGD("{}:: without overlapped_ptr!", __func__);
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
    if (cvars::logging) {
      XELOGI("{} completed immediately", __func__);
    }

    if (num_bytes_recv_ptr) {
      *num_bytes_recv_ptr = *receive_async_data.num_bytes_recv;
    }

    *flags_ptr = *receive_async_data.flags;

    return result;
  }

  X_WSA_ERROR wsa_error =
      (X_WSA_ERROR)receive_async_data.overlapped->internal.get();

  if (!overlapped_ptr && wsa_error == X_WSA_ERROR::X_WSAEWOULDBLOCK) {
    XWSASetLastError(X_WSA_ERROR::X_WSAEWOULDBLOCK);
    return result;
  }

  XWSASetLastError(wsa_error);

  if (overlapped_ptr && wsa_error == X_WSA_ERROR::X_WSAEWOULDBLOCK) {
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

        uint32_t error_code = XWSAGetLastError();

        if (error_code != (uint32_t)X_WSA_ERROR::X_WSAEWOULDBLOCK) {
          XELOGI("{} Async:: failed with error code {}", __func__, error_code);
        }
      }
    }

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_PENDING);
  } else {
    // An error occurred that's not X_WSAEWOULDBLOCK
    XELOGI("{}:: failed!", __func__);

    // Check WSA error is not corrupted!
    if (wsa_error !=
        (X_WSA_ERROR)receive_async_data.overlapped->internal.get()) {
      XELOGI("{}:: Overlapped Corruption!!", __func__);
    }

    if (wsa_error == X_WSA_ERROR::X_WSA_OPERATION_ABORTED) {
      XELOGD("{}:: Operation Aborted!", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSA_OPERATION_ABORTED);
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

  X_WSA_ERROR wsa_error = (X_WSA_ERROR)overlapped_ptr->internal.get();

  switch (wsa_error) {
    case X_WSA_ERROR::X_WSA_OPERATION_ABORTED: {
      XELOGD("{}:: Operation Aborted!", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSA_OPERATION_ABORTED);
      return false;
    } break;
    case X_WSA_ERROR::X_WSAEWOULDBLOCK: {
      XWSASetLastError(X_WSA_ERROR::X_WSA_IO_INCOMPLETE);
      return false;
    } break;
    default:
      break;
  }

  if (cvars::logging) {
    if (overlapped_ptr->offset_high == 1) {
      XELOGI("{}:: WSASendTo bytes sent {} with status {}!", __func__,
             overlapped_ptr->internal_high.get(),
             overlapped_ptr->internal.get());
    } else {
      XELOGI("{}:: WSARecvFrom bytes received {} with status {}!", __func__,
             overlapped_ptr->internal_high.get(),
             overlapped_ptr->internal.get());
    }
  }

  if (static_cast<uint32_t>(wsa_error) == 0) {
    if (overlapped_ptr->internal_high == 0) {
      XELOGI("{}:: 0 bytes sent!", __func__);
      XWSASetLastError(X_WSA_ERROR::X_WSA_IO_INCOMPLETE);
      return false;
    }

    *bytes_transferred = overlapped_ptr->internal_high;
    *flags_ptr = overlapped_ptr->offset;
  } else {
    XELOGI("{}:: failed with error code {}", __func__,
           overlapped_ptr->internal.get());

    XWSASetLastError(X_WSA_ERROR::X_WSA_IO_INCOMPLETE);
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

    overlapped_ptr->internal = (uint32_t)X_WSA_ERROR::X_WSA_OPERATION_ABORTED;
  }

  pending_overlapped_io_.clear();

  XWSASetLastError(X_WSA_ERROR::X_WSA_OPERATION_ABORTED);

  return 0;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len,
              flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                    XSOCKADDR_IN* to, uint32_t to_len) {
  to->address_port =
      XLiveAPI::upnp_handler->GetMappedBindPort(to->address_port);

  sockaddr addr = to->to_host();

  return sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                to ? &addr : nullptr, to_len);
}

int XSocket::WSAEventSelect(uint64_t socket_handle, uint64_t event_handle,
                            uint32_t flags) {
  return ::WSAEventSelect(socket_handle, reinterpret_cast<HANDLE>(event_handle),
                          flags);
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

X_STATUS XSocket::GetPeerName(XSOCKADDR_IN* buf, int* buf_len) {
  sockaddr addr = buf->to_host();
  sockaddr* sa = const_cast<sockaddr*>(&addr);

  int ret = getpeername(native_handle_, sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->to_guest(sa);
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(XSOCKADDR_IN* buf, int* buf_len) {
  sockaddr addr = buf->to_host();
  sockaddr* sa = const_cast<sockaddr*>(&addr);

  int ret = getsockname(native_handle_, sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->to_guest(sa);
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
#ifdef XE_PLATFORM_WIN32
  const uint32_t last_error = WSAGetLastError();

  bool known = XWSAIsKnownError(static_cast<X_WSA_ERROR>(last_error));

  return last_error;
#endif
  return errno;
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