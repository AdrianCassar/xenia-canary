/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/string_util.h"
#include "xenia/kernel/json/xstorage_file_info_object_json.h"
#include "xenia/kernel/util/net_utils.h"

namespace xe {
namespace kernel {
XStorageFileInfoObjectJSON::XStorageFileInfoObjectJSON()
    : title_id_(0),
      title_version_(0),
      owner_puid_(0),
      country_id_(0),
      content_type_(0),
      storage_size_(0),
      installed_size_(0),
      ft_created_(0),
      ft_last_modified_(0),
      file_path_("") {}

XStorageFileInfoObjectJSON::~XStorageFileInfoObjectJSON() {}

bool XStorageFileInfoObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("title_id")) {
    TitleID(obj["title_id"].GetUint());
  }

  if (obj.HasMember("title_version")) {
    TitleVersion(obj["title_version"].GetUint());
  }

  if (obj.HasMember("owner_puid")) {
    OwnerPUID(obj["owner_puid"].GetUint64());
  }

  if (obj.HasMember("country_id")) {
    CountryID(obj["country_id"].GetUint());
  }

  if (obj.HasMember("content_type")) {
    ContentType(obj["content_type"].GetUint());
  }

  if (obj.HasMember("storage_size")) {
    StorageSize(obj["storage_size"].GetUint());
  }

  if (obj.HasMember("installed_size")) {
    InstalledSize(obj["installed_size"].GetUint());
  }

  if (obj.HasMember("created")) {
    Created(obj["created"].GetDouble());
  }

  if (obj.HasMember("last_modified")) {
    LastModified(obj["last_modified"].GetDouble());
  }

  if (obj.HasMember("path")) {
    FilePath(obj["path"].GetString());
  }

  return true;
}

bool XStorageFileInfoObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  return false;
}

XStorageFilesInfoObjectJSON::XStorageFilesInfoObjectJSON()
    : items({}), total_num_items_(0), max_items_(0) {}

XStorageFilesInfoObjectJSON::~XStorageFilesInfoObjectJSON() {}

bool XStorageFilesInfoObjectJSON::Deserialize(const rapidjson::Value& obj) {
  if (obj.HasMember("total_num_items")) {
    TotalNumItems(obj["total_num_items"].GetUint());
  }

  if (obj.HasMember("items")) {
    if (obj["items"].IsArray()) {
      for (const auto& FileInfoObj : obj["items"].GetArray()) {
        XStorageFileInfoObjectJSON* FileInfo = new XStorageFileInfoObjectJSON();
        FileInfo->Deserialize(FileInfoObj.GetObj());

        items.push_back(*FileInfo);
      }
    }
  }

  return true;
}

bool XStorageFilesInfoObjectJSON::Serialize(
    rapidjson::PrettyWriter<rapidjson::StringBuffer>* writer) const {
  writer->StartObject();

  writer->String("MaxItems");
  writer->Uint(MaxItems());

  writer->EndObject();

  return true;
}

}  // namespace kernel
}  // namespace xe