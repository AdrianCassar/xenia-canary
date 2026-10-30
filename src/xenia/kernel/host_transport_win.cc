/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/host_transport.h"

#include <mstcpip.h>
#include <mswsock.h>
#include <chrono>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xsocket.h"

namespace xe {
namespace kernel {

using namespace std::chrono_literals;

constexpr auto kEventWatchInterval = 10ms;

// Winsock's errors are the guest's.
uint32_t HostLastError() { return WSAGetLastError(); }

HostTransport::HostTransport(uint64_t native_handle, bool datagram)
    : native_handle_(native_handle), datagram_(datagram) {}

HostTransport::~HostTransport() { StopEventWatcher(); }

bool HostTransport::Open(int af, int type, int protocol) {
  datagram_ = type == XSocket::X_SOCK_DGRAM;

  native_handle_ = socket(af, type, protocol);
  if (native_handle_ == X_INVALID_SOCKET) {
    ReportHostError(X_SOCKET_ERROR);
    return false;
  }

  // 415607FF fails on WSAECONNRESET after sending to a port with no socket.
  if (datagram_) {
    BOOL report_resets = FALSE;
    DWORD bytes_returned = 0;
    WSAIoctl(static_cast<SOCKET>(native_handle_), SIO_UDP_CONNRESET,
             &report_resets, sizeof(report_resets), nullptr, 0, &bytes_returned,
             nullptr, nullptr);
  }

  return true;
}

int HostTransport::Close() {
  StopEventWatcher();
  return ReportHostError(closesocket(static_cast<SOCKET>(native_handle_)));
}

int HostTransport::GetOption(int level, int optname, void* value,
                             socklen_t* len) {
  return ReportHostError(getsockopt(native_handle_, level, optname,
                                    static_cast<char*>(value), len));
}

int HostTransport::SetOption(int level, int optname, const void* value,
                             socklen_t len) {
  return ReportHostError(setsockopt(native_handle_, level, optname,
                                    static_cast<const char*>(value), len));
}

int HostTransport::IOControl(uint32_t cmd, uint32_t* arg_ptr) {
  const u_long initial_param = xe::load_and_swap<uint32_t>(arg_ptr);
  u_long param = initial_param;

  // The host refuses to make a socket blocking while WSAEventSelect is
  // active, so cancel its association first, and stop signalling the event.
  if (cmd == FIONBIO && !param && event_select_active_) {
    ::WSAEventSelect(native_handle_, nullptr, 0);
    StopEventWatcher();
    event_select_active_ = false;
  }

  int ret = ReportHostError(ioctlsocket(native_handle_, cmd, &param));

  // The host counts every queued datagram. Report only the size of the next
  // one, which is what a single receive returns.
  if (!ret && cmd == FIONREAD && datagram_ && param) {
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

  return ret;
}

// The host signals the event itself, apart from FD_READ for more data that
// arrives before a receive.
int HostTransport::EventSelect(uint64_t native_event_handle, uint32_t flags,
                               std::function<void()> signal) {
  const HANDLE hEvent =
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(native_event_handle));
  const int ret =
      ReportHostError(::WSAEventSelect(native_handle_, hEvent, flags));

  if (ret != X_ERROR_SUCCESS) {
    return ret;
  }

  StopEventWatcher();

  event_select_active_ = flags != 0;
  event_flags_ = flags;
  event_signal_ = std::move(signal);

  if (flags & X_FD_READ) {
    last_queued_bytes_ = QueuedReceiveBytes();
    stop_event_watcher_ = false;

    event_watcher_ = std::thread(&HostTransport::WatchEvents, this);
  }

  return ret;
}

// The host only signals FD_READ again after a receive, so 494707E4 (Race Pro)
// would never be signalled again after resetting its event on a timeout.
void HostTransport::WatchEvents() {
  while (!stop_event_watcher_) {
    const uint64_t queued = QueuedReceiveBytes();

    if (queued > last_queued_bytes_.exchange(queued)) {
      event_signal_();
    }

    std::this_thread::sleep_for(kEventWatchInterval);
  }
}

// FIONREAD counts every queued datagram.
uint64_t HostTransport::QueuedReceiveBytes() const {
  u_long queued = 0;

  if (ioctlsocket(native_handle_, FIONREAD, &queued) != 0) {
    return 0;
  }

  return queued;
}

int HostTransport::Bind(const sockaddr* addr, socklen_t len) {
  int ret = ReportHostError(bind(native_handle_, addr, len));

  // The host reports a port held with SO_EXCLUSIVEADDRUSE as WSAEACCES.
  if (ret < 0 && XSocket::XWSAGetLastError() == X_WSA_ERROR::X_WSAEACCES) {
    XSocket::XWSASetLastError(X_WSA_ERROR::X_WSAEADDRINUSE);
  }

  return ret;
}

int HostTransport::Connect(const sockaddr* addr) {
  // The host retries a refused connect for about 2 seconds, where the console
  // fails straight away. A SYN to this machine can't be lost, so only skip
  // the retries there.
  const auto* addr_in = reinterpret_cast<const sockaddr_in*>(addr);
  if (!datagram_ && (ntohl(addr_in->sin_addr.s_addr) >> 24) == 127) {
    TCP_INITIAL_RTO_PARAMETERS rto = {};
    rto.Rtt = TCP_INITIAL_RTO_UNSPECIFIED_RTT;
    rto.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;

    DWORD bytes_returned = 0;
    WSAIoctl(static_cast<SOCKET>(native_handle_), SIO_TCP_INITIAL_RTO, &rto,
             sizeof(rto), nullptr, 0, &bytes_returned, nullptr, nullptr);
  }

  // The guest's length is ignored, the whole address is always read.
  return ReportHostError(connect(native_handle_, addr, sizeof(sockaddr_in)));
}

int HostTransport::Listen(int backlog) {
  return ReportHostError(listen(native_handle_, backlog));
}

int HostTransport::GetPeerName(sockaddr* addr, socklen_t* len) {
  return ReportHostError(getpeername(native_handle_, addr, len));
}

// The host already reports a timed out receive or send as WSAETIMEDOUT.
void HostTransport::ReportTimeout(int, uint32_t) const {}

int HostTransport::Recv(uint8_t* buf, uint32_t len) {
  return ReportHostError(
      recv(native_handle_, reinterpret_cast<char*>(buf), len, 0));
}

int HostTransport::RecvFrom(uint8_t* buf, uint32_t len, sockaddr* from,
                            socklen_t* from_len) {
  return ReportHostError(recvfrom(native_handle_, reinterpret_cast<char*>(buf),
                                  len, 0, from, from_len));
}

int HostTransport::Send(const uint8_t* buf, uint32_t len) {
  return ReportHostError(
      send(native_handle_, reinterpret_cast<const char*>(buf), len, 0));
}

int HostTransport::SendBuffers(const std::vector<HostBuffer>& buffers,
                               const sockaddr* to, int to_len,
                               uint32_t* bytes_sent) {
  std::vector<WSABUF> host_buffers(buffers.size());

  for (size_t i = 0; i < buffers.size(); i++) {
    host_buffers[i].len = buffers[i].len;
    host_buffers[i].buf = buffers[i].data;
  }

  return ReportHostError(::WSASendTo(native_handle_, host_buffers.data(),
                                     static_cast<DWORD>(host_buffers.size()),
                                     reinterpret_cast<DWORD*>(bytes_sent), 0,
                                     to, to_len, nullptr, nullptr));
}

int HostTransport::ReceiveBuffer(const HostBuffer& buffer, sockaddr* from,
                                 int* from_len, uint32_t* bytes_received,
                                 uint32_t* flags) {
  WSABUF recv_buffer = {};
  recv_buffer.buf = buffer.data;
  recv_buffer.len = buffer.len;

  return ReportHostError(::WSARecvFrom(
      native_handle_, &recv_buffer, 1, reinterpret_cast<DWORD*>(bytes_received),
      reinterpret_cast<DWORD*>(flags), from, from_len, nullptr, nullptr));
}

int HostTransport::Select(bool write, long timeout_us) {
  fd_set fds = {};
  FD_ZERO(&fds);

  SOCKET s = static_cast<SOCKET>(native_handle_);
  FD_SET(s, &fds);

  timeval tv = {};
  tv.tv_usec = timeout_us;

  // Winsock ignores nfds.
  const int ret = ReportHostError(
      select(0, write ? nullptr : &fds, write ? &fds : nullptr, nullptr, &tv));

  if (ret || !write || datagram_) {
    return ret;
  }

  // The host reports a reset connection as readable but never as writable,
  // so a send waiting for room would never finish.
  FD_SET(s, &fds);
  tv = {};

  if (select(0, &fds, nullptr, nullptr, &tv) > 0) {
    char byte = 0;

    if (recv(s, &byte, 1, MSG_PEEK) == SOCKET_ERROR) {
      const int error = WSAGetLastError();

      if (error == WSAECONNRESET || error == WSAECONNABORTED) {
        return 1;
      }
    }
  }

  return 0;
}

int TransportSelect::Wait(uint32_t nfds, timeval* timeout) {
  return ReportHostError(select(nfds, use_read_ ? &read_ : nullptr,
                                use_write_ ? &write_ : nullptr,
                                use_except_ ? &except_ : nullptr, timeout));
}

}  // namespace kernel
}  // namespace xe
