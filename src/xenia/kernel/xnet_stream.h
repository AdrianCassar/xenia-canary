/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_STREAM_H_
#define XENIA_KERNEL_XNET_STREAM_H_

#include <cstdint>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "xenia/kernel/xnet_ice.h"

namespace xe {
namespace kernel {

class XSocket;

// Guest TCP over title ICE via KCP.
class XNetStream {
 public:
  static XNetStream& Instance();

  void Initialize();
  void Shutdown();
  void Tick();

  void OnMuxPacket(const XNetMuxPacket& packet);

  int Connect(XSocket* socket, uint32_t online_ip_nbo, uint16_t port_nbo);
  int Listen(XSocket* socket, uint16_t port_nbo);
  bool TakeAccept(XSocket* listen_socket, uint16_t* out_stream_id,
                  uint32_t* out_ip_nbo, uint16_t* out_port_nbo);
  void AttachSocket(uint16_t stream_id, XSocket* socket);
  int Send(XSocket* socket, const uint8_t* data, size_t len);
  int Recv(XSocket* socket, uint8_t* data, size_t len);
  void Close(XSocket* socket);

  bool HasPendingAccept(XSocket* listen_socket);
  bool HasRecvData(XSocket* socket);

 private:
  enum class CtrlType : uint8_t {
    kOpen = 1,
    kAccept = 2,
    kRst = 3,
    kClose = 4,
  };

  struct Stream {
    uint16_t stream_id = 0;
    uint32_t remote_ip_nbo = 0;
    uint16_t remote_port_nbo = 0;
    uint16_t local_port_nbo = 0;
    XSocket* socket = nullptr;
    void* kcp = nullptr;  // ikcpcb*
    bool connected = false;
    bool listening = false;
    std::queue<std::vector<uint8_t>> recv_queue;
  };

  XNetStream() = default;

  static int KcpOutput(const char* buf, int len, void* kcp, void* user);
  uint16_t AllocStreamIdLocked();
  Stream* FindBySocketLocked(XSocket* socket);
  Stream* FindByIdLocked(uint16_t stream_id);
  void SendCtrlLocked(uint32_t online_ip_nbo, CtrlType type, uint16_t stream_id,
                      uint16_t dest_port_nbo, uint16_t src_port_nbo);
  void HandleCtrl(const XNetMuxPacket& packet);
  void HandleKcp(const XNetMuxPacket& packet);

  std::mutex mutex_;
  bool initialized_ = false;
  uint16_t next_stream_id_ = 1;
  std::unordered_map<uint16_t, Stream> streams_;
  std::unordered_map<XSocket*, uint16_t> socket_to_stream_;
  // listen port_nbo -> listen socket
  std::unordered_map<uint16_t, XSocket*> listeners_;
  // pending accepts per listen socket
  std::unordered_map<XSocket*, std::queue<uint16_t>> accept_queues_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_STREAM_H_
