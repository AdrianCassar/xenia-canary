/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <vector>

#include "xenia/base/string_util.h"
#include "xenia/kernel/base_object_json.h"
#include "xenia/kernel/player_object_json.h"

namespace xe {
namespace kernel {
class SessionObjectJSON : public BaseObjectJSON {
 public:
  SessionObjectJSON();
  virtual ~SessionObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::string& SessionID() const { return sessionid_; }
  void SessionID(const std::string& sessionid) { sessionid_ = sessionid; }

  const uint64_t SessionID_UInt() const {
    return string_util::from_string<uint64_t>(sessionid_, true);
  }

  const std::string& Title() const { return title_; }
  void Title(const std::string& title) { title_ = title; }

  const std::string& MediaID() const { return mediaId_; }
  void MediaID(const std::string& mediaId) { mediaId_ = mediaId; }

  const std::string& Version() const { return version_; }
  void Version(const std::string& version) { version_ = version; }

  const xe::be<uint32_t>& Flags() const { return flags_; }
  void Flags(const xe::be<uint32_t>& flags) { flags_ = flags; }

  const xe::be<uint32_t>& PublicSlotsCount() const { return publicSlotsCount_; }
  void PublicSlotsCount(const xe::be<uint32_t>& slots_count) {
    publicSlotsCount_ = slots_count;
  }

  const xe::be<uint32_t>& PrivateSlotsCount() const {
    return privateSlotsCount_;
  }
  void PrivateSlotsCount(const xe::be<uint32_t>& slots_count) {
    privateSlotsCount_ = slots_count;
  }

  const uint32_t& UserIndex() const { return userIndex_; }
  void UserIndex(const uint32_t& user_index) { userIndex_ = user_index; }

  const std::string& HostAddress() const { return hostAddress_; }
  void HostAddress(const std::string& host_address) {
    hostAddress_ = host_address;
  }

  const std::string& MacAddress() const { return macAddress_; }
  void MacAddress(const std::string& mac_address) { macAddress_ = mac_address; }

  const xe::be<uint16_t>& Port() const { return port_; }
  void Port(const xe::be<uint16_t>& port) { port_ = port; }

  // GetDetails
  const xe::be<uint32_t>& OpenPublicSlotsCount() const {
    return openPublicSlotsCount_;
  }
  void OpenPublicSlotsCount(const xe::be<uint32_t>& slots_count) {
    openPublicSlotsCount_ = slots_count;
  }

  const xe::be<uint32_t>& OpenPrivateSlotsCount() const {
    return privateSlotsCount_;
  }
  void OpenPrivateSlotsCount(const xe::be<uint32_t>& slots_count) {
    privateSlotsCount_ = slots_count;
  }

  const xe::be<uint32_t>& FilledPublicSlotsCount() const {
    return filledPublicSlotsCount_;
  }
  void FilledPublicSlotsCount(const xe::be<uint32_t>& slots_count) {
    filledPublicSlotsCount_ = slots_count;
  }

  const xe::be<uint32_t>& FilledPrivateSlotsCount() const {
    return filledPrivateSlotsCount_;
  }
  void FilledPrivateSlotsCount(const xe::be<uint32_t>& slots_count) {
    filledPrivateSlotsCount_ = slots_count;
  }

  const std::vector<PlayerObjectJSON>& Players() const { return players_; }
  void Players(const std::vector<PlayerObjectJSON>& players) {
    players_ = players;
  }

 private:
  std::string sessionid_;
  std::string title_;
  std::string mediaId_;
  std::string version_;
  xe::be<uint32_t> flags_;
  xe::be<uint32_t> publicSlotsCount_;
  xe::be<uint32_t> privateSlotsCount_;
  uint32_t userIndex_;
  std::string hostAddress_;
  std::string macAddress_;
  xe::be<uint16_t> port_;

  // GetDetails
  xe::be<uint32_t> openPublicSlotsCount_;
  xe::be<uint32_t> openPrivateSlotsCount_;
  xe::be<uint32_t> filledPublicSlotsCount_;
  xe::be<uint32_t> filledPrivateSlotsCount_;

  std::vector<PlayerObjectJSON> players_{};
};
}  // namespace kernel
}  // namespace xe
