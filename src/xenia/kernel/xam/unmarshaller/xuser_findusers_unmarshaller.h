/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_FINDUSERS_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_FINDUSERS_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XUserFindUsersUnmarshaller : public Unmarshaller {
 public:
  XUserFindUsersUnmarshaller(uint32_t marshaller_buffer);

  ~XUserFindUsersUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const BASE_MSG_HEADER MessageHeader() const { return msg_header_; };

  const uint64_t XUIDIssuer() const { return xuid_issuer_; };

  const uint32_t NumUsers() const { return num_users_; };

  const std::vector<FIND_USER_INFO>& Users() const { return users_; };

 private:
  BASE_MSG_HEADER msg_header_;
  uint64_t xuid_issuer_;
  uint32_t num_users_;
  std::vector<FIND_USER_INFO> users_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_FINDUSERS_UNMARSHALLER_H_