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
#include "xenia/kernel/json/player_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
PlayerObjectJSON::PlayerObjectJSON()
    : xuid_(0),
      hostAddress_(""),
      gamertag_(""),
      machineId_(0),
      port_(0),
      macAddress_(0),
      sessionId_(0) {}

PlayerObjectJSON::~PlayerObjectJSON() {}

bool PlayerObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("xuid")) {
    XUID(string_util::from_string<uint64_t>(obj["xuid"].GetString(), true));
  }

  if (obj.HasMember("machineId")) {
    MachineID(
        string_util::from_string<uint64_t>(obj["machineId"].GetString(), true));
  }

  if (obj.HasMember("hostAddress")) {
    HostAddress(obj["hostAddress"].GetString());
  }

  if (obj.HasMember("gamertag")) {
    Gamertag(obj["gamertag"].GetString());
  }

  if (obj.HasMember("macAddress")) {
    xe::kernel::MacAddress address =
        xe::kernel::MacAddress(obj["macAddress"].GetString());

    MacAddress(address.to_uint64());
  }

  if (obj.HasMember("sessionId")) {
    SessionID(
        string_util::from_string<uint64_t>(obj["sessionId"].GetString(), true));
  }

  if (obj.HasMember("port")) {
    Port(obj["port"].GetInt());
  }

  return true;
}

bool PlayerObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("xuid");
  writer->String(fmt::format("{:016X}", static_cast<uint64_t>(xuid_)));

  writer->String("machineId");
  writer->String(fmt::format("{:016x}", static_cast<uint64_t>(machineId_)));

  writer->String("hostAddress");
  writer->String(hostAddress_);

  writer->String("gamertag");
  writer->String(gamertag_);

  writer->String("macAddress");
  writer->String(fmt::format("{:012x}", static_cast<uint64_t>(macAddress_)));

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe