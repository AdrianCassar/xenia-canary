/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_DELETE_MY_PROFILES_OBJECT_JSON_H_
#define XENIA_KERNEL_DELETE_MY_PROFILES_OBJECT_JSON_H_

#include <map>

#include "xenia/base/string_util.h"
#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
class DeleteMyProfilesObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  DeleteMyProfilesObjectJSON();
  virtual ~DeleteMyProfilesObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  void AddDeletedProfile(std::string& gamertag, std::string& xuid_str) {
    const uint64_t xuid =
        xe::string_util::from_string<uint64_t>(xuid_str, true);

    deleted_profiles[xuid] = gamertag;
  }

  const std::map<uint64_t, std::string> GetDeletedProfiles() const {
    return deleted_profiles;
  }

 private:
  std::map<uint64_t, std::string> deleted_profiles;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_DELETE_MY_PROFILES_OBJECT_JSON_H_