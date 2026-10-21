/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_GUEST_STRING_WRITER_H_
#define XENIA_KERNEL_UTIL_GUEST_STRING_WRITER_H_

#include <cstdint>
#include <string_view>

#include "xenia/base/string_util.h"
#include "xenia/memory.h"

namespace xe {
namespace kernel {
namespace util {

// Sequentially writes null terminated byte swapped strings into guest memory.
class GuestStringWriter {
 public:
  struct GuestString {
    uint32_t address;
    uint16_t length;  // Includes null terminator.
  };

  GuestStringWriter(Memory* memory, char16_t* buffer)
      : memory_(memory), buffer_(buffer) {}

  GuestString Write(std::u16string_view string) {
    const uint16_t length = static_cast<uint16_t>(string.size() + 1);

    xe::string_util::copy_and_swap_truncating(buffer_, string, length);

    const GuestString guest_string = {memory_->HostToGuestVirtual(buffer_),
                                      length};
    buffer_ += length;

    return guest_string;
  }

 private:
  Memory* memory_;
  char16_t* buffer_;
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_GUEST_STRING_WRITER_H_
