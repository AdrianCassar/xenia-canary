/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_NET_H_
#define XENIA_KERNEL_XAM_XAM_NET_H_

#include "xenia/xbox.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WinSock2.h>

namespace xe {
namespace kernel {
namespace xam {

uint32_t xeXOnlineGetNatType();
in_addr getOnlineIp();
const unsigned char* getMacAddress();
uint16_t getPort();
uint64_t getMachineId();
void clearXnaddrCache();
uint16_t GetMappedConnectPort(uint16_t port);
uint16_t GetMappedBindPort(uint16_t port);
std::string GetApiAddress();

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_NET_H_
