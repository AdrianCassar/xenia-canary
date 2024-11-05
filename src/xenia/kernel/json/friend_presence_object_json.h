/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FRIEND_PRESENCE_OBJECT_JSON_H_
#define XENIA_KERNEL_FRIEND_PRESENCE_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class FriendPresenceObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  FriendPresenceObjectJSON();
  virtual ~FriendPresenceObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const xe::be<uint64_t>& XUID() const { return xuid_; }
  void XUID(const xe::be<uint64_t>& xuid) { xuid_ = xuid; }

  const std::string& Gamertag() const { return gamertag_; }
  void Gamertag(const std::string& gamertag) {
    gamertag_ = gamertag.substr(0, 15);
  }

  const xe::be<uint32_t>& State() const { return state_; }
  void State(const xe::be<uint32_t>& state) { state_ = state; }

  const xe::be<uint64_t>& SessionID() const { return sessionId_; }
  void SessionID(const xe::be<uint64_t>& sessionId) { sessionId_ = sessionId; }

  const std::string& TitleID() const { return title_id_; }
  void TitleID(const std::string& titleID) { title_id_ = titleID; }

  const xe::be<uint64_t>& StateChangeTime() const { return state_change_time_; }
  void StateChangeTime(const xe::be<uint64_t>& stateChangeTime) {
    state_change_time_ = stateChangeTime;
  }

  const xe::be<uint32_t>& RichStatePresenceSize() const {
    return rich_state_presence_size_;
  }
  void RichStatePresenceSize(const xe::be<uint32_t>& richStatePresenceSize) {
    if (richStatePresenceSize > X_MAX_RICHPRESENCE_SIZE) {
      rich_state_presence_size_ = X_MAX_RICHPRESENCE_SIZE;
      return;
    }

    rich_state_presence_size_ = richStatePresenceSize;
  }

  const std::u16string& RichPresence() const { return rich_presence_; }
  void RichPresence(const std::u16string& richPresence) {
    const xe::be<uint32_t> presence_size =
        static_cast<uint32_t>(richPresence.size()) * sizeof(char16_t);

    std::u16string rich_presence = richPresence;

    if (presence_size > X_MAX_RICHPRESENCE_SIZE) {
      assert_always();

      rich_state_presence_size_ = X_MAX_RICHPRESENCE_SIZE;

      rich_presence =
          richPresence.substr(0, X_MAX_RICHPRESENCE_SIZE / sizeof(char16_t));
    }

    rich_state_presence_size_ = presence_size;

    rich_presence_ = rich_presence;
  }

  X_ONLINE_PRESENCE ToOnlineRichPresence() const;
  X_ONLINE_FRIEND GetFriendPresence() const;

 private:
  xe::be<uint64_t> xuid_;
  std::string gamertag_;
  xe::be<uint32_t> state_;
  xe::be<uint64_t> sessionId_;
  std::string title_id_;
  xe::be<uint64_t> state_change_time_;
  xe::be<uint32_t> rich_state_presence_size_;
  std::u16string rich_presence_;
};

class FriendsPresenceObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  FriendsPresenceObjectJSON();
  virtual ~FriendsPresenceObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const void AddXUID(uint64_t xuid) { xuids_.push_back(xuid); }

  const std::vector<uint64_t>& XUIDs() const { return xuids_; }
  void XUIDs(const std::vector<uint64_t>& xuids) { xuids_ = xuids; }

  const std::vector<FriendPresenceObjectJSON>& PlayersPresence() const {
    return players_presence_;
  }

 private:
  std::vector<uint64_t> xuids_;
  std::vector<FriendPresenceObjectJSON> players_presence_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FRIEND_PRESENCE_OBJECT_JSON_H_