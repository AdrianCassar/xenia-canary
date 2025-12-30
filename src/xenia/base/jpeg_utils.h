/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_JPEG_UTILS_H_
#define XENIA_BASE_JPEG_UTILS_H_

#include <filesystem>
#include <span>

namespace xe {

bool IsDataJpegImage(std::span<const uint8_t> jpeg_data);
bool IsDataJpegJfifFormat(std::span<const uint8_t> jpeg_data);
bool IsDataJpegExifFormat(std::span<const uint8_t> jpeg_data);

bool IsFileJpegImage(const std::filesystem::path& file_path);

}  // namespace xe

#endif  // XENIA_BASE_JPEG_UTILS_H_
