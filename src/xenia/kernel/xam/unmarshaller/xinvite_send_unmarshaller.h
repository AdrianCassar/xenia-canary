/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XINVITE_SEND_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XINVITE_SEND_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XInviteSendUnmarshaller : public Unmarshaller {
 public:
  XInviteSendUnmarshaller(uint32_t marshaller_buffer);

  ~XInviteSendUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t UserIndex() const { return user_index_; };

  const uint32_t NumInvitees() const { return num_invitees_; };

  const std::vector<uint64_t> Invitees() const { return invitees_; };

  const uint32_t DisplayStringSize() const { return display_string_size_; };

  const std::u16string DisplayString() const { return display_string_; };

  const uint32_t XMessageHandle() const { return xmsg_handle_; };

 private:
  uint32_t user_index_;
  uint32_t num_invitees_;
  std::vector<uint64_t> invitees_;
  uint32_t display_string_size_;
  std::u16string display_string_;
  uint32_t xmsg_handle_;  // const 0
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XINVITE_SEND_UNMARSHALLER_H_