/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/services_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
ServicesObjectJSON::ServicesObjectJSON()
    : services_results_({}), query_search_results_({}) {}

ServicesObjectJSON::~ServicesObjectJSON() {}

bool ServicesObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (!obj.IsObject()) {
    return false;
  }

  if (!obj.HasMember("services") && !obj.HasMember("querysearch")) {
    return false;
  }

  if (obj.HasMember("services") && obj["services"].IsArray()) {
    for (const auto& service : obj["services"].GetArray()) {
      X_ONLINE_SERVICE_INFO service_info = {};

      if (service.HasMember("service_id")) {
        service_info.id = service["service_id"].GetInt();
      }

      if (service.HasMember("address")) {
        service_info.ip = ip_to_in_addr(service["address"].GetString());
      }

      if (service.HasMember("port")) {
        service_info.port = service["port"].GetInt();
      }
    }
  }

  if (obj.HasMember("querysearch") && obj["querysearch"].IsArray()) {
    for (const auto& service : obj["querysearch"].GetArray()) {
      TSADDR server_addr = {};

      if (service.HasMember("address")) {
        server_addr.inaOnline = ip_to_in_addr(service["address"].GetString());
      }

      if (service.HasMember("port")) {
        server_addr.wPortOnline = service["port"].GetInt();
      }

      query_search_results_.push_back(server_addr);
    }
  }

  return true;
}

bool ServicesObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}
}  // namespace kernel
}  // namespace xe