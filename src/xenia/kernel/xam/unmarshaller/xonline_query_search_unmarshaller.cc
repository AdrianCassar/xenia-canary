/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xonline_query_search_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XQuerySearchUnmarshaller::XQuerySearchUnmarshaller(uint32_t marshaller_address)
    : Unmarshaller(marshaller_address),
      title_id_(0),
      dataset_id_(0),
      proc_index_(0),
      page_(0),
      results_pre_page_(0),
      num_result_specs_(0),
      num_attributes_(0),
      attributes_spec_({}) {};

X_HRESULT XQuerySearchUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  title_id_ = Read<uint32_t>();
  dataset_id_ = Read<uint32_t>();
  proc_index_ = Read<uint32_t>();
  page_ = Read<uint32_t>();
  results_pre_page_ = Read<uint32_t>();
  num_result_specs_ = Read<uint32_t>();
  num_attributes_ = Read<uint32_t>();

  for (uint32_t i = 0; i < num_result_specs_; i++) {
    X_ONLINE_QUERY_ATTRIBUTE_SPEC attribute_spec =
        Read<X_ONLINE_QUERY_ATTRIBUTE_SPEC>();

    attributes_spec_.push_back(attribute_spec);
  }

  assert_false(num_attributes_ > 0);

  // for (uint32_t i = 0; i < num_attributes_; i++) {
  //   X_ONLINE_QUERY_ATTRIBUTE_DATA* attribute =
  //       reinterpret_cast<X_ONLINE_QUERY_ATTRIBUTE_DATA*>(
  //           Advance(sizeof(X_ONLINE_QUERY_ATTRIBUTE_DATA)).data());
  // }

  if (GetPosition() !=
      GetAsyncTask()->GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  if (results_pre_page_ == 0) {
    return X_E_INVALIDARG;
  }

  if (results_pre_page_ > X_ONLINE_QUERY_MAX_PAGE_SIZE) {
    return X_E_INVALIDARG;
  }

  if (dataset_id_ != X_ONLINE_LSP_DEFAULT_DATASET_ID) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

void XQuerySearchUnmarshaller::PrettyPrintAttributesSpec() {
  std::string attribute_details = "\n\nXOnlineQuerySearch Attributes:\n";

  for (const auto& attribute : attributes_spec_) {
    std::string attribute_type = "Unknown";
    std::string attribute_desc = "";

    switch (attribute.type & X_ATTRIBUTE_DATATYPE_MASK) {
      case X_ATTRIBUTE_DATATYPE_INTEGER:
        attribute_type = "Integer";
        break;
      case X_ATTRIBUTE_DATATYPE_STRING:
        attribute_type = "String";
        break;
      case X_ATTRIBUTE_DATATYPE_BLOB:
        attribute_type = "Blob";
        break;
    }

    switch (attribute.type) {
      case X_ONLINE_LSP_ATTRIBUTE_TSADDR:
        attribute_desc = "TSADDR";
        break;
      case X_ONLINE_LSP_ATTRIBUTE_XNKID:
        attribute_desc = "XNKID";
        break;
      case X_ONLINE_LSP_ATTRIBUTE_KEY:
        attribute_desc = "XNKEY";
        break;
      case X_ONLINE_LSP_ATTRIBUTE_USER:
        attribute_desc = "USER";
        break;
      case X_ONLINE_LSP_ATTRIBUTE_PARAM_USER:
        attribute_desc = "PARAM USER";
        break;
      default:
        attribute_desc = attribute_type;
        break;
    }

    attribute_details.append(fmt::format(
        "ID: {:08X}\nSize: {}\nType: {}\nDescription: {}\n\n", attribute.type,
        attribute.length, attribute_type, attribute_desc));
  }

  XELOGD(attribute_details);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
