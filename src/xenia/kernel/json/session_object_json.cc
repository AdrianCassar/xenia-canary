/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/kernel/json/session_object_json.h"

namespace xe {
namespace kernel {
SessionObjectJSON::SessionObjectJSON()
    : sessionid_(""),
      title_(""),
      mediaId_(""),
      version_(""),
      flags_(0),
      publicSlotsCount_(0),
      privateSlotsCount_(0),
      userIndex_(0),
      hostAddress_(""),
      macAddress_(""),
      port_(0),
      openPublicSlotsCount_(0),
      openPrivateSlotsCount_(0),
      filledPublicSlotsCount_(0),
      filledPrivateSlotsCount_(0) {}

SessionObjectJSON::~SessionObjectJSON() {}

bool SessionObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("id")) {
    SessionID(obj["id"].GetString());
  }

  if (obj.HasMember("xuid")) {
    XUID(obj["xuid"].GetString());
  }

  if (obj.HasMember("title")) {
    Title(obj["title"].GetString());
  }

  if (obj.HasMember("mediaId")) {
    MediaID(obj["mediaId"].GetString());
  }

  if (obj.HasMember("version")) {
    Version(obj["version"].GetString());
  }

  if (obj.HasMember("flags")) {
    Flags(obj["flags"].GetInt());
  }

  if (obj.HasMember("publicSlotsCount")) {
    PublicSlotsCount(obj["publicSlotsCount"].GetInt());
  }

  if (obj.HasMember("privateSlotsCount")) {
    PrivateSlotsCount(obj["privateSlotsCount"].GetInt());
  }

  // Create session only?
  if (obj.HasMember("userIndex")) {
    UserIndex(obj["userIndex"].GetInt());
  }

  if (obj.HasMember("hostAddress")) {
    HostAddress(obj["hostAddress"].GetString());
  }

  if (obj.HasMember("macAddress")) {
    MacAddress(obj["macAddress"].GetString());
  }

  if (obj.HasMember("port")) {
    Port(obj["port"].GetInt());
  }

  // GetDetails
  if (obj.HasMember("openPublicSlotsCount")) {
    OpenPublicSlotsCount(obj["openPublicSlotsCount"].GetInt());
  }

  if (obj.HasMember("openPrivateSlotsCount")) {
    OpenPrivateSlotsCount(obj["openPrivateSlotsCount"].GetInt());
  }

  if (obj.HasMember("filledPublicSlotsCount")) {
    FilledPublicSlotsCount(obj["filledPublicSlotsCount"].GetInt());
  }

  if (obj.HasMember("filledPrivateSlotsCount")) {
    FilledPrivateSlotsCount(obj["filledPrivateSlotsCount"].GetInt());
  }

  if (obj.HasMember("players")) {
    const auto& playersArray = obj["players"].GetArray();

    std::vector<PlayerObjectJSON> players{};

    for (uint8_t i = 0; i < playersArray.Size(); i++) {
      PlayerObjectJSON player = PlayerObjectJSON();
      player.Deserialize(playersArray[i].GetObj());

      players.push_back(player);
    }

    Players(players);
  }

  return true;
}

bool SessionObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("sessionId");
  writer->String(sessionid_);

  writer->String("xuid");
  writer->String(xuid_);

  writer->String("title");
  writer->String(title_);

  writer->String("mediaId");
  writer->String(mediaId_);

  writer->String("version");
  writer->String(version_);

  writer->String("flags");
  writer->Uint(flags_);

  writer->String("publicSlotsCount");
  writer->Uint(publicSlotsCount_);

  writer->String("privateSlotsCount");
  writer->Uint(privateSlotsCount_);

  writer->String("userIndex");
  writer->Uint(userIndex_);

  writer->String("hostAddress");
  writer->String(hostAddress_);

  writer->String("macAddress");
  writer->String(macAddress_);

  writer->String("port");
  writer->Int(port_);

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe