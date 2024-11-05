/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_PLAYER_OBJECT_JSON_H_
#define XENIA_KERNEL_PLAYER_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
class PlayerObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  PlayerObjectJSON();
  virtual ~PlayerObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const xe::be<uint64_t>& XUID() const { return xuid_; }
  void XUID(const xe::be<uint64_t>& xuid) { xuid_ = xuid; }

  const xe::be<uint64_t>& MachineID() const { return machineId_; }
  void MachineID(const xe::be<uint64_t>& machineId) { machineId_ = machineId; }

  const xe::be<uint64_t>& MacAddress() const { return macAddress_; }
  void MacAddress(const xe::be<uint64_t>& macAddress) {
    macAddress_ = macAddress;
  }

  const std::string& HostAddress() const { return hostAddress_; }
  void HostAddress(const std::string& hostAddress) {
    hostAddress_ = hostAddress;
  }

  const std::string& Gamertag() const { return gamertag_; }
  void Gamertag(const std::string& gamertag) { gamertag_ = gamertag; }

  const xe::be<uint64_t>& SessionID() const { return sessionId_; }
  void SessionID(const xe::be<uint64_t>& sessionId) { sessionId_ = sessionId; }

  const uint16_t& Port() const { return port_; }
  void Port(const uint16_t& port) { port_ = port; }

 private:
  xe::be<uint64_t> xuid_;
  std::string hostAddress_;
  std::string gamertag_;
  xe::be<uint64_t> machineId_;
  xe::be<uint64_t> macAddress_;  // 6 Bytes
  xe::be<uint64_t> sessionId_;
  uint16_t port_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_PLAYER_OBJECT_JSON_H_