/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/base/jpeg_utils.h"

namespace xe {

bool IsDataJpegImage(std::span<const uint8_t> jpeg_data) {
  return IsDataJpegJfifFormat(jpeg_data) || IsDataJpegExifFormat(jpeg_data);
}

bool IsDataJpegJfifFormat(std::span<const uint8_t> jpeg_data) {
  const uint32_t start_offset = 6;
  const uint32_t magic_size = 4;
  const uint32_t size = start_offset + magic_size;

  if (jpeg_data.empty() || jpeg_data.size() < size) {
    return false;
  }

  const auto magic = jpeg_data.subspan(start_offset, magic_size);
  const std::string jfif_magic =
      std::string(reinterpret_cast<const char*>(magic.data()), magic_size);

  if (jfif_magic != "JFIF") {
    return false;
  }

  return true;
}

bool IsDataJpegExifFormat(std::span<const uint8_t> jpeg_data) {
  const uint32_t start_offset = 6;
  const uint32_t magic_size = 4;
  const uint32_t size = start_offset + magic_size;

  if (jpeg_data.empty() || jpeg_data.size() < size) {
    return false;
  }

  const auto magic = jpeg_data.subspan(start_offset, magic_size);
  const std::string exif_magic =
      std::string(reinterpret_cast<const char*>(magic.data()), magic_size);

  if (exif_magic != "Exif") {
    return false;
  }

  return true;
}

bool IsFileJpegImage(const std::filesystem::path& file_path) {
  FILE* file = xe::filesystem::OpenFile(file_path, "rb");
  if (!file) {
    return false;
  }

  const uint32_t start_offset = 6;
  const uint32_t magic_size = 4;
  const uint32_t size = start_offset + magic_size;

  uint8_t magic[size];
  if (fread(&magic, 1, size, file) != size) {
    return false;
  }

  fclose(file);

  return IsDataJpegImage({magic});
}

}  // namespace xe
