/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/null_file.h"

#include "xenia/base/memory.h"
#include "xenia/vfs/devices/null_device.h"
#include "xenia/vfs/devices/null_entry.h"

namespace xe {
namespace vfs {

NullFile::NullFile(uint32_t file_access, NullEntry* entry)
    : File(file_access, entry) {}

NullFile::~NullFile() = default;

void NullFile::Destroy() { delete this; }

X_STATUS NullFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                            size_t* out_bytes_read) {
  if (!(file_access_ & FileAccess::kFileReadData)) {
    return X_STATUS_ACCESS_DENIED;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS NullFile::WriteSync(std::span<const uint8_t> buffer,
                             size_t byte_offset, size_t* out_bytes_written) {
  if (!(file_access_ &
        (FileAccess::kFileWriteData | FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  // XapiFormatFATVolumeEx writes a FATX header at offset 0 when formatting a
  // cache partition. The game then checks NtQueryVolumeInformationFile's
  // SectorsPerAllocationUnit * BytesPerSector against its requested cluster
  // size (e.g. Titanfall uses 0x1000), so report back what was written.
  if (byte_offset == 0 && buffer.size() >= 12 &&
      xe::load_and_swap<uint32_t>(buffer.data()) == 0x58544146 /* XTAF */) {
    uint32_t sectors_per_cluster =
        xe::load_and_swap<uint32_t>(buffer.data() + 8);
    if (sectors_per_cluster) {
      static_cast<NullDevice*>(entry_->device())
          ->set_sectors_per_allocation_unit(sectors_per_cluster);
    }
  }

  return X_STATUS_SUCCESS;
}

X_STATUS NullFile::SetLength(size_t length) {
  if (!(file_access_ & FileAccess::kFileWriteData)) {
    return X_STATUS_ACCESS_DENIED;
  }

  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
