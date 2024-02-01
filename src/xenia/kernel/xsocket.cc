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
// #include "xenia/kernel/xnet.h"

#include <xenia/kernel/XLiveAPI.h>

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
  if (native_handle_ == -1) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Close() {
  std::unique_lock lock(receive_mutex_);
  if (active_overlapped_ && !(active_overlapped_->offset_high & 1)) {
    active_overlapped_->offset_high |= 2;
  }
  lock.unlock();

  std::unique_lock socket_lock(receive_socket_mutex_);
#if XE_PLATFORM_WIN32
  int ret = closesocket(native_handle_);
#elif XE_PLATFORM_LINUX
  int ret = close(native_handle_);
#endif
  socket_lock.unlock();

  if (ret != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                            int* optlen) {
  int ret =
      getsockopt(native_handle_, level, optname, (char*)optval_ptr, optlen);
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

  int ret =
      setsockopt(native_handle_, level, optname, (char*)optval_ptr, optlen);
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
      XLiveAPI::upnp_handler->get_mapped_connect_port(name->address_port);

  int ret = connect(native_handle_, &sa_in.to_host(), name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(const XSOCKADDR_IN* name, int name_len) {
  XSOCKADDR_IN sa_in = XSOCKADDR_IN();
  memcpy(&sa_in, name, sizeof(XSOCKADDR_IN));

  sa_in.address_port =
      XLiveAPI::upnp_handler->get_mapped_connect_port(name->address_port);

  int ret = bind(native_handle_, &sa_in.to_host(), name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_ = true;
  bound_port_ = name->address_port;

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
  if (ret == -1) {
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
  sockaddr sa = from->to_host();

  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len,
                     flags, from ? &sa : nullptr, (int*)from_len);

  from->to_guest(&sa);

  return ret;
}

struct WSARecvFromData {
  XWSABUF* buffers;
  uint32_t num_buffers;
  uint32_t flags;
  XSOCKADDR_IN* from;
  xe::be<uint32_t>* from_len;
  XWSAOVERLAPPED* overlapped;
};

int XSocket::PollWSARecvFrom(bool wait, WSARecvFromData receive_async_data) {
  receive_async_data.overlapped->internal_high = 0;

  struct pollfd fds[1];
  fds->fd = native_handle_;
  fds->events = POLLIN;

  int ret;
  do {
#ifdef XE_PLATFORM_WIN32
    ret = WSAPoll(fds, 1, wait ? 1000 : 0);
#else
    ret = poll(fds, 1, wait ? 1000 : 0);
#endif

    if (receive_async_data.overlapped->offset_high & 2) {
      receive_async_data.overlapped->internal_high =
          (uint32_t)X_WSAError::X_WSA_OPERATION_ABORTED;
      ret = -1;
      goto threadexit;
    }
  } while (ret == 0 && wait);

  if (ret < 0) {
    receive_async_data.overlapped->internal_high = WSAGetLastError();
    XELOGE("XSocket receive thread failed polling with error {}",
           static_cast<uint32_t>(receive_async_data.overlapped->internal_high));
    goto threadexit;
  } else if (ret == 0) {
    receive_async_data.overlapped->internal_high =
        (uint32_t)X_WSAError::X_WSAEWOULDBLOCK;
    ret = -1;
    goto threadexit;
  }

#ifdef XE_PLATFORM_WIN32
  DWORD bytes_received = 0;
  DWORD flags = receive_async_data.flags;

  auto buffers = new WSABUF[receive_async_data.num_buffers];
  for (auto i = 0u; i < receive_async_data.num_buffers; i++) {
    buffers[i].len = receive_async_data.buffers[i].len;
    buffers[i].buf =
        reinterpret_cast<CHAR*>(kernel_state()->memory()->TranslateVirtual(
            receive_async_data.buffers[i].buf_ptr));
  }

  {
    std::unique_lock socket_lock(receive_socket_mutex_);

    sockaddr* sa = nullptr;
    if (receive_async_data.from) {
      sa = const_cast<sockaddr*>(&receive_async_data.from->to_host());
    }

    ret = ::WSARecvFrom(native_handle_, buffers, receive_async_data.num_buffers,
                        &bytes_received, &flags, sa,
                        (LPINT)receive_async_data.from_len, nullptr, nullptr);
    if (ret < 0) {
      receive_async_data.overlapped->internal_high = GetLastWSAError();
    } else {
      receive_async_data.overlapped->internal = bytes_received;
    }
    receive_async_data.from->to_guest(sa);
    socket_lock.unlock();
  }

  receive_async_data.overlapped->offset = flags;
#else
  auto buffers = new iovec[receive_async_data.num_buffers];
  for (auto i = 0u; i < receive_async_data.num_buffers; i++) {
    buffers[i].iov_len = receive_async_data.buffers[i].len;
    buffers[i].iov_base = kernel_state()->memory()->TranslateVirtual(
        receive_async_data.buffers[i].buf_ptr);
  }

  msghdr msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_name = &n_from;
  msg.msg_namelen = n_from_len;
  msg.msg_iov = buffers;
  msg.msg_iovlen = receive_async_data.num_buffers;

  {
    std::unique_lock socket_lock(receive_socket_mutex_);
    ret = recvmsg(native_handle_, &msg, receive_async_data.flags);
    if (ret < 0) {
      receive_async_data.overlapped->internal_high = GetLastWSAError();
    } else {
      receive_async_data.overlapped->internal = ret;
    }
    socket_lock.unlock();
  }

  flags = 0;
  if (msg.msg_flags & MSG_TRUNC) flags |= MSG_PARTIAL;
  if (msg.msg_flags & MSG_OOB) flags |= MSG_OOB;
  receive_async_data.overlapped->offset = flags;

  if (ret >= 0) {
    SetLastWSAError((X_WSAError)0);
    ret = 0;
  }
#endif
  delete[] buffers;

threadexit:
  std::unique_lock lock(receive_mutex_);
  if (wait) {
    delete[] receive_async_data.buffers;
  }

  receive_async_data.overlapped->offset_high |= 1;

  if (wait && receive_async_data.overlapped->event_handle) {
    xboxkrnl::xeNtSetEvent(receive_async_data.overlapped->event_handle,
                           nullptr);
  }

  receive_cv_.notify_all();
  lock.unlock();

  return ret;
}

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
  // make the code paths the same to improve symmetry in behaviour.

  WSARecvFromData receive_async_data;
  receive_async_data.buffers = buffers;
  receive_async_data.num_buffers = num_buffers;
  receive_async_data.flags = *flags_ptr;
  receive_async_data.from = from_ptr;
  receive_async_data.from_len = fromlen_ptr;

  XWSAOVERLAPPED tmp_overlapped;
  std::memset(&tmp_overlapped, 0, sizeof(tmp_overlapped));
  receive_async_data.overlapped =
      overlapped_ptr ? overlapped_ptr : &tmp_overlapped;

  int ret = PollWSARecvFrom(false, receive_async_data);

  if (ret < 0) {
    auto wsa_error = receive_async_data.overlapped->internal_high.get();
    SetLastWSAError((X_WSAError)wsa_error);

    if (overlapped_ptr && wsa_error == (uint32_t)X_WSAError::X_WSAEWOULDBLOCK) {
      receive_mutex_.lock();

      if (!active_overlapped_ || active_overlapped_->offset_high & 1) {
        // These may have been on the stack - copy them.
        receive_async_data.buffers = new XWSABUF[num_buffers];
        std::memcpy(receive_async_data.buffers, buffers,
                    num_buffers * sizeof(XWSABUF));

        overlapped_ptr->offset_high = 0;
        if (overlapped_ptr->event_handle) {
          xboxkrnl::xeNtClearEvent(overlapped_ptr->event_handle);
        }
        active_overlapped_ = overlapped_ptr;

        if (!polling_task_.valid()) {
          polling_task_ =
              std::async(std::launch::async, &XSocket::PollWSARecvFrom, this,
                         true, receive_async_data);
        } else {
          auto status = polling_task_.wait_for(0ms);
          if (status == std::future_status::ready) {
            auto result = polling_task_.get();
          }
        }
        SetLastWSAError(X_WSAError::X_WSA_IO_PENDING);
      }

      receive_mutex_.unlock();
    }
  } else {
    if (num_bytes_recv_ptr) {
      *num_bytes_recv_ptr = receive_async_data.overlapped->internal;
    }
    *flags_ptr = receive_async_data.overlapped->offset;
  }

  return ret;
}

bool XSocket::WSAGetOverlappedResult(XWSAOVERLAPPED* overlapped_ptr,
                                     xe::be<uint32_t>* bytes_transferred,
                                     bool wait, xe::be<uint32_t>* flags_ptr) {
  if (!overlapped_ptr || !bytes_transferred || !flags_ptr) {
    SetLastWSAError(X_WSAError::X_WSA_INVALID_PARAMETER);
    return false;
  }

  std::unique_lock lock(receive_mutex_);
  if (!(overlapped_ptr->offset_high & 1)) {
    if (wait) {
      receive_cv_.wait(lock);
    } else {
      SetLastWSAError(X_WSAError::X_WSA_IO_INCOMPLETE);
      return false;
    }
  }

  if (overlapped_ptr->internal_high != 0) {
    SetLastWSAError((X_WSAError)overlapped_ptr->internal_high.get());
    active_overlapped_ = nullptr;
    return false;
  }

  *bytes_transferred = overlapped_ptr->internal;
  *flags_ptr = overlapped_ptr->offset;

  active_overlapped_ = nullptr;

  return true;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len,
              flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags,
                    XSOCKADDR_IN* to, uint32_t to_len) {
  return sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                to ? &to->to_host() : nullptr, to_len);
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
  sockaddr* sa = const_cast<sockaddr*>(&buf->to_host());

  int ret = getpeername(native_handle_, sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->to_guest(sa);
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(XSOCKADDR_IN* buf, int* buf_len) {
  sockaddr* sa = const_cast<sockaddr*>(&buf->to_host());

  int ret = getsockname(native_handle_, sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->to_guest(sa);
  return X_STATUS_SUCCESS;
}

uint32_t XSocket::GetLastWSAError() const {
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