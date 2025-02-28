/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XSTORAGE_ENUMERATE_JSON_H_
#define XENIA_KERNEL_XSTORAGE_ENUMERATE_JSON_H_

#include <vector>

#include "xenia/kernel/json/base_object_json.h"

namespace xe {
namespace kernel {
class XStorageFileInfoObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  XStorageFileInfoObjectJSON();
  virtual ~XStorageFileInfoObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const uint32_t& TitleID() const { return title_id_; }
  void TitleID(const uint32_t& title_id) { title_id_ = title_id; }

  const uint32_t& TitleVersion() const { return title_version_; }
  void TitleVersion(const uint32_t& title_version) {
    title_version_ = title_version;
  }

  const uint64_t& OwnerPUID() const { return owner_puid_; }
  void OwnerPUID(const uint64_t& owner_puid) { owner_puid_ = owner_puid; }

  const uint32_t& CountryID() const { return country_id_; }
  void CountryID(const uint32_t& country_id) { country_id_ = country_id; }

  const uint32_t& ContentType() const { return content_type_; }
  void ContentType(const uint32_t& content_type) {
    content_type_ = content_type;
  }

  const uint32_t& StorageSize() const { return storage_size_; }
  void StorageSize(const uint32_t& storage_size) {
    storage_size_ = storage_size;
  }

  const uint32_t& InstalledSize() const { return installed_size_; }
  void InstalledSize(const uint32_t& installed_size) {
    installed_size_ = installed_size;
  }

  const double& Created() const { return ft_created_; }
  void Created(const double& ft_created) { ft_created_ = ft_created; }

  const double& LastModified() const { return ft_last_modified_; }
  void LastModified(const double& ft_last_modified) {
    ft_last_modified_ = ft_last_modified;
  }

  const std::string& FilePath() const { return file_path_; }
  void FilePath(const std::string& file_path) { file_path_ = file_path; }

 private:
  uint32_t title_id_;
  uint32_t title_version_;
  uint64_t owner_puid_;
  uint32_t country_id_;
  uint32_t content_type_;
  uint32_t storage_size_;
  uint32_t installed_size_;
  double ft_created_;
  double ft_last_modified_;
  std::string file_path_;
};

class XStorageFilesInfoObjectJSON : public BaseObjectJSON {
 public:
  using BaseObjectJSON::Deserialize;
  using BaseObjectJSON::Serialize;

  XStorageFilesInfoObjectJSON();
  virtual ~XStorageFilesInfoObjectJSON();

  virtual bool Deserialize(const rapidjson::Value& obj);
  virtual bool Serialize(
      rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const;

  const std::vector<XStorageFileInfoObjectJSON>& Items() const { return items; }

  const uint32_t& TotalNumItems() const { return total_num_items_; }
  void TotalNumItems(const uint32_t& total_num_items) {
    total_num_items_ = total_num_items;
  }

  const uint32_t& MaxItems() const { return max_items_; }
  void MaxItems(const uint32_t& max_items) { max_items_ = max_items; }

 private:
  std::vector<XStorageFileInfoObjectJSON> items;
  uint32_t total_num_items_;
  uint32_t max_items_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XSTORAGE_ENUMERATE_JSON_H_