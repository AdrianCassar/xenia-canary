/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "base_object_json.h"

namespace xe {
namespace kernel {
std::pair<bool, std::string> BaseObjectJSON::Serialize() const {
  rapidjson::StringBuffer ss;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(ss);

  std::pair<bool, std::string> result{};

  if (Serialize(&writer)) {
    result.first = true;
    result.second = ss.GetString();
  }

  return result;
}

bool BaseObjectJSON::Deserialize(const std::string& s) {
  rapidjson::Document doc;

  if (!InitDocument(s, doc)) {
    return false;
  }

  return Deserialize(doc);
}

bool BaseObjectJSON::DeserializeFromString(const std::string& s) {
  return Deserialize(s);
}

bool BaseObjectJSON::SerializeToString(std::string& s) {
  std::pair<bool, std::string> result = Serialize();

  s = result.second;

  return result.first;
}

bool BaseObjectJSON::InitDocument(const std::string& s,
                                  rapidjson::Document& doc) {
  if (s.empty()) {
    return false;
  }

  std::string validJson(s);

  return !doc.Parse(validJson.c_str()).HasParseError() ? true : false;
}
}  // namespace kernel
}  // namespace xe