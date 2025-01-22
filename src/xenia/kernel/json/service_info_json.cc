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
#include "xenia/kernel/json/service_info_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
ServiceInfoObjectJSON::ServiceInfoObjectJSON() : hostAddress_(""), port_(0) {}

ServiceInfoObjectJSON::~ServiceInfoObjectJSON() {}

bool ServiceInfoObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsArray()) {
    return false;
  }

  for (const auto& service_info : obj.GetArray()) {
    if (service_info.HasMember("address")) {
      Address(service_info["address"].GetString());
    }

    if (service_info.HasMember("port")) {
      Port(service_info["port"].GetInt());
    }

    break;
  }

  return true;
}

bool ServiceInfoObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}
}  // namespace kernel
}  // namespace xe