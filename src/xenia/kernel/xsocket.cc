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

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
// #include "xenia/kernel/xnet.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
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
#include <src/xenia/kernel/xam/xam_net.h>
#include <src/xenia/kernel/XLiveAPI.h>
using namespace xe::kernel::xam;

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

X_STATUS XSocket::Connect(const XSOCKADDR* name, int name_len) {
  sockaddr_storage n_name;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(n_name.ss_family);
  if (name_len > sizeof(n_name) || name_len < family_size) {
    SetLastWSAError(X_WSAError::X_WSAEFAULT);
    return X_STATUS_UNSUCCESSFUL;
  }

  n_name.ss_family = name->address_family;
  std::memcpy(reinterpret_cast<uint8_t*>(&n_name) + family_size, name->sa_data,
              name_len - family_size);

  auto addrin = reinterpret_cast<sockaddr_in*>(&n_name);

  addrin->sin_port = htons(
      XLiveAPI::upnp_handler.get_mapped_connect_port(ntohs(addrin->sin_port)));

  int ret = connect(native_handle_, (const sockaddr*)&n_name, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(const XSOCKADDR* name, int name_len) {
  sockaddr_storage n_name;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(n_name.ss_family);
  if (name_len > sizeof(n_name) || name_len < family_size) {
    SetLastWSAError(X_WSAError::X_WSAEFAULT);
    return X_STATUS_UNSUCCESSFUL;
  }

  n_name.ss_family = name->address_family;
  std::memcpy(reinterpret_cast<uint8_t*>(&n_name) + family_size, name->sa_data,
              name_len - family_size);

  auto addrin = reinterpret_cast<sockaddr_in*>(&n_name);

  addrin->sin_port =
      htons(XLiveAPI::upnp_handler.get_mapped_bind_port(ntohs(addrin->sin_port)));

  int ret = bind(native_handle_, (sockaddr*)&n_name, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_ = true;
  bound_port_ = reinterpret_cast<sockaddr_in*>(&n_name)->sin_port;

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Listen(int backlog) {
  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(XSOCKADDR* name, int* name_len) {
  sockaddr_storage n_sockaddr;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(n_sockaddr.ss_family);
  socklen_t n_name_len = 0;

  if (name_len) {
    n_name_len = *name_len;
    if (n_name_len > sizeof(n_sockaddr) || n_name_len < family_size) {
      SetLastWSAError(X_WSAError::X_WSAEFAULT);
      return nullptr;
    }
  }

  uintptr_t ret = accept(native_handle_,
                         name ? (sockaddr*)&n_sockaddr : nullptr, &n_name_len);
  if (ret == -1) {
    if (name && name_len) {
      std::memset((sockaddr*)name, 0, *name_len);
      *name_len = 0;
    }
    return nullptr;
  }

  if (name) {
    name->address_family = n_sockaddr.ss_family;
    std::memcpy((sockaddr*)name, &n_sockaddr, n_name_len);
  }
  if (name_len) {
    *name_len = n_name_len;
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
                      XSOCKADDR* from, uint32_t* from_len) {
  // Pop from secure packets first
  // TODO(DrChat): Enable when I commit XNet
  /*
  {
    std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
    if (incoming_packets_.size()) {
      packet* pkt = (packet*)incoming_packets_.front();
      int data_len = pkt->data_len;
      std::memcpy(buf, pkt->data, std::min((uint32_t)pkt->data_len, buf_len));

      from->sin_family = 2;
      from->sin_addr = pkt->src_ip;
      from->sin_port = pkt->src_port;

      incoming_packets_.pop();
      uint8_t* pkt_ui8 = (uint8_t*)pkt;
      delete[] pkt_ui8;

      return data_len;
    }
  }
  */

  sockaddr_storage nfrom;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(nfrom.ss_family);
  socklen_t nfromlen = 0;

  if (from_len) {
    nfromlen = *from_len;
    if (nfromlen > sizeof(nfrom) || nfromlen < family_size) {
      SetLastWSAError(X_WSAError::X_WSAEFAULT);
      return -1;
    }
  }

  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len,
                     flags, from ? (sockaddr*)&nfrom : nullptr, &nfromlen);

  if (from) {
    from->address_family = nfrom.ss_family;
    std::memcpy(from->sa_data, reinterpret_cast<uint8_t*>(&nfrom) + family_size,
                nfromlen - family_size);
  }
  if (from_len) {
    *from_len = nfromlen;
  }

  return ret;
}

struct WSARecvFromData {
  XWSABUF* buffers;
  uint32_t num_buffers;
  uint32_t flags;
  XSOCKADDR* from;
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
           receive_async_data.overlapped->internal_high);
    goto threadexit;
  } else if (ret == 0) {
    receive_async_data.overlapped->internal_high =
        (uint32_t)X_WSAError::X_WSAEWOULDBLOCK;
    ret = -1;
    goto threadexit;
  }

  sockaddr_storage n_from;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(n_from.ss_family);
  socklen_t n_from_len = 0;

  if (receive_async_data.from_len) {
    n_from_len = *receive_async_data.from_len;
    if (n_from_len > sizeof(n_from) || n_from_len < family_size) {
      receive_async_data.overlapped->internal_high =
          (uint32_t)X_WSAError::X_WSAEFAULT;
      ret = -1;
      goto threadexit;
    }
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
    ret = ::WSARecvFrom(native_handle_, buffers, receive_async_data.num_buffers,
                        &bytes_received, &flags,
                        receive_async_data.from ? (sockaddr*)&n_from : nullptr,
                        &n_from_len, nullptr, nullptr);
    if (ret < 0) {
      receive_async_data.overlapped->internal_high = GetLastWSAError();
    } else {
      receive_async_data.overlapped->internal = bytes_received;
    }
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

  if (receive_async_data.from) {
    receive_async_data.from->address_family = n_from.ss_family;
    std::memcpy(receive_async_data.from->sa_data,
                reinterpret_cast<uint8_t*>(&n_from) + family_size,
                n_from_len - family_size);
  }
  if (receive_async_data.from_len) {
    *receive_async_data.from_len = n_from_len;
  }

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
                         xe::be<uint32_t>* flags_ptr, XSOCKADDR* from_ptr,
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
        receive_thread_ = std::thread(&XSocket::PollWSARecvFrom, this, true,
                                      receive_async_data);
        receive_thread_.detach();
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
                    XSOCKADDR* to, uint32_t to_len) {
  // Send 2 copies of the packet: One to XNet (for network security) and an
  // unencrypted copy for other Xenia hosts.
  // TODO(DrChat): Enable when I commit XNet.
  /*
  auto xam = kernel_state()->GetKernelModule<xam::XamModule>("xam.xex");
  auto xnet = xam->xnet();
  if (xnet) {
    xnet->SendPacket(this, to, buf, buf_len);
  }
  */

  sockaddr_storage nto;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(nto.ss_family);
  if (to) {
    if (to_len > sizeof(nto) || to_len < family_size) {
      SetLastWSAError(X_WSAError::X_WSAEFAULT);
      return -1;
    }

    nto.ss_family = to->address_family;
    std::memcpy(reinterpret_cast<uint8_t*>(&nto) + family_size, to->sa_data,
                to_len - family_size);
  }

  auto addrin = reinterpret_cast<sockaddr_in*>(&nto);
  addrin->sin_port =
      htons(XLiveAPI::upnp_handler.get_mapped_bind_port(ntohs(addrin->sin_port)));

  return sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                to ? (const sockaddr*)&nto : nullptr, to_len);
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

X_STATUS XSocket::GetPeerName(XSOCKADDR* buf, int* buf_len) {
  sockaddr_storage sa;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(sa.ss_family);
  if (*buf_len > sizeof(sa) || *buf_len < family_size) {
    SetLastWSAError(X_WSAError::X_WSAEFAULT);
    return X_STATUS_UNSUCCESSFUL;
  }

  int ret = getpeername(native_handle_, (sockaddr*)&sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->address_family = sa.ss_family;
  std::memcpy(buf->sa_data, reinterpret_cast<uint8_t*>(&sa) + family_size,
              *buf_len - family_size);
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(XSOCKADDR* buf, int* buf_len) {
  sockaddr_storage sa;
  auto family_size =
      offsetof(sockaddr_storage, ss_family) + sizeof(sa.ss_family);
  if (*buf_len > sizeof(sa) || *buf_len < family_size) {
    SetLastWSAError(X_WSAError::X_WSAEFAULT);
    return X_STATUS_UNSUCCESSFUL;
  }

  int ret = getsockname(native_handle_, (sockaddr*)&sa, (socklen_t*)buf_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  buf->address_family = sa.ss_family;
  std::memcpy(buf->sa_data, reinterpret_cast<uint8_t*>(&sa) + family_size,
              *buf_len - family_size);
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