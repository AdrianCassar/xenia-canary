/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XONLINE_QUERYSEARCH_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XONLINE_QUERYSEARCH_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XQuerySearchUnmarshaller : public Unmarshaller {
 public:
  XQuerySearchUnmarshaller(uint32_t marshaller_buffer);

  ~XQuerySearchUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t TitleID() const { return title_id_; };

  const uint32_t DatasetID() const { return dataset_id_; };

  const uint32_t ProcedureIndex() const { return proc_index_; };

  const uint32_t Page() const { return page_; };

  const uint32_t ResultsPrePage() const { return results_pre_page_; };

  const uint32_t NumResultSpects() const { return num_result_specs_; };

  const uint32_t NumAttributes() const { return num_attributes_; };

  const std::vector<X_ONLINE_QUERY_ATTRIBUTE_SPEC> SpecAttributes() const {
    return attributes_spec_;
  };

  void PrettyPrintAttributesSpec();

 private:
  uint32_t title_id_;
  uint32_t dataset_id_;
  uint32_t proc_index_;
  uint32_t page_;
  uint32_t results_pre_page_;
  uint32_t num_result_specs_;
  uint32_t num_attributes_;
  std::vector<X_ONLINE_QUERY_ATTRIBUTE_SPEC> attributes_spec_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XONLINE_QUERYSEARCH_UNMARSHALLER_H_