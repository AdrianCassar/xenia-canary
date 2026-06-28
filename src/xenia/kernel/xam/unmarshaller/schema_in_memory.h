/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_SCHEMA_IN_MEMORY_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_SCHEMA_IN_MEMORY_H_

#include "xenia/kernel/xnet.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

class SchemaInMemory : public SCHEMA_DATA {
 private:
  ORDINAL_TO_INDEX* ordinal_to_index_ptr_ = nullptr;
  SCHEMA_TABLE_ENTRY* schema_table_entry_ptr_ = nullptr;
  uint8_t* schema_data_ptr_ = nullptr;
  xe::be<uint16_t>* url_offsets_ptr_ = nullptr;
  char* url_data_ptr_ = nullptr;
  xe::be<uint32_t>* constant_list_ptr_ = nullptr;

  std::unordered_map<uint16_t, std::string> named_ordinals_ = {
      {0x0305, "XOnlineQuerySearch"},
      {0x0583, "XInviteSend"},
      {0x0602, "XAccountGetPointsBalance"},
      {0x0604, "XAccountGetUserInfo"},
      {0x0613, "XPassportGetMemberName"},
      {0x0636, "XAccountGetUserInfo"},
      {0x0704, "XUserEstimateRankForRating"},
      {0x0716, "XUserValidateAvatarManifest"},
      {0x0801, "XStringVerify"},
      {0x0900, "XOfferingContentEnumerate"},
      {0x0904, "XOfferingSubscriptionEnumerate"},
      {0x0908, "XBannerGetList"},
      {0x0909, "XBannerGetListHot"},
      {0x0A03, "XUserFindUsers"},
      {0x0E11, "XStorageDownloadToMemory"},
      {0x0E13, "XStorageUploadFromMemory"},
      {0x0E14, "XStorageEnumerate"},
      {0x0E15, "XStorageDelete"},
  };

 public:
  SchemaInMemory(KernelState* kernel_state);

  void Bind(uint32_t schema_address);

  void BindToSchema(uint32_t schema_address);

  void ResolveBind();

  bool GetSchemaEntry(uint16_t schema_index,
                      SCHEMA_TABLE_ENTRY* schema_entry_ptr);

  bool GetSchemaDataFromEntry(SCHEMA_TABLE_ENTRY* schema_entry_ptr,
                              bool request, std::span<uint8_t>* schema_data);

  bool EndianBufferBind(BASE_ENDIAN_BUFFER* base, std::span<uint8_t>& buffer);

  bool XLookupSchemaIndexFromOrdinal(uint16_t ordinal,
                                     uint16_t* schema_index) const;

  bool LookupUrlFromTable(uint16_t url_index, std::string_view* url_ptr);

  bool LookupConstantFromTable(uint16_t constant_index, uint32_t* value_ptr);

  std::string_view GetTaskUrl(uint16_t schema_index);

  std::string GetOrdinalFunctionName(uint16_t ordinal);

  std::pair<uint16_t, uint16_t> SchemaVersion() const;

  std::string SchemaVersionString() const;

  void PrettyPrintSchemaTables() const;

  void PrettyPrintUrls();

  void PrettyPrintUrlsWithSchemaIndex();

  void PrettyPrintOrdinalToIndex() const;

 protected:
  KernelState* kernel_state_;
  Memory* memory_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_SCHEMA_IN_MEMORY_H_
