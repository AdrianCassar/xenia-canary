/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/string_util.h"
#include "xenia/kernel/friend_presence_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
FriendPresenceObjectJSON::FriendPresenceObjectJSON()
    : xuid_(0),
      gamertag_(""),
      state_(0),
      sessionId_(0),
      title_id_(""),
      state_change_time_(0),
      rich_state_presence_size_(0),
      rich_presence_(u"") {}

FriendPresenceObjectJSON::~FriendPresenceObjectJSON() {}

bool FriendPresenceObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("xuid")) {
    const auto xuid_str = std::string(obj["xuid"].GetString());

    if (!xuid_str.empty()) {
      XUID(string_util::from_string<uint64_t>(xuid_str, true));
    }
  }

  if (obj.HasMember("gamertag")) {
    Gamertag(obj["gamertag"].GetString());
  }

  if (obj.HasMember("state")) {
    State(obj["state"].GetUint());
  }

  if (obj.HasMember("sessionId")) {
    const auto sessionId_str = std::string(obj["sessionId"].GetString());

    if (!sessionId_str.empty()) {
      SessionID(string_util::from_string<uint64_t>(sessionId_str, true));
    }
  }

  if (obj.HasMember("titleId")) {
    TitleID(obj["titleId"].GetString());
  }

  if (obj.HasMember("stateChangeTime")) {
    StateChangeTime(obj["stateChangeTime"].GetUint64());
  }

  if (obj.HasMember("richPresenceStateSize")) {
    // RichStatePresenceSize(obj["richPresenceStateSize"].GetUint());
  }

  if (obj.HasMember("richPresence")) {
    const std::u16string richPresence =
        xe::to_utf16(obj["richPresence"].GetString());

    RichPresence(richPresence);
  }

  return true;
}

bool FriendPresenceObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  // writer->StartObject();

  // writer->String("xuid");
  // writer->String(fmt::format("{:016X}", xuid_));

  // writer->String("gamertag");
  // writer->Uint(gamertag_);

  // writer->String("state");
  // writer->Uint(state_);

  // writer->String("sessionId");
  // writer->String(fmt::format("{:016X}", sessionId_));

  // writer->String("titleId");
  // writer->String(title_id_);

  // writer->String("stateChangeTime");
  // writer->Uint64(state_change_time_);

  // writer->String("richPresenceStateSize");
  // writer->Uint(rich_state_presence_size_);

  //// Problem :/
  // writer->String("richPresence");
  //// writer->String(rich_presence_);

  // writer->EndObject();

  return false;
}

X_ONLINE_PRESENCE FriendPresenceObjectJSON::ToOnlineRichPresence() const {
  X_ONLINE_PRESENCE presence = {};

  presence.xuid = XUID();
  presence.state = State();
  std::memcpy(&presence.session_id, &SessionID(), sizeof(XNKID));

  if (!TitleID().empty()) {
    presence.title_id = string_util::from_string<uint32_t>(TitleID(), true);
  }

  presence.state_change_time = StateChangeTime();
  presence.cchRichPresence = RichStatePresenceSize();

  const std::u16string presence_string = RichPresence();

  char16_t* rich_presence_ptr =
      reinterpret_cast<char16_t*>(presence.wszRichPresence);
  xe::string_util::copy_and_swap_truncating(rich_presence_ptr, presence_string,
                                            presence.cchRichPresence);

  return presence;
}

X_ONLINE_FRIEND FriendPresenceObjectJSON::GetFriendPresence() const {
  X_ONLINE_FRIEND peer = {};

  peer.xuid = XUID();

  char* gamertag_ptr = reinterpret_cast<char*>(peer.Gamertag);
  strcpy(gamertag_ptr, Gamertag().c_str());

  peer.state = State();
  std::memcpy(&peer.session_id, &SessionID(), sizeof(XNKID));

  if (!TitleID().empty()) {
    peer.title_id = string_util::from_string<uint32_t>(TitleID(), true);
  }

  peer.ftUserTime = StateChangeTime();
  peer.cchRichPresence = RichStatePresenceSize();

  const std::u16string presence_string = RichPresence();

  char16_t* rich_presence_ptr =
      reinterpret_cast<char16_t*>(peer.wszRichPresence);
  xe::string_util::copy_and_swap_truncating(rich_presence_ptr, presence_string,
                                            peer.cchRichPresence);

  return peer;
}

FriendsPresenceObjectJSON::FriendsPresenceObjectJSON() : xuids_(0) {}

FriendsPresenceObjectJSON::~FriendsPresenceObjectJSON() {}

bool FriendsPresenceObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  for (const auto& presenceObj : obj.GetArray()) {
    FriendPresenceObjectJSON* presence = new FriendPresenceObjectJSON();
    presence->Deserialize(presenceObj.GetObj());

    players_presence_.push_back(*presence);
  }

  return true;
}

bool FriendsPresenceObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();
  writer->String("xuids");

  writer->StartArray();

  for (const auto xuid : xuids_) {
    writer->String(fmt::format("{:016X}", xuid));
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}

}  // namespace kernel
}  // namespace xe