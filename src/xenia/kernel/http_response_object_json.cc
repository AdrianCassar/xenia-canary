/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/http_response_object_json.h"

namespace xe {
namespace kernel {
HTTPResponseObjectJSON::HTTPResponseObjectJSON(response_data chunk)
    : message_(""), error_(""), status_code_(0) {
  raw_response_ = chunk;
}

HTTPResponseObjectJSON::~HTTPResponseObjectJSON() {}

// Parse JSON exception keys by default, otherwise it's a non-erroneous
// response.
bool HTTPResponseObjectJSON::Deserialize(const rapidjson::Value& obj) {
  bool isValid = false;

  // {} or []
  if (obj.IsObject() || obj.IsArray()) {
    isValid = true;
  }

  if (obj.IsObject()) {
    // Custom error message
    if (obj.HasMember("message")) {
      Message(obj["message"].GetString());
    }

    // Original error message
    if (obj.HasMember("error")) {
      Error(obj["error"].GetString());
    }

    if (obj.HasMember("statusCode")) {
      StatusCode(obj["statusCode"].GetUint64());
    }
  }

  return isValid;
}

bool HTTPResponseObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return true;
}
}  // namespace kernel
}  // namespace xe