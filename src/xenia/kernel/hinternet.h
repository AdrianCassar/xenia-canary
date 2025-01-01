/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_HINTERNET_H_
#define XENIA_KERNEL_HINTERNET_H_

#include <string>
#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {

class KernelState;

class HINTERNET : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Internet;

  HINTERNET(KernelState* kernel_state);

  uint32_t getLastError() const { return last_error_; }

  std::string user_agent() const { return user_agent_; };

  std::string server_name() const { return server_name_; };

  uint16_t port() const { return port_; };

  std::string path() const { return path_; };

  std::string url() const { return url_; };

  std::string method() const { return method_; };

  std::string path_ = "";
  std::string server_name_ = "";
  std::string user_agent_ = "";
  std::string method_ = "";
  uint32_t port_ = 80;
  std::string url_ = "";
  uint32_t last_error_ = 0;

  void SendRequest(std::string Headers, std::string buffer);

  void Connect();

  void setLastError(uint32_t error) { last_error_ = error; };

  HINTERNET* CreateSessionHandle(std::string user_agent);

  HINTERNET* CreateConnectionHandle(std::string server_name, uint32_t port);

  HINTERNET* CreateRequestHandle(std::string path, std::string method);
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_HINTERNET_H_