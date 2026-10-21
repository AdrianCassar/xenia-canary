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

#include <future>
#include <mutex>

#include "xenia/base/byte_order.h"
#include "xenia/kernel/xnet.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

enum XNADDR_STATUS : uint32_t {
  XNADDR_PENDING = 0x00000000,
  XNADDR_NONE = 0x00000001,
  XNADDR_ETHERNET = 0x00000002,
  XNADDR_STATIC = 0x00000004,
  XNADDR_DHCP = 0x00000008,
  XNADDR_PPPOE = 0x00000010,
  XNADDR_GATEWAY = 0x00000020,
  XNADDR_DNS = 0x00000040,
  XNADDR_ONLINE = 0x00000080,
  XNADDR_TROUBLESHOOT = 0x00008000
};

enum ETHERNET_STATUS : uint32_t {
  ETHERNET_LINK_NONE = 0x00000000,
  ETHERNET_LINK_ACTIVE = 0x00000001,
  ETHERNET_LINK_100MBPS = 0x00000002,
  ETHERNET_LINK_10MBPS = 0x00000004,
  ETHERNET_LINK_FULL_DUPLEX = 0x00000008,
  ETHERNET_LINK_HALF_DUPLEX = 0x00000010,
  ETHERNET_LINK_WIRELESS = 0x00000020
};

enum class X_INTERNET_SCHEME : uint32_t { HTTP = 1, HTTPS = 2 };

enum class X_URL_COMPONENTS {
  Full,
  Protocol,
  Username,
  Password,
  Host,
  Port,
  Path,
  Query
};

struct XHTTP_URL_COMPONENTS {
  xe::be<uint32_t> struct_size;
  xe::be<uint32_t> scheme_ptr;
  xe::be<uint32_t> scheme_length;
  xe::be<uint32_t> scheme;
  xe::be<uint32_t> host_name_ptr;
  xe::be<uint32_t> host_name_length;
  xe::be<uint16_t> port;
  xe::be<uint32_t> user_name_ptr;
  xe::be<uint32_t> user_name_length;
  xe::be<uint32_t> password_ptr;
  xe::be<uint32_t> password_length;
  xe::be<uint32_t> url_path_ptr;
  xe::be<uint32_t> url_path_length;
  xe::be<uint32_t> extra_info_ptr;
  xe::be<uint32_t> extra_info_length;
};
static_assert_size(XHTTP_URL_COMPONENTS, 0x3C);

struct XAM_RELYING_PARTY_TOKEN {
  xe::be<uint32_t> reserved;
  xe::be<uint32_t> length;
  xe::be<uint32_t> token_data_ptr;  // uint8_t*
};
static_assert_size(XAM_RELYING_PARTY_TOKEN, 0xC);

enum XNET_QOS {
  XNET_QOS_LISTEN_ENABLE = 0x01,
  XNET_QOS_LISTEN_DISABLE = 0x02,
  XNET_QOS_LISTEN_SET_DATA = 0x04,
  XNET_QOS_LISTEN_SET_BITSPERSEC = 0x08,
  XNET_QOS_LISTEN_RELEASE = 0x10
};

enum XNET_CONNECT {
  XNET_CONNECT_STATUS_IDLE = 0x00,
  XNET_CONNECT_STATUS_PENDING = 0x01,
  XNET_CONNECT_STATUS_CONNECTED = 0x02,
  XNET_CONNECT_STATUS_LOST = 0x03,
};

enum XNET_STARTUP {
  XNET_STARTUP_BYPASS_SECURITY = 0x01,
  XNET_STARTUP_ALLOCATE_MAX_DGRAM_SOCKETS = 0x02,
  XNET_STARTUP_ALLOCATE_MAX_STREAM_SOCKETS = 0x04,
  XNET_STARTUP_DISABLE_PEER_ENCRYPTION = 0x08,
};

enum XNET_XNQOSINFO {
  XNET_XNQOSINFO_COMPLETE = 0x01,
  XNET_XNQOSINFO_TARGET_CONTACTED = 0x02,
  XNET_XNQOSINFO_TARGET_DISABLED = 0x04,
  XNET_XNQOSINFO_DATA_RECEIVED = 0x08,
  XNET_XNQOSINFO_PARTIAL_COMPLETE = 0x10
};

// XNetGetBroadcastVersionStatus
enum XNET_BROADCAST_VERSION_STATUS {
  XNET_BROADCAST_VERSION_OLDER = 0x01,
  XNET_BROADCAST_VERSION_NEWER = 0x02,
};

// https://github.com/G91/TitanOffLine/blob/1e692d9bb9dfac386d08045ccdadf4ae3227bb5e/xkelib/xam/xamNet.h
enum {
  XNCALLER_INVALID = 0x0,
  XNCALLER_TITLE = 0x1,
  XNCALLER_SYSAPP = 0x2,
  XNCALLER_XBDM = 0x3,
  XNCALLER_TEST = 0x4,
  NUM_XNCALLER_TYPES = 0x4,
};

