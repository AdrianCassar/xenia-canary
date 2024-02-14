/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/string_util.h"
#include "xenia/kernel/arbitration_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
ArbitrationObjectJSON::ArbitrationObjectJSON()
    : total_players_(0), machines_() {}

ArbitrationObjectJSON::~ArbitrationObjectJSON() {}

bool ArbitrationObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("totalPlayers")) {
    TotalPlayers(obj["totalPlayers"].GetUint());
  }

  if (obj.HasMember("machines")) {
    const auto machinesArray = obj["machines"].GetArray();

    std::vector<MachineInfo> machine_info{};

    for (uint8_t i = 0; i < machinesArray.Size(); i++) {
      MachineInfo machine{};

      machine.machine_id = string_util::from_string<uint64_t>(
          machinesArray[i]["id"].GetString(), true);

      const auto players = machinesArray[i]["players"].GetArray();
      machine.player_count = players.Size();

      for (const auto& player : players) {
        const auto xuid = string_util::from_string<uint64_t>(
            player["xuid"].GetString(), true);

        machine.xuids.push_back(xuid);
      }

      machine_info.push_back(machine);
    }

    Machines(machine_info);
  }

  return true;
}

bool ArbitrationObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return true;
}
}  // namespace kernel
}  // namespace xe