/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/host_transport.h"

#include <fcntl.h>
#include <linux/sock_diag.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xsocket.h"

namespace xe {
namespace kernel {

using namespace std::chrono_literals;

constexpr auto kEventWatchInterval = 10ms;

uint32_t HostLastError() {
  uint32_t last_error = errno;

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

  return last_error;
}

HostTransport::HostTransport(uint64_t native_handle, bool datagram)
    : native_handle_(native_handle), datagram_(datagram) {
  RememberPeer();
  connected_ = true;
}

HostTransport::~HostTransport() { StopEventWatcher(); }

bool HostTransport::Open(int af, int type, int protocol) {
  datagram_ = type == XSocket::X_SOCK_DGRAM;

  native_handle_ = socket(af, type, protocol);
  if (native_handle_ == X_INVALID_SOCKET) {
    ReportHostError(X_SOCKET_ERROR);
    return false;
  }

  return true;
}

int HostTransport::Close() {
  StopEventWatcher();
  return ReportHostError(close(static_cast<int>(native_handle_)));
}

int HostTransport::GetOption(int level, int optname, void* value,
                             socklen_t* len) {
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

  if (level == SOL_SOCKET && optname == ~SO_LINGER &&
      *len >= sizeof(uint32_t)) {
    linger native_linger = {};
    socklen_t linger_len = sizeof(native_linger);

    if (getsockopt(native_handle_, SOL_SOCKET, SO_LINGER, &native_linger,
                   &linger_len) < 0) {
      return ReportHostError(X_SOCKET_ERROR);
    }

    const uint32_t dont_linger = !native_linger.l_onoff;
    std::memcpy(value, &dont_linger, sizeof(dont_linger));
    *len = sizeof(dont_linger);
    return 0;
  }

  if (level == SOL_SOCKET && optname == ~SO_REUSEADDR &&
      *len >= sizeof(uint32_t)) {
    const uint32_t exclusive = exclusive_address_use_;
    std::memcpy(value, &exclusive, sizeof(exclusive));
    *len = sizeof(exclusive);
    return 0;
  }

  if (level == SOL_SOCKET && (optname == SO_RCVBUF || optname == SO_SNDBUF) &&
      *len >= sizeof(uint32_t)) {
    int size = 0;
    socklen_t size_len = sizeof(size);

    if (getsockopt(native_handle_, level, optname, &size, &size_len) < 0) {
      return ReportHostError(X_SOCKET_ERROR);
    }

    const uint32_t set_size = static_cast<uint32_t>(size) / 2;
    std::memcpy(value, &set_size, sizeof(set_size));
    *len = sizeof(set_size);
    return 0;
  }

  return ReportHostError(
      getsockopt(native_handle_, level, optname, value, len));
}

int HostTransport::SetOption(int level, int optname, const void* value,
                             socklen_t len) {
  // Linux takes a timeval instead of milliseconds.
  if (level == SOL_SOCKET &&
      (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
      len == sizeof(uint32_t)) {
    uint32_t timeout_ms = 0;
    std::memcpy(&timeout_ms, value, sizeof(timeout_ms));

    timeval timeout = {};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int ret = ReportHostError(
        setsockopt(native_handle_, level, optname, &timeout, sizeof(timeout)));
    if (!ret) {
      (optname == SO_RCVTIMEO ? receive_timeout_ms_ : send_timeout_ms_) =
          timeout_ms;
    }
    return ret;
  }

  // SO_DONTLINGER turns SO_LINGER off, keeping its timeout.
  if (level == SOL_SOCKET && optname == ~SO_LINGER && len >= sizeof(uint32_t)) {
    uint32_t dont_linger = 0;
    std::memcpy(&dont_linger, value, sizeof(dont_linger));

    linger native_linger = {};
    socklen_t linger_len = sizeof(native_linger);

    if (getsockopt(native_handle_, SOL_SOCKET, SO_LINGER, &native_linger,
                   &linger_len) < 0) {
      return ReportHostError(X_SOCKET_ERROR);
    }

    native_linger.l_onoff = !dont_linger;

    return ReportHostError(setsockopt(native_handle_, SOL_SOCKET, SO_LINGER,
                                      &native_linger, sizeof(native_linger)));
  }

  // Linux never shares a port unless both sockets set SO_REUSEADDR, so
  // SO_EXCLUSIVEADDRUSE only needs reporting back.
  if (level == SOL_SOCKET && optname == ~SO_REUSEADDR &&
      len >= sizeof(uint32_t)) {
    uint32_t exclusive = 0;
    std::memcpy(&exclusive, value, sizeof(exclusive));

    exclusive_address_use_ = exclusive;
    return 0;
  }

  return ReportHostError(
      setsockopt(native_handle_, level, optname, value, len));
}

int HostTransport::IOControl(uint32_t cmd, uint32_t* arg_ptr) {
  int param = static_cast<int>(xe::load_and_swap<uint32_t>(arg_ptr));

  int ret = ReportHostError(ioctl(native_handle_, cmd, &param));

  if (ret < 0) {
    return ret;
  }

  xe::store_and_swap<uint32_t>(arg_ptr, static_cast<uint32_t>(param));

  return ret;
}

int HostTransport::EventSelect(uint64_t, uint32_t flags,
                               std::function<void()> signal) {
  int non_blocking = 1;

  if (ioctl(native_handle_, FIONBIO, &non_blocking) < 0) {
    return ReportHostError(X_SOCKET_ERROR);
  }

  StopEventWatcher();

  event_select_active_ = flags != 0;
  event_flags_ = flags;
  event_signal_ = std::move(signal);

  if (flags) {
    read_armed_ = true;
    write_armed_ = true;
    close_armed_ = true;
    last_queued_bytes_ = 0;
    stop_event_watcher_ = false;

    event_watcher_ = std::thread(&HostTransport::WatchEvents, this);
  }

  return X_ERROR_SUCCESS;
}

void HostTransport::WatchEvents() {
  while (!stop_event_watcher_) {
    pollfd poll_fd = {};
    poll_fd.fd = static_cast<int>(native_handle_);

    // Only wait for what's selected, and for a close or room to write only
    // until it's signalled. Both stay set, so poll would never block.
    if (event_flags_ & (X_FD_READ | X_FD_ACCEPT)) {
      poll_fd.events |= POLLIN;
    }

    if ((event_flags_ & X_FD_CLOSE) && close_armed_) {
      poll_fd.events |= POLLRDHUP;
    }

    if (connect_pending_ || ((event_flags_ & X_FD_WRITE) && write_armed_ &&
                             (datagram_ || connected_))) {
      poll_fd.events |= POLLOUT;
    }

    const int ready =
        poll(&poll_fd, 1, static_cast<int>(kEventWatchInterval.count()));
    const short revents = ready > 0 ? poll_fd.revents : 0;

    bool signal = false;

    // Data to read or a connection to accept.
    if (event_flags_ & (X_FD_READ | X_FD_ACCEPT)) {
      if (!(revents & POLLIN)) {
        read_armed_ = true;
      } else if (read_armed_.exchange(false)) {
        signal = true;
      }
    }

    // More data arrived, even if nothing was received since the last signal.
    if (event_flags_ & X_FD_READ) {
      const uint64_t queued = QueuedReceiveBytes();
      if (queued > last_queued_bytes_.exchange(queued)) {
        signal = true;
      }
    }

    // A non-blocking connect finished, whether it succeeded or not.
    if (connect_pending_ && (revents & (POLLOUT | POLLERR | POLLHUP))) {
      connect_pending_ = false;

      sockaddr_storage peer = {};
      socklen_t peer_len = sizeof(peer);
      connected_ =
          getpeername(native_handle_, reinterpret_cast<sockaddr*>(&peer),
                      &peer_len) == 0;

      if (event_flags_ & X_FD_CONNECT) {
        signal = true;
      }
    }

    // An unconnected stream socket also reports POLLOUT.
    if ((event_flags_ & X_FD_WRITE) && (revents & POLLOUT) &&
        (datagram_ || connected_) && write_armed_.exchange(false)) {
      signal = true;
    }

    // The peer shut down sending or reset the connection.
    if ((event_flags_ & X_FD_CLOSE) && !datagram_ && connected_ &&
        (revents & (POLLRDHUP | POLLHUP | POLLERR)) &&
        close_armed_.exchange(false)) {
      signal = true;
    }

    if (signal) {
      event_signal_();
    }

    // Something is still ready, so wait before looking again.
    if (ready > 0) {
      std::this_thread::sleep_for(kEventWatchInterval);
    }
  }
}

// Linux only reports the next datagram's size through FIONREAD, so use the
// memory the receive queue holds.
uint64_t HostTransport::QueuedReceiveBytes() const {
#ifdef SO_MEMINFO
  uint32_t meminfo[SK_MEMINFO_VARS] = {};
  socklen_t len = sizeof(meminfo);

  if (getsockopt(native_handle_, SOL_SOCKET, SO_MEMINFO, meminfo, &len) == 0) {
    return meminfo[SK_MEMINFO_RMEM_ALLOC];
  }
#endif

  return 0;
}

int HostTransport::Bind(const sockaddr* addr, socklen_t len) {
  return ReportHostError(bind(native_handle_, addr, len));
}

int HostTransport::Connect(const sockaddr* addr) {
  // The guest's length is ignored, the whole address is always read.
  const int ret =
      ReportHostError(connect(native_handle_, addr, sizeof(sockaddr_in)));

  if (!ret) {
    RememberPeer();
    connected_ = true;
  }

  // The event watcher reports when a selected or failed connect finishes.
  connect_pending_ = ret < 0 || (event_flags_ & X_FD_CONNECT);

  return ret;
}

int HostTransport::Listen(int backlog) {
  int ret = ReportHostError(listen(native_handle_, backlog));

  // Linux reports listen on a connected socket as EINVAL.
  if (ret < 0 && errno == EINVAL) {
    sockaddr_storage peer = {};
    socklen_t peer_len = sizeof(peer);

    if (getpeername(native_handle_, reinterpret_cast<sockaddr*>(&peer),
                    &peer_len) == 0) {
      XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAEISCONN);
    }
  }

  return ret;
}

int HostTransport::GetPeerName(sockaddr* addr, socklen_t* len) {
  const int ret = getpeername(native_handle_, addr, len);

  // A non-blocking connect only shows its peer once it completes.
  if (!ret) {
    peer_len_ = std::min<socklen_t>(*len, sizeof(peer_));
    std::memcpy(&peer_, addr, peer_len_);
  }

  // Linux fails with ENOTCONN once the connection is reset, but the peer is
  // still reported, and the last error is left alone.
  if (ret < 0 && errno == ENOTCONN && peer_len_) {
    std::memcpy(addr, &peer_, std::min(*len, peer_len_));
    *len = peer_len_;
    return 0;
  }

  return ReportHostError(ret);
}

void HostTransport::ReportTimeout(int ret, uint32_t timeout_ms) const {
  // Linux reports a timed out blocking receive or send as EAGAIN, the same
  // error as a non-blocking call that can't complete.
  if (ret < 0 && errno == EAGAIN && timeout_ms &&
      !(fcntl(native_handle_, F_GETFL) & O_NONBLOCK)) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAETIMEDOUT);
  }
}

