/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SETUSERSETTINGS_OBJECT_JSON_H_
#define XENIA_KERNEL_SETUSERSETTINGS_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xam/user_settings.h"

namespace xe {
namespace kernel {
class SetUserSettingsObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  SetUserSettingsObjectJSON();
  virtual ~SetUserSettingsObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::map<uint64_t,
                 std::map<uint32_t, std::vector<kernel::xam::UserSetting>>>&
  Settings() const {
    return set_settings_;
  }
  void Settings(
      const std::map<uint64_t,
                     std::map<uint32_t, std::vector<kernel::xam::UserSetting>>>&
          settings) {
    set_settings_ = settings;
  }

 private:
  std::map<uint64_t, std::map<uint32_t, std::vector<kernel::xam::UserSetting>>>
      set_settings_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_SETUSERSETTINGS_OBJECT_JSON_H_
