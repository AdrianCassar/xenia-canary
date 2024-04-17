/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_
#define XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_

#include "xenia//kernel/xsession.h"
#include "xenia/kernel/base_object_json.h"

namespace xe {
namespace kernel {
class LeaderboardObjectJSON : public BaseObjectJSON {
 public:
  LeaderboardObjectJSON();
  virtual ~LeaderboardObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<XSessionViewProperties>& ViewProperties() const {
    return view_properties_;
  }
  void ViewProperties(
      const std::vector<XSessionViewProperties>& view_properties) {
    view_properties_ = view_properties;
  }

  const XSessionWriteStats& Stats() const { return stats_; }
  void Stats(const XSessionWriteStats& stats) { stats_ = stats; }

 private:
  XSessionWriteStats stats_;
  std::vector<XSessionViewProperties> view_properties_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_