/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_PRESENCE_OBJECT_JSON_H_
#define XENIA_KERNEL_PRESENCE_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/json/friend_presence_object_json.h"

namespace xe {
namespace kernel {
class PresenceObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  PresenceObjectJSON();
  virtual ~PresenceObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const void AddPresence(FriendPresenceObjectJSON presence) {
    players_presence_.push_back(presence);
  }

  const std::vector<FriendPresenceObjectJSON>& PlayersPresence() const {
    return players_presence_;
  }
  void PlayersPresence(const std::vector<FriendPresenceObjectJSON>& presences) {
    players_presence_ = presences;
  }

 private:
  std::vector<FriendPresenceObjectJSON> players_presence_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_PRESENCE_OBJECT_JSON_H_