struct XNDNS {
  xe::be<int32_t> status;
  xe::be<uint32_t> cina;
  in_addr aina[8];
};
static_assert_size(XNDNS, 0x28);

struct XNQOSINFO {
  uint8_t flags;
  uint8_t reserved;
  xe::be<uint16_t> probes_xmit;
  xe::be<uint16_t> probes_recv;
  xe::be<uint16_t> data_len;
  xe::be<uint32_t> data_ptr;
  xe::be<uint16_t> rtt_min_in_msecs;
  xe::be<uint16_t> rtt_med_in_msecs;
  xe::be<uint32_t> up_bits_per_sec;
  xe::be<uint32_t> down_bits_per_sec;
};
static_assert_size(XNQOSINFO, 0x18);

struct XNQOS {
  xe::be<uint32_t> count;
  xe::be<uint32_t> count_pending;
  XNQOSINFO info[1];
};
static_assert_size(XNQOS, 0x20);

struct X_WSADATA {
  xe::be<uint16_t> version;
  xe::be<uint16_t> version_high;
  char description[256 + 1];
  char system_status[128 + 1];
  xe::be<uint16_t> max_sockets;
  xe::be<uint16_t> max_udpdg;
  xe::be<uint32_t> vendor_info_ptr;
};
static_assert_size(X_WSADATA, 0x190);

// https://github.com/joolswills/mameox/blob/master/MAMEoX/Sources/xbox_Network.cpp#L136
struct XNetStartupParams {
  uint8_t cfgSizeOfStruct;
  uint8_t cfgFlags = 0;
  uint8_t cfgSockMaxDgramSockets = 8;
  uint8_t cfgSockMaxStreamSockets = 32;
  uint8_t cfgSockDefaultRecvBufsizeInK = 16;
  uint8_t cfgSockDefaultSendBufsizeInK = 16;
  uint8_t cfgKeyRegMax = 8;
  uint8_t cfgSecRegMax = 32;
  uint8_t cfgQosDataLimitDiv4 = 64;
  uint8_t cfgQosProbeTimeoutInSeconds = 2;
  uint8_t cfgQosProbeRetries = 3;
  uint8_t cfgQosSrvMaxSimultaneousResponses = 8;
  uint8_t cfgQosPairWaitTimeInSeconds = 2;
};
static_assert_size(XNetStartupParams, 0xD);

// Internal settings passed from XAuthStartup.
// The first two fields are the title's XAUTH_SETTINGS, followed by a 512000
// byte buffer the title allocates for XAM.
// XAuthShutdown frees it only if XampXAuthShutdown reports no remaining
// references and XampXAuthGetTitleBuffer returns it.
struct XAMP_XAUTH_SETTINGS {
  xe::be<uint32_t> SizeOfStruct;  // sizeof(XAUTH_SETTINGS)
  xe::be<uint32_t> Flags;
  xe::be<uint32_t> title_buffer_ptr;
};
static_assert_size(XAMP_XAUTH_SETTINGS, 0xC);

// sizeof(XAUTH_SETTINGS) from the title's view.
constexpr uint32_t kXAuthSettingsSize = 0x8;

struct XNQOSLISTENSTATS {
  uint32_t size_of_struct;
  uint32_t requests_received_count;
  uint32_t probes_received_count;
  uint32_t slots_full_discards_count;
  uint32_t data_replies_sent_count;
  uint32_t data_reply_bytes_sent;
  uint32_t probe_replies_sent_count;
};
static_assert_size(XNQOSLISTENSTATS, 0x1C);

struct X_TIMEVAL {
  xe::be<uint32_t> tv_sec;
  xe::be<uint32_t> tv_usec;
};
static_assert_size(X_TIMEVAL, 0x8);

struct x_fd_set {
  xe::be<uint32_t> fd_count;
  xe::be<uint32_t> fd_array[X_FD_SETSIZE];
};

// Very hacky
bool EXPLICIT_XBOXLIVE_KEY = false;

std::vector<std::future<int32_t>> upnp_actions_;

std::map<uint32_t, std::stop_source> qos_lookup_threads;
std::mutex qos_lookup_mutex;

std::map<uint32_t, std::stop_source> dns_lookup_threads;
std::mutex dns_lookup_mutex;

std::atomic<bool> initialized_xnet_ = false;

std::atomic<uint32_t> winsock_reference_count_ = 0;

X_BACKGROUND_DOWNLOAD_MODE download_mode_ = X_BACKGROUND_DOWNLOAD_MODE::AUTO;

static void CleanupUPnPActions();

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_NET_H_
