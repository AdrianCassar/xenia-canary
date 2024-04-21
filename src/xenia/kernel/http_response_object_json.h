/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_HTTP_RESPONSE_OBJECT_JSON_H_
#define XENIA_KERNEL_HTTP_RESPONSE_OBJECT_JSON_H_

#include "xenia/kernel/base_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {

class HTTPResponseObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  HTTPResponseObjectJSON(response_data chunk);
  virtual ~HTTPResponseObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::string& Message() const { return message_; }
  void Message(const std::string& message) { message_ = message; }

  const std::string& Error() const { return error_; }
  void Error(const std::string& error) { error_ = error; }

  const uint64_t& StatusCode() const { return status_code_; }
  void StatusCode(const uint64_t& status_code) { status_code_ = status_code; }

  const response_data& RawResponse() const { return raw_response_; }

  template <typename T>
  std::unique_ptr<T> Deserialize() {
    std::unique_ptr<T> instance = std::make_unique<T>();
    bool validJSON = instance->Deserialize(raw_response_.response);
    return instance;
  }

 private:
  std::string message_;
  std::string error_;
  uint64_t status_code_;
  response_data raw_response_;
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HTTP_RESPONSE_OBJECT_JSON_H_