int HostTransport::Recv(uint8_t* buf, uint32_t len) {
  read_armed_ = true;

  const int ret = ReportHostError(
      recv(native_handle_, buf, len, datagram_ ? MSG_TRUNC : 0));

  ReportTimeout(ret, receive_timeout_ms_);

  if (ret > static_cast<int>(len)) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    return X_SOCKET_ERROR;
  }

  return ret;
}

int HostTransport::RecvFrom(uint8_t* buf, uint32_t len, sockaddr* from,
                            socklen_t* from_len) {
  read_armed_ = true;

  const int ret = ReportHostError(recvfrom(
      native_handle_, buf, len, datagram_ ? MSG_TRUNC : 0, from, from_len));

  ReportTimeout(ret, receive_timeout_ms_);

  if (ret > static_cast<int>(len)) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    return X_SOCKET_ERROR;
  }

  return ret;
}

int HostTransport::Send(const uint8_t* buf, uint32_t len) {
  const int ret = ReportHostError(send(native_handle_, buf, len, 0));

  if (ret < 0) {
    write_armed_ = true;
  }

  ReportTimeout(ret, send_timeout_ms_);

  if (ret < 0 && errno == EPIPE && !shutdown_send_) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAENOTCONN);
  }

  return ret;
}

int HostTransport::SendBuffers(const std::vector<HostBuffer>& buffers,
                               const sockaddr* to, int to_len,
                               uint32_t* bytes_sent) {
  std::vector<iovec> host_buffers(buffers.size());

  for (size_t i = 0; i < buffers.size(); i++) {
    host_buffers[i].iov_len = buffers[i].len;
    host_buffers[i].iov_base = buffers[i].data;
  }

  msghdr message = {};

  message.msg_name = const_cast<sockaddr*>(to);
  message.msg_namelen = to_len;

  // Set scatter-gather buffers (Equivalent to WSABUF array)
  message.msg_iov = host_buffers.data();
  message.msg_iovlen = host_buffers.size();

  ssize_t sent_bytes = sendmsg(native_handle_, &message, 0);

  if (sent_bytes < 0) {
    write_armed_ = true;
    return ReportHostError(X_SOCKET_ERROR);
  }

  *bytes_sent = static_cast<uint32_t>(sent_bytes);
  return X_ERROR_SUCCESS;
}

