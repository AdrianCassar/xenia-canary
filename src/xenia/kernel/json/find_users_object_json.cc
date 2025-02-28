/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/string_util.h"
#include "xenia/kernel/json/find_users_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
FindUsersObjectJSON::FindUsersObjectJSON() : users({}), resolved_users({}) {}

FindUsersObjectJSON::~FindUsersObjectJSON() {}

bool FindUsersObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  for (const auto& user_info : obj.GetArray()) {
    FIND_USER_INFO info = {};

    if (user_info.HasMember("xuid")) {
      const auto xuid_str = std::string(user_info["xuid"].GetString());

      if (!xuid_str.empty()) {
        info.xuid = string_util::from_string<uint64_t>(xuid_str, true);
      }
    }

    if (user_info.HasMember("gamertag")) {
      strcpy(info.gamertag, user_info["gamertag"].GetString());
    }

    resolved_users.push_back(info);
  }

  return true;
}

bool FindUsersObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("UsersInfo");

  writer->StartArray();

  for (const auto& user : users) {
    writer->StartArray();

    if (user.xuid) {
      writer->String(fmt::format("{:016X}", xe::byte_swap(user.xuid.get())));
    } else {
      writer->String("");
    }

    if (user.gamertag) {
      writer->String(user.gamertag);
    } else {
      writer->String("");
    }

    writer->EndArray();
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe