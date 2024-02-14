/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <vector>

#include "xenia/kernel/base_object_json.h"

namespace xe {
namespace kernel {

class ArbitrationObjectJSON : public BaseObjectJSON {
 public:
  ArbitrationObjectJSON();
  virtual ~ArbitrationObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  struct MachineInfo {
    xe::be<uint64_t> machine_id;
    xe::be<uint32_t> player_count;
    std::vector<uint64_t> xuids;
  };

  const xe::be<uint32_t>& TotalPlayers() const { return total_players_; }
  void TotalPlayers(const xe::be<uint32_t>& total_players) {
    total_players_ = total_players;
  }

  const std::vector<MachineInfo>& Machines() const { return machines_; }
  void Machines(const std::vector<MachineInfo>& machines) {
    machines_ = machines;
  }

 private:
  xe::be<uint32_t> total_players_;
  std::vector<MachineInfo> machines_;
};

}  // namespace kernel
}  // namespace xe
