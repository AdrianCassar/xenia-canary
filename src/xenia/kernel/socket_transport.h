/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SOCKET_TRANSPORT_H_
#define XENIA_KERNEL_SOCKET_TRANSPORT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "xenia/xbox.h"

#ifdef XE_PLATFORM_WIN32
// clang-format off
#include "xenia/base/platform.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace xe {
namespace kernel {

// A buffer of an overlapped send or receive, already translated to host
// memory.
struct HostBuffer {
  char* data;
  uint32_t len;
};

// Carries an XSocket's traffic. XSocket keeps the behaviour the guest sees and
// calls its transport for everything that reaches the network. Every call
// works like its Winsock counterpart: it returns a negative value on failure
// and sets the error with XSocket::XWSASetLastError. Addresses are sockaddr_in
// and options use the host's SOL_ and SO_ values.
class SocketTransport {
 public:
  virtual ~SocketTransport() = default;

  virtual bool Open(int af, int type, int protocol) = 0;
  virtual int Close() = 0;

  virtual int GetOption(int level, int optname, void* value,
                        socklen_t* len) = 0;
  virtual int SetOption(int level, int optname, const void* value,
                        socklen_t len) = 0;
  // cmd is the host's value and *arg_ptr the guest's.
  virtual int IOControl(uint32_t cmd, uint32_t* arg_ptr) = 0;
  // signal sets the guest event. A transport calls it itself when the host
  // can't signal native_event_handle directly.
  virtual int EventSelect(uint64_t native_event_handle, uint32_t flags,
                          std::function<void()> signal) = 0;
  // The guest reset the selected event. Data already queued doesn't signal
  // it again, only data that arrives afterwards.
  virtual void EventReset() = 0;

  virtual int Bind(const sockaddr* addr, socklen_t len) = 0;
  virtual int Connect(const sockaddr* addr) = 0;
  virtual int Listen(int backlog) = 0;
  // Returns nullptr on failure.
  virtual std::unique_ptr<SocketTransport> Accept(sockaddr* addr,
                                                  socklen_t* len) = 0;
  virtual int Shutdown(int how) = 0;

  virtual int GetPeerName(sockaddr* addr, socklen_t* len) = 0;
  virtual int GetSockName(sockaddr* addr, socklen_t* len) = 0;
  // The bound port, in network byte order, or 0. Unlike the other calls it
  // never sets the last error, so XSocket can call it straight after a failed
  // call for an implicit bind without losing that call's error.
  virtual uint16_t GetBoundPort() const = 0;

  virtual int Recv(uint8_t* buf, uint32_t len) = 0;
  virtual int RecvFrom(uint8_t* buf, uint32_t len, sockaddr* from,
                       socklen_t* from_len) = 0;
  virtual int Send(const uint8_t* buf, uint32_t len) = 0;
  virtual int SendTo(const uint8_t* buf, uint32_t len, const sockaddr* to,
                     socklen_t to_len) = 0;

  // Overlapped I/O. Both return 0 or X_SOCKET_ERROR.
  virtual int SendBuffers(const std::vector<HostBuffer>& buffers,
                          const sockaddr* to, int to_len,
                          uint32_t* bytes_sent) = 0;
  virtual int ReceiveBuffer(const HostBuffer& buffer, sockaddr* from,
                            int* from_len, uint32_t* bytes_received,
                            uint32_t* flags) = 0;

  // Waits up to timeout_us for the socket to become readable or writable.
  // Returns 1 if it did, 0 on timeout or a negative value on failure.
  virtual int Select(bool write, long timeout_us) = 0;
};

// Waits on several transports at once, like select. Sockets are added to the
// read, write or except set, then Wait blocks until one is ready.
class TransportSelect {
 public:
  // A set that isn't used is passed to the host as null.
  TransportSelect(bool use_read, bool use_write, bool use_except);

  void AddRead(SocketTransport* transport);
  // Also watches for a refused connect, which the host only reports in the
  // except set.
  void AddWrite(SocketTransport* transport);
  // Lets the socket end the wait by becoming writable, without reporting it.
  void AddWriteWake(SocketTransport* transport);
  void AddExcept(SocketTransport* transport);

  // nfds is the guest's value. Returns the select result.
  int Wait(uint32_t nfds, timeval* timeout);

  bool IsReadable(SocketTransport* transport) const;
  bool IsWritable(SocketTransport* transport) const;
  bool IsExcepted(SocketTransport* transport) const;

 private:
  bool use_read_;
  bool use_write_;
  bool use_except_;

  fd_set read_ = {};
  fd_set write_ = {};
  fd_set except_ = {};
  // Except set sockets the host only reports as errored, not in except_.
  fd_set errored_ = {};
  std::vector<uint64_t> except_handles_;
  int highest_ = -1;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_SOCKET_TRANSPORT_H_
