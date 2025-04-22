/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/delete_my_profiles_json.h"

namespace xe {
namespace kernel {
DeleteMyProfilesObjectJSON::DeleteMyProfilesObjectJSON()
    : deleted_profiles({}) {}

DeleteMyProfilesObjectJSON::~DeleteMyProfilesObjectJSON() {}

bool DeleteMyProfilesObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  for (const auto& profiles : obj.GetArray()) {
    const auto profile = profiles.GetArray();

    if (profile.Size() == 2) {
      std::string gamertag = profile[0].GetString();
      std::string xuid = profile[1].GetString();

      AddDeletedProfile(gamertag, xuid);
    }
  }

  return true;
}

bool DeleteMyProfilesObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}
}  // namespace kernel
}  // namespace xe