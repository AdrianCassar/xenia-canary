/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

extern "C" {
#include "third_party/FFmpeg/libavutil/base64.h"
}

#include "xenia/base/string_util.h"
#include "xenia/kernel/json/properties_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
PropertiesObjectJSON::PropertiesObjectJSON() : properties_({}) {}

PropertiesObjectJSON::~PropertiesObjectJSON() {}

bool PropertiesObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("properties")) {
    auto& propertiesObj = obj["properties"];

    if (!propertiesObj.IsArray()) {
      return false;
    }

    for (const auto& serialized_property : propertiesObj.GetArray()) {
      const std::optional<xam::Property> property =
          xam::Property::DeserializeBase64(serialized_property.GetString());

      if (property.has_value()) {
        properties_.push_back(property.value());
      }
    }
  }

  return true;
}

bool PropertiesObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("properties");

  writer->StartArray();

  for (const auto& property : properties_) {
    const auto property_base64 = property.SerializeToBase64();

    if (property_base64.has_value()) {
      writer->String(property_base64.value());
    }
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe
