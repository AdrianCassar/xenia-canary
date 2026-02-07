/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/setusersettings_object_json.h"

namespace xe {
namespace kernel {
SetUserSettingsObjectJSON::SetUserSettingsObjectJSON() {}

SetUserSettingsObjectJSON::~SetUserSettingsObjectJSON() {}

bool SetUserSettingsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return false;
}

bool SetUserSettingsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("settings");
  writer->StartArray();

  for (const auto& [xuid, titles] : set_settings_) {
    writer->StartObject();

    writer->String(fmt::format("{:016X}", xuid).c_str());
    writer->StartArray();

    for (const auto& [title_id, settings] : titles) {
      writer->StartObject();

      writer->String(fmt::format("{:08X}", title_id));
      writer->StartArray();

      for (const auto& setting : settings) {
        const auto setting_base64 = setting.SerializeToBase64();

        if (setting_base64.has_value()) {
          writer->String(setting_base64.value());
        }
      }

      writer->EndArray();
      writer->EndObject();
    }

    writer->EndArray();
    writer->EndObject();
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}

}  // namespace kernel
}  // namespace xe
