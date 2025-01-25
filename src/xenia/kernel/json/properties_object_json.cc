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

    for (auto& serialized_property : propertiesObj.GetArray()) {
      const std::string base64 = serialized_property.GetString();
      std::uint32_t base64_size = static_cast<uint32_t>(base64.size());
      std::uint32_t base64_decode_size = AV_BASE64_DECODE_SIZE(base64_size);

      uint8_t* data_out = new uint8_t[base64_decode_size];
      auto out = av_base64_decode(data_out, base64.c_str(), base64_decode_size);

      std::span<uint8_t> data_span =
          std::span<uint8_t>(data_out, base64_decode_size);

      const xam::Property property = xam::Property(data_span);

      properties_.push_back(property);
    }
  }

  return true;
}

bool PropertiesObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("properties");

  writer->StartArray();

  for (const auto& entry : properties_) {
    auto entry_data = entry.Serialize();

    const uint32_t entry_size = static_cast<uint32_t>(entry_data.size());
    const uint32_t entry_out_size = AV_BASE64_SIZE(entry_size);

    std::vector<char> entry_serialized(entry_out_size);

    auto out = av_base64_encode(entry_serialized.data(), entry_out_size,
                                entry_data.data(), entry_size);

    std::string base64_out = std::string(entry_serialized.data());

    writer->String(base64_out);
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe