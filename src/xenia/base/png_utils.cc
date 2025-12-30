/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <span>

#include "xenia/base/filesystem.h"
#include "xenia/base/png_utils.h"

#include "third_party/stb/stb_image.h"

namespace xe {

bool IsDataPngImage(std::span<const uint8_t> png_data) {
  const uint32_t start_offset = 1;
  const uint32_t magic_size = 3;
  const uint32_t size = start_offset + magic_size;

  if (png_data.empty() || png_data.size() < size) {
    return false;
  }

  const auto magic = png_data.subspan(start_offset, magic_size);
  const std::string png_magic =
      std::string(reinterpret_cast<const char*>(magic.data()), magic_size);

  if (png_magic != "PNG") {
    return false;
  }

  return true;
}

bool IsFilePngImage(const std::filesystem::path& file_path) {
  FILE* file = xe::filesystem::OpenFile(file_path, "rb");
  if (!file) {
    return false;
  }

  constexpr uint8_t magic_size = 4;
  char magic[magic_size];
  if (fread(&magic, 1, magic_size, file) != magic_size) {
    return false;
  }

  fclose(file);

  if (magic[1] != 'P' || magic[2] != 'N' || magic[3] != 'G') {
    return false;
  }

  return true;
}

std::pair<uint16_t, uint16_t> GetImageResolution(
    const std::filesystem::path& file_path) {
  FILE* file = xe::filesystem::OpenFile(file_path, "rb");
  if (!file) {
    return {};
  }

  int width, height, channels;
  if (!stbi_info_from_file(file, &width, &height, &channels)) {
    return {};
  }

  fclose(file);
  return {width, height};
}

std::vector<uint8_t> ReadPngFromFile(const std::filesystem::path& file_path) {
  FILE* file = xe::filesystem::OpenFile(file_path, "rb");
  if (!file) {
    return {};
  }

  const auto file_size = std::filesystem::file_size(file_path);
  std::vector<uint8_t> data(file_size);
  fread(data.data(), 1, file_size, file);
  fclose(file);

  return data;
}

}  // namespace xe
