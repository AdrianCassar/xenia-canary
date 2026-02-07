/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/getusersettings_object_json.h"

namespace xe {
namespace kernel {
GetUserSettingsObjectJSON::GetUserSettingsObjectJSON() {}

GetUserSettingsObjectJSON::~GetUserSettingsObjectJSON() {}

bool GetUserSettingsObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsObject()) {
    return false;
  }

  const rapidjson::Value::ConstMemberIterator settingsObj_itr =
      obj.FindMember("settings");

  if (settingsObj_itr == obj.MemberEnd()) {
    return false;
  }

  const auto& settingsObj = settingsObj_itr->value;

  if (!settingsObj.IsArray()) {
    return false;
  }

  for (const auto& xuidsObj : settingsObj.GetArray()) {
    if (xuidsObj.IsObject()) {
      const auto& xuid = xuidsObj.GetObj();

      for (rapidjson::Value::ConstMemberIterator xuids_itr = xuid.MemberBegin();
           xuids_itr != xuid.MemberEnd(); ++xuids_itr) {
        const std::string xuidStr = xuids_itr->name.GetString();
        const uint64_t xuid =
            xe::string_util::from_string<uint64_t>(xuidStr, true);

        const auto& title_ids = xuids_itr->value.GetArray();

        for (const auto& title_idObjs : title_ids) {
          const auto& title_idObj = title_idObjs.GetObj();

          for (rapidjson::Value::ConstMemberIterator title_idsObj_itr =
                   title_idObj.MemberBegin();
               title_idsObj_itr != title_idObj.MemberEnd();
               ++title_idsObj_itr) {
            const std::string title_idStr = title_idsObj_itr->name.GetString();
            const uint32_t title_id =
                xe::string_util::from_string<uint32_t>(title_idStr, true);

            const auto& settings = title_idsObj_itr->value.GetArray();

            for (const auto& setting : settings) {
              const std::string setting_base64 = setting.GetString();

              const auto setting =
                  xam::UserSetting::DeserializeBase64(setting_base64);

              if (setting.has_value()) {
                users_settings_[xuid][title_id].push_back(setting.value());
              }
            }
          }
        }
      }
    }
  }

  return false;
}

bool GetUserSettingsObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("settings");
  writer->StartArray();

  for (const auto& [xuid, titles] : settings_ids_) {
    writer->StartObject();

    writer->String(fmt::format("{:016X}", xuid).c_str());
    writer->StartArray();

    for (const auto& [title_id, settings] : titles) {
      writer->StartObject();

      writer->String(fmt::format("{:08X}", title_id));
      writer->StartArray();

      for (const auto& setting_id : settings) {
        writer->String(
            fmt::format("{:08X}", static_cast<uint32_t>(setting_id)));
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
