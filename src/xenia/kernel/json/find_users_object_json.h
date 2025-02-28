/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FIND_USERS_OBJECT_JSON_H_
#define XENIA_KERNEL_FIND_USERS_OBJECT_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"
#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class FindUsersObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  FindUsersObjectJSON();
  virtual ~FindUsersObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  void SetFindUsers(const std::vector<FIND_USER_INFO>& find_users) {
    users = find_users;
  }

  void AddUserInfo(const FIND_USER_INFO& user_info) {
    users.push_back(user_info);
  }

  std::vector<FIND_USER_INFO>& GetResolvedUsers() { return resolved_users; }

 private:
  std::vector<FIND_USER_INFO> users;
  std::vector<FIND_USER_INFO> resolved_users;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FIND_USERS_OBJECT_JSON_H_