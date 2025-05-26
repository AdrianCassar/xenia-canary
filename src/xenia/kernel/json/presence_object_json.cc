/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/json/presence_object_json.h"

namespace xe {
namespace kernel {
PresenceObjectJSON::PresenceObjectJSON() : players_presence_({}) {}

PresenceObjectJSON::~PresenceObjectJSON() {}

bool PresenceObjectJSON::Deserialize(const rapidjson::Value& obj) {
  return false;
}

bool PresenceObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("presence");

  writer->StartArray();

  for (const auto& presence : players_presence_) {
    writer->StartObject();

    writer->String("xuid");
    writer->String(fmt::format("{:016X}", presence.XUID().get()));

    writer->String("richPresence");
    writer->String(xe::to_utf8(presence.RichPresence()));

    // writer->String("state");
    // writer->Uint(state_);

    // writer->String("sessionId");
    // writer->String(fmt::format("{:016X}", sessionId_));

    // writer->String("titleId");
    // writer->String(title_id_);

    // writer->String("stateChangeTime");
    // writer->Uint64(state_change_time_);

    writer->EndObject();
  }

  writer->EndArray();

  writer->EndObject();

  return true;
}
}  // namespace kernel
}  // namespace xe