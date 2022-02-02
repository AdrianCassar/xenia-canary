/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include "xenia/patcher/patcher.h"

#include "xenia/base/logging.h"

namespace xe {
namespace patcher {

Patcher::Patcher(const std::filesystem::path patches_root) {
  is_any_patch_applied_ = false;
  patch_db = new PatchDB(patches_root);
}

Patcher::~Patcher() {}

void Patcher::ApplyPatchesForTitle(Memory* memory, const uint32_t title_id,
                                   const uint64_t hash) {
  const auto title_patches = patch_db->GetTitlePatches(title_id, hash);

  for (const PatchFileEntry& patchFile : title_patches) {
    for (const PatchInfoEntry& patchEntry : patchFile.patch_info) {
      if (!patchEntry.is_enabled) {
        continue;
      }
      XELOGE("Patcher: Applying patch for: {}({:08X}) - {}",
             patchFile.title_name, patchFile.title_id, patchEntry.patch_name);
      ApplyPatch(memory, &patchEntry);
    }
  }
}

void Patcher::ApplyPatch(Memory* memory, const PatchInfoEntry* patch) {
  for (const PatchDataEntry& patch_data_entry : patch->patch_data) {
    uint32_t old_address_protect = 0;
    auto address = memory->TranslateVirtual(patch_data_entry.memory_address_);
    auto heap = memory->LookupHeap(patch_data_entry.memory_address_);
    if (!heap) {
      continue;
    }

    heap->QueryProtect(patch_data_entry.memory_address_, &old_address_protect);

    heap->Protect(patch_data_entry.memory_address_,
                  patch_data_entry.alloc_size_,
                  kMemoryProtectRead | kMemoryProtectWrite);

    switch (patch_data_entry.alloc_size_) {
      case 1:
        xe::store_and_swap(address, uint8_t(patch_data_entry.new_value_));
        break;
      case 2:
        xe::store_and_swap(address, uint16_t(patch_data_entry.new_value_));
        break;
      case 4:
        xe::store_and_swap(address, uint32_t(patch_data_entry.new_value_));
        break;
      case 8:
        xe::store_and_swap(address, uint64_t(patch_data_entry.new_value_));
        break;
      default:
        XELOGE("Patcher: Unsupported patch allocation size - {}",
               patch_data_entry.alloc_size_);
        break;
    }
    // Restore previous protection
    heap->Protect(patch_data_entry.memory_address_,
                  patch_data_entry.alloc_size_, old_address_protect);

    is_any_patch_applied_ = true;
  }
}

}  // namespace patcher
}  // namespace xe
