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
#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
class LeaderboardObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  LeaderboardObjectJSON();
  virtual ~LeaderboardObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<XSESSION_VIEW_PROPERTIES>& ViewProperties() const {
    return view_properties_;
  }
  void ViewProperties(
      const std::vector<XSESSION_VIEW_PROPERTIES>& view_properties) {
    view_properties_ = view_properties;
  }

  const XGI_STATS_WRITE& Stats() const { return stats_; }
  void Stats(const XGI_STATS_WRITE& stats) { stats_ = stats; }

 private:
  XGI_STATS_WRITE stats_;
  std::vector<XSESSION_VIEW_PROPERTIES> view_properties_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_LEADERBOARD_OBJECT_JSON_H_