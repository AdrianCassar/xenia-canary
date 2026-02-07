/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_GETUSERSETTINGS_OBJECT_JSON_H_
#define XENIA_KERNEL_GETUSERSETTINGS_OBJECT_JSON_H_

#include <map>
#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xam/user_settings.h"

namespace xe {
namespace kernel {
class GetUserSettingsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  // Settings must maintain order.
  using users_settings_ids =
      std::map<uint64_t,
               std::map<uint32_t, std::vector<kernel::xam::UserSettingId>>>;

  // Settings must maintain order.
  using users_settings =
      std::map<uint64_t,
               std::map<uint32_t, std::vector<kernel::xam::UserSetting>>>;

  GetUserSettingsObjectJSON();
  virtual ~GetUserSettingsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const users_settings_ids& SettingIds() const { return settings_ids_; }
  void SettingIds(const users_settings_ids& settings) {
    settings_ids_ = settings;
  }

  const users_settings& Settings() const { return users_settings_; }
  void Settings(const users_settings& users_settings) {
    users_settings_ = users_settings;
  }

 private:
  users_settings_ids settings_ids_;
  users_settings users_settings_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_GETUSERSETTINGS_OBJECT_JSON_H_
