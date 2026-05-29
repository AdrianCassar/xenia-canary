/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/schema_in_memory.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {
namespace xam {

SchemaInMemory::SchemaInMemory(KernelState* kernel_state)
    : SCHEMA_DATA{},
      kernel_state_(kernel_state),
      memory_(kernel_state->memory()) {};

void SchemaInMemory::Bind(uint32_t schema_address) {
  XONLINE_SCHEMA_DATA* schema_data_ptr =
      memory_->TranslateVirtual<XONLINE_SCHEMA_DATA*>(schema_address);

  const SCHEMA_HEADER* SchemaHeader =
      memory_->TranslateVirtual<SCHEMA_HEADER*>(schema_data_ptr->schema_ptr);

  Header = *SchemaHeader;

  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(SchemaHeader + 1);

  OrdinalToIndexPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  cursor += SchemaHeader->SchemaTableEntries * sizeof(ORDINAL_TO_INDEX);

  TableEntriesPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  cursor +=
      SchemaHeader->SchemaTableEntries * SchemaHeader->SchemaTableEntrySize;

  SchemaDataPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  SchemaDataSize =
      SchemaHeader->ConstantsTableOffset -
      (SchemaHeader->SchemaTableEntries * sizeof(ORDINAL_TO_INDEX)) -
      SchemaHeader->ExtensionDataSize -
      (SchemaHeader->SchemaTableEntries * SchemaHeader->SchemaTableEntrySize) -
      sizeof(SCHEMA_HEADER);

  ExtensionDataPtr = 0;

  cursor += SchemaDataSize;

  ConstantListPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  cursor += Header.ConstantsTableSize * Header.ConstantSize;

  UrlOffsetsPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  cursor += Header.UrlTableSize * sizeof(uint16_t);

  UrlDataPtr = memory_->HostToGuestVirtual(std::to_address(cursor));

  ResolveBind();
}

void SchemaInMemory::BindToSchema(uint32_t schema_address) {
  const SCHEMA_DATA* title_schema =
      memory_->TranslateVirtual<const SCHEMA_DATA*>(schema_address);

  Header = title_schema->Header;
  OrdinalToIndexPtr = title_schema->OrdinalToIndexPtr;
  TableEntriesPtr = title_schema->TableEntriesPtr;
  SchemaDataPtr = title_schema->SchemaDataPtr;
  SchemaDataSize = title_schema->SchemaDataSize;
  ExtensionDataPtr = title_schema->ExtensionDataPtr;
  ConstantListPtr = title_schema->ConstantListPtr;
  UrlOffsetsPtr = title_schema->UrlOffsetsPtr;
  UrlDataPtr = title_schema->UrlDataPtr;

  ResolveBind();
}

void SchemaInMemory::ResolveBind() {
  ordinal_to_index_ptr_ =
      memory_->TranslateVirtual<ORDINAL_TO_INDEX*>(OrdinalToIndexPtr);

  schema_table_entry_ptr_ =
      memory_->TranslateVirtual<SCHEMA_TABLE_ENTRY*>(TableEntriesPtr);

  schema_data_ptr_ = memory_->TranslateVirtual<uint8_t*>(SchemaDataPtr);

  constant_list_ptr_ =
      memory_->TranslateVirtual<xe::be<uint32_t>*>(ConstantListPtr);

  url_offsets_ptr_ =
      memory_->TranslateVirtual<xe::be<uint16_t>*>(UrlOffsetsPtr);

  url_data_ptr_ = memory_->TranslateVirtual<char*>(UrlDataPtr);
}

bool SchemaInMemory::GetSchemaEntry(uint16_t schema_index,
                                    SCHEMA_TABLE_ENTRY* schema_entry_ptr) {
  if (schema_index >= Header.SchemaTableEntries) {
    return false;
  }

  std::memcpy(schema_entry_ptr, schema_table_entry_ptr_ + schema_index,
              sizeof(SCHEMA_TABLE_ENTRY));

  return true;
}

bool SchemaInMemory::GetSchemaDataFromEntry(
    SCHEMA_TABLE_ENTRY* schema_entry_ptr, bool request,
    std::span<uint8_t>* schema_data) {
  uint32_t request_schema_offset = 0;
  uint32_t schema_data_size = 0;

  if (request) {
    schema_data_size = schema_entry_ptr->RequestSchemaSize;
    request_schema_offset = schema_entry_ptr->RequestSchemaOffset;
  } else {
    schema_data_size = schema_entry_ptr->ResponseSchemaSize;
    request_schema_offset = schema_entry_ptr->ResponseSchemaOffset;
  }

  uint8_t* buffer_ptr = &schema_data_ptr_[request_schema_offset];

  *schema_data = std::span<uint8_t>(buffer_ptr, schema_data_size);

  return true;
}

bool SchemaInMemory::EndianBufferBind(BASE_ENDIAN_BUFFER* base,
                                      std::span<uint8_t>& buffer) {
  base->BufferPtr = memory_->HostToGuestVirtual(std::to_address(buffer.data()));
  base->BufferSize = static_cast<uint32_t>(buffer.size());
  base->AvailableSize = static_cast<uint32_t>(buffer.size());
  base->ConsumedSize = 0;
  base->ReverseEndian = 1;

  return true;
}

bool SchemaInMemory::XLookupSchemaIndexFromOrdinal(
    uint16_t ordinal, uint16_t* schema_index) const {
  auto CompareOrdinal = [](const void* e1, const void* e2) -> int {
    const auto* element1 = reinterpret_cast<const xe::be<uint16_t>*>(e1);
    const auto* element2 = reinterpret_cast<const xe::be<uint16_t>*>(e2);

    return *element1 - *element2;
  };

  ordinal = xe::byte_swap(ordinal);

  const ORDINAL_TO_INDEX* found = static_cast<ORDINAL_TO_INDEX*>(
      std::bsearch(&ordinal, ordinal_to_index_ptr_, Header.SchemaTableEntries,
                   4, CompareOrdinal));

  if (!found) {
    return false;
  }

  *schema_index = found->Index;

  return true;
}

bool SchemaInMemory::LookupUrlFromTable(uint16_t url_index,
                                        std::string_view* url_ptr) {
  if (url_index > Header.UrlTableSize) {
    return false;
  }

  uint16_t url_offset = url_offsets_ptr_[url_index];

  if (url_offset > Header.UrlTableDataSize) {
    return false;
  }

  *url_ptr = std::string_view(url_data_ptr_ + url_offset);

  return true;
};

bool SchemaInMemory::LookupConstantFromTable(uint16_t constant_index,
                                             uint32_t* value_ptr) {
  if (constant_index > Header.ConstantsTableSize) {
    return false;
  }

  assert_false(Header.ConstantSize != 4);

  *value_ptr = constant_list_ptr_[constant_index];

  return true;
}

std::string_view SchemaInMemory::GetTaskUrl(uint16_t schema_index) {
  SCHEMA_TABLE_ENTRY schema_entry = {};
  std::string_view url = "";

  GetSchemaEntry(schema_index, &schema_entry);
  LookupUrlFromTable(schema_entry.RequestUrlIndex, &url);

  return url;
}

std::string SchemaInMemory::GetOrdinalFunctionName(uint16_t ordinal) {
  return named_ordinals_.contains(ordinal) ? named_ordinals_.at(ordinal) : "";
};

std::pair<uint16_t, uint16_t> SchemaInMemory::SchemaVersion() const {
  return std::pair<uint16_t, uint16_t>(Header.SchemaVersionMajor,
                                       Header.SchemaVersionMinor);
}

std::string SchemaInMemory::SchemaVersionString() const {
  const auto version = SchemaVersion();
  return fmt::format("{}.{}", version.first, version.second);
}

void SchemaInMemory::PrettyPrintSchemaTables() const {
  const auto schema_table_entries = std::vector<SCHEMA_TABLE_ENTRY>(
      schema_table_entry_ptr_,
      schema_table_entry_ptr_ + Header.SchemaTableEntries);

  std::string schema_entries_details =
      fmt::format("\nSchema Version: {}\n", SchemaVersionString());

  for (uint32_t i = 0; const auto& entry : schema_table_entries) {
    std::string entry_details = fmt::format(
        "Schema entry {}: Request [{:08X}, {:08X}, {:08X}], Response [{:08X}, "
        "{:08X}, "
        "{:08X}], Service: {} ({})\n",
        i, entry.RequestSchemaSize.get(), entry.ResponseSchemaSize.get(),
        entry.RequestSchemaOffset.get(), entry.ResponseSchemaOffset.get(),
        entry.MaxRequestAggregateSize.get(),
        entry.MaxResponseAggregateSize.get(), entry.ServiceIDIndex.get(),
        entry.RequestUrlIndex.get());

    schema_entries_details.append(entry_details);

    i++;
  }

  XELOGI(schema_entries_details);
}

void SchemaInMemory::PrettyPrintUrls() {
  const auto schema_table_entries = std::vector<SCHEMA_TABLE_ENTRY>(
      schema_table_entry_ptr_,
      schema_table_entry_ptr_ + Header.SchemaTableEntries);

  std::string pretty_urls =
      fmt::format("\nSchema Version: {}\n", SchemaVersionString());

  for (const auto& entry : schema_table_entries) {
    std::string_view url = "";

    LookupUrlFromTable(entry.RequestUrlIndex, &url);

    pretty_urls.append(fmt::format("URL: {}\n", url));
  }

  XELOGI(pretty_urls);
}

void SchemaInMemory::PrettyPrintUrlsWithSchemaIndex() {
  const auto ordinal_to_index_entries = std::vector<ORDINAL_TO_INDEX>(
      ordinal_to_index_ptr_, ordinal_to_index_ptr_ + Header.SchemaTableEntries);

  std::string pretty_urls =
      fmt::format("\nSchema Version: {}\n", SchemaVersionString());

  for (const auto& ordinal_entry : ordinal_to_index_entries) {
    SCHEMA_TABLE_ENTRY schema_entry = {};
    std::string_view url = "";

    GetSchemaEntry(ordinal_entry.Index.get(), &schema_entry);
    LookupUrlFromTable(schema_entry.RequestUrlIndex, &url);

    pretty_urls.append(fmt::format("Schema Index: {:04X}, URL: {}\n",
                                   ordinal_entry.Index.get(), url));
  }

  XELOGI(pretty_urls);
}

void SchemaInMemory::PrettyPrintOrdinalToIndex() const {
  const auto ordinal_to_index_entries = std::vector<ORDINAL_TO_INDEX>(
      ordinal_to_index_ptr_, ordinal_to_index_ptr_ + Header.SchemaTableEntries);

  std::string pretty_schema_table =
      fmt::format("\nSchema Version: {}\n", SchemaVersionString());

  for (const auto& entry : ordinal_to_index_entries) {
    pretty_schema_table.append(fmt::format("Ordinal: {:04X}, Index: {:04X}\n",
                                           entry.Ordinal.get(),
                                           entry.Index.get()));
  }

  XELOGI(pretty_schema_table);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
