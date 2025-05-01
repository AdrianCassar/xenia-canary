/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XSTRINGVERIFY_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XSTRINGVERIFY_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XStringVerifyUnmarshaller : public Unmarshaller {
 public:
  XStringVerifyUnmarshaller(uint32_t marshaller_buffer);

  ~XStringVerifyUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t TitleId() const { return title_id_; };

  const uint32_t Flags() const { return flags_; };

  const uint16_t LocaleSize() const { return locale_size_; };

  const uint16_t NumStrings() const { return num_strings_; };

  const std::string Locale() const { return locale_; };

  const std::vector<std::string>& StringToVerify() const {
    return strings_to_verify_;
  };

 private:
  uint32_t title_id_;
  uint32_t flags_;
  uint16_t locale_size_;
  uint16_t num_strings_;
  std::string locale_;
  std::vector<std::string> strings_to_verify_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XSTRINGVERIFY_UNMARSHALLER_H_