int HostTransport::ReceiveBuffer(const HostBuffer& buffer, sockaddr* from,
                                 int* from_len, uint32_t* bytes_received,
                                 uint32_t* flags) {
  read_armed_ = true;

  iovec recv_buffer = {};
  recv_buffer.iov_base = buffer.data;
  recv_buffer.iov_len = buffer.len;

  msghdr message = {};

  message.msg_name = from;
  message.msg_namelen = from_len ? *from_len : 0;

  message.msg_iov = &recv_buffer;
  message.msg_iovlen = 1;

  ssize_t received_bytes = recvmsg(native_handle_, &message, *flags);

  if (received_bytes >= 0 && (message.msg_flags & MSG_TRUNC)) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAEMSGSIZE);
    return X_SOCKET_ERROR;
  }

  if (received_bytes < 0) {
    return ReportHostError(X_SOCKET_ERROR);
  }

  *bytes_received = static_cast<uint32_t>(received_bytes);
  return X_ERROR_SUCCESS;
}

int HostTransport::Select(bool write, long timeout_us) {
  fd_set fds = {};
  FD_ZERO(&fds);

  int s = static_cast<int>(native_handle_);
  FD_SET(s, &fds);

  timeval tv = {};
  tv.tv_usec = timeout_us;

  return ReportHostError(select(s + 1, write ? nullptr : &fds,
                                write ? &fds : nullptr, nullptr, &tv));
}

// Linux only checks descriptors below nfds. Winsock ignores it, so titles
// usually pass 0.
int TransportSelect::Wait(uint32_t, timeval* timeout) {
  // The host reports a refused connect as POLLERR rather than in the except
  // set.
  const auto mark_errored = [this] {
    bool errored = false;

    for (const uint64_t handle : except_handles_) {
      pollfd poll_fd = {};
      poll_fd.fd = static_cast<int>(handle);

      if (poll(&poll_fd, 1, 0) > 0 && (poll_fd.revents & POLLERR)) {
        FD_SET(poll_fd.fd, &errored_);
        errored = true;
      }
    }

    return errored;
  };

  // select doesn't end the wait for an error in the except set, so don't wait
  // when one is already there.
  const bool errored = mark_errored();
  timeval no_wait = {};

  int ret = ReportHostError(select(highest_ + 1, use_read_ ? &read_ : nullptr,
                                   use_write_ ? &write_ : nullptr,
                                   use_except_ ? &except_ : nullptr,
                                   errored ? &no_wait : timeout));

  if (ret == X_SOCKET_ERROR) {
    return ret;
  }

  if (!errored) {
    mark_errored();
  }

  // Count the errored sockets the host left out of the except set.
  for (const uint64_t handle : except_handles_) {
    if (FD_ISSET(handle, &errored_) && !FD_ISSET(handle, &except_)) {
      ret++;
    }
  }

  return ret;
}

}  // namespace kernel
}  // namespace xe
