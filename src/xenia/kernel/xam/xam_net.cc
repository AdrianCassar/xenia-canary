/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <random>

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/net_utils.h"
#include "xenia/kernel/util/network_adapter_manager.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_net.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_error.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_modules.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xsocket.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <comutil.h>
#include <wrl/client.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include "xenia/kernel/XLiveAPI.h"

DEFINE_bool(xhttp, false, "Toggles XHTTP.", "Live");

DECLARE_bool(logging);

DECLARE_bool(log_mask_ips);

DECLARE_int32(network_mode);

DECLARE_bool(xlink_kai_systemlink_hack);

enum XNET_QOS {
  LISTEN_ENABLE = 0x01,
  LISTEN_DISABLE = 0x02,
  LISTEN_SET_DATA = 0x04,
  LISTEN_SET_BITSPERSEC = 0x08,
  XLISTEN_RELEASE = 0x10
};

enum XNET_CONNECT {
  STATUS_IDLE = 0x00,
  XNET_CONNECT_STATUS_PENDING = 0x01,
  STATUS_CONNECTED = 0x02,
  STATUS_LOST = 0x03,
};

enum XNET_STARTUP {
  BYPASS_SECURITY = 0x01,
  ALLOCATE_MAX_DGRAM_SOCKETS = 0x02,
  ALLOCATE_MAX_STREAM_SOCKETS = 0x04,
  DISABLE_PEER_ENCRYPTION = 0x08,
};

enum XNET_XNQOSINFO {
  COMPLETE = 0x01,
  TARGET_CONTACTED = 0x02,
  TARGET_DISABLED = 0x04,
  DATA_RECEIVED = 0x08,
  PARTIAL_COMPLETE = 0x10
};

// XNetGetBroadcastVersionStatus
enum VERSION {
  OLDER = 0x01,
  NEWER = 0x02,
};

namespace xe {
namespace kernel {
namespace xam {

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

struct XAUTH_SETTINGS {
  xe::be<uint32_t> SizeOfStruct;
  xe::be<uint32_t> Flags;
};

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

// Initialize sockaddr to its default state
static void InitalizeSockaddr(XSOCKADDR_IN* sockaddr_ptr) {
  if (sockaddr_ptr) {
    std::memset(sockaddr_ptr, 0, sizeof(XSOCKADDR_IN));
    sockaddr_ptr->address_family = XSocket::AddressFamily::X_AF_INET;
  }
}

XNetStartupParams xnet_startup_params{};

void Update_XNetStartupParams(XNetStartupParams& dest,
                              const XNetStartupParams& src) {
  uint8_t* dest_ptr = reinterpret_cast<uint8_t*>(&dest);
  const uint8_t* src_ptr = reinterpret_cast<const uint8_t*>(&src);

  size_t size = sizeof(XNetStartupParams);

  for (size_t i = 0; i < size; i++) {
    if (src_ptr[i] != 0 && dest_ptr[i] != src_ptr[i]) {
      dest_ptr[i] = src_ptr[i];
    }
  }
}

dword_result_t NetDll_XNetStartup_entry(dword_t caller,
                                        pointer_t<XNetStartupParams> params) {
  if (XLiveAPI::GetInitState() != XLiveAPI::InitState::Pending) {
    return 0;
  }

  XLiveAPI::Init();

  if (params) {
    assert_true(params->cfgSizeOfStruct == sizeof(XNetStartupParams));
    Update_XNetStartupParams(xnet_startup_params, *params);

    switch (xnet_startup_params.cfgFlags) {
      case BYPASS_SECURITY:
        XELOGI("XNetStartup BYPASS_SECURITY");
        break;
      case ALLOCATE_MAX_DGRAM_SOCKETS:
        XELOGI("XNetStartup ALLOCATE_MAX_DGRAM_SOCKETS");
        break;
      case ALLOCATE_MAX_STREAM_SOCKETS:
        XELOGI("XNetStartup ALLOCATE_MAX_STREAM_SOCKETS");
        break;
      case DISABLE_PEER_ENCRYPTION:
        XELOGI("XNetStartup DISABLE_PEER_ENCRYPTION");
        break;
      default:
        break;
    }
  }

  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  /*
  if (!xam->xnet()) {
    auto xnet = new XNet(kernel_state());
    xnet->Initialize();

    xam->set_xnet(xnet);
  }
  */

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetStartup, kNetworking, kImplemented);

// https://github.com/jogolden/testdev/blob/master/xkelib/syssock.h#L46
dword_result_t NetDll_XNetStartupEx_entry(dword_t caller,
                                          pointer_t<XNetStartupParams> params,
                                          dword_t versionReq) {
  // versionReq
  // MW3, Ghosts: 0x20501400

  return NetDll_XNetStartup_entry(caller, params);
}
DECLARE_XAM_EXPORT1(NetDll_XNetStartupEx, kNetworking, kImplemented);

dword_result_t NetDll_XNetCleanup_entry(dword_t caller, lpvoid_t params) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  // auto xnet = xam->xnet();
  // xam->set_xnet(nullptr);

  // TODO: Shut down and delete.
  // delete xnet;

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetCleanup, kNetworking, kImplemented);

dword_result_t XNetLogonGetMachineID_entry(lpqword_t machine_id_ptr) {
  *machine_id_ptr = GetLocalMachineId(GetConsoleMacAddress());

  // if (XLiveAPI::GetInitState() != XLiveAPI::InitState::Success) {
  //   *machine_id_ptr = 0;
  //   return X_ERROR_LOGON_NOT_LOGGED_ON;
  // }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNetLogonGetMachineID, kNetworking, kImplemented);

dword_result_t XNetLogonGetTitleID_entry(dword_t caller, lpvoid_t params) {
  return kernel_state()->title_id();
}
DECLARE_XAM_EXPORT1(XNetLogonGetTitleID, kNetworking, kImplemented);

dword_result_t NetDll_XnpLogonGetStatus_entry(
    dword_t caller, pointer_t<SGADDR> security_gateway_ptr, lpdword_t reason) {
  if (security_gateway_ptr) {
    security_gateway_ptr.Zero();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XnpLogonGetStatus, kNetworking, kStub);

dword_result_t XamGetToken_entry(dword_t user_index, lpstring_t url_ptr,
                                 dword_t url_size,
                                 pointer_t<XAM_RELYING_PARTY_TOKEN> token_ptr,
                                 pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (token_ptr) {
    token_ptr.Zero();
  }

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = X_ERROR_SUCCESS;
    length = 0;

    // Failing causes the least crashes.
    extended_error = X_E_FAIL;

    XThread::SetLastError(X_ERROR_FUNCTION_FAILED);
    return X_ERROR_FUNCTION_FAILED;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = run(extended_error, length);

    return result;
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamGetToken, kNetworking, kStub);

void XamFreeToken_entry(pointer_t<XAM_RELYING_PARTY_TOKEN> token_ptr) {
  if (token_ptr) {
    kernel_memory()->SystemHeapFree(token_ptr.guest_address());
  }
}
DECLARE_XAM_EXPORT1(XamFreeToken, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetOpt_entry(dword_t caller, dword_t option_id,
                                       lpvoid_t buffer_ptr,
                                       lpdword_t buffer_size) {
  assert_true(caller == 1);
  switch (option_id) {
    case 1:
      if (*buffer_size < sizeof(XNetStartupParams)) {
        *buffer_size = sizeof(XNetStartupParams);
        return uint32_t(X_WSAError::X_WSAEMSGSIZE);
      }
      std::memcpy(buffer_ptr, &xnet_startup_params, sizeof(XNetStartupParams));
      return 0;
    default:
      XELOGE("NetDll_XNetGetOpt: option {} unimplemented", option_id.value());
      return uint32_t(X_WSAError::X_WSAEINVAL);
  }
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetOpt, kNetworking, kSketchy);

dword_result_t NetDll_XNetRandom_entry(dword_t caller, lpvoid_t buffer_ptr,
                                       dword_t length) {
  uint8_t* buffer_data_ptr = buffer_ptr.as<uint8_t*>();

  if (buffer_data_ptr == nullptr || length == 0) {
    return X_ERROR_SUCCESS;
  }

  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<int> dist(0,
                                          std::numeric_limits<uint8_t>::max());

  std::generate(buffer_data_ptr, buffer_data_ptr + length,
                [&]() { return static_cast<uint8_t>(dist(gen)); });

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetRandom, kNetworking, kImplemented);

dword_result_t NetDll_WSAStartup_entry(dword_t caller, word_t version,
                                       pointer_t<X_WSADATA> data_ptr) {
  // NetDll_WSAStartup is called multiple times?
  XELOGI("NetDll_WSAStartup");

  XLiveAPI::Init();

  // TODO(benvanik): abstraction layer needed.
  int ret = 0;

#ifdef XE_PLATFORM_WIN32
  WSADATA wsaData = {};

  ret = WSAStartup(version, &wsaData);

  // 415607E1 provides version 0 which returns WSAVERNOTSUPPORTED on Windows.
  // However console does not support such error, instead it returns 0.
  if (ret == WSAVERNOTSUPPORTED) {
    ret = X_ERROR_SUCCESS;
  }

  if (ret != X_ERROR_SUCCESS) {
    assert_always();
    ret = X_ERROR_SUCCESS;
  }
#endif

  if (data_ptr) {
    data_ptr.Zero();

#ifdef XE_PLATFORM_WIN32
    data_ptr->version = wsaData.wVersion;
    data_ptr->version_high = wsaData.wHighVersion;
#else
    data_ptr->version = version.value();
    data_ptr->version_high = 0x0202;
#endif
  }

  // DEBUG
  /*
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  if (!xam->xnet()) {
    auto xnet = new XNet(kernel_state());
    xnet->Initialize();

    xam->set_xnet(xnet);
  }
  */

  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_WSAStartup, kNetworking, kImplemented);

dword_result_t NetDll_WSAStartupEx_entry(dword_t caller, word_t version,
                                         pointer_t<X_WSADATA> data_ptr,
                                         dword_t versionReq) {
  return NetDll_WSAStartup_entry(caller, version, data_ptr);
}
DECLARE_XAM_EXPORT1(NetDll_WSAStartupEx, kNetworking, kImplemented);

dword_result_t NetDll_WSACleanup_entry(dword_t caller) {
  // This does nothing. Xenia needs WSA running.
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_WSACleanup, kNetworking, kImplemented);

// Instead of using dedicated storage for WSA error like on OS.
// Xbox shares space between normal error codes and WSA errors.
// This under the hood returns directly value received from RtlGetLastError.
dword_result_t NetDll_WSAGetLastError_entry() {
  uint32_t last_error = XThread::GetLastError();
  XELOGD("NetDll_WSAGetLastError: {}", last_error);
  return last_error;
}
DECLARE_XAM_EXPORT1(NetDll_WSAGetLastError, kNetworking, kImplemented);

dword_result_t NetDll_WSARecvFrom_entry(
    dword_t caller, dword_t socket_handle, pointer_t<XWSABUF> buffers,
    dword_t num_buffers, lpdword_t num_bytes_recv_ptr, lpdword_t flags_ptr,
    pointer_t<XSOCKADDR_IN> from_ptr, lpdword_t fromlen_ptr,
    pointer_t<XWSAOVERLAPPED> overlapped_ptr, lpvoid_t completion_routine_ptr) {
  InitalizeSockaddr(from_ptr);

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int ret =
      socket->WSARecvFrom(buffers, num_buffers, num_bytes_recv_ptr, flags_ptr,
                          from_ptr, fromlen_ptr, overlapped_ptr);
  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
  } else if (ret >= 0 && !cvars::log_mask_ips && from_ptr) {
    XELOGI("NetDll_WSARecvFrom: Received {} bytes from: {}:{}({})",
           static_cast<uint32_t>(*num_bytes_recv_ptr),
           ip_to_string(from_ptr->address_ip), from_ptr->address_port.get(),
           socket->GetProtocolUPnPString());
  }

  return ret;
}
DECLARE_XAM_EXPORT2(NetDll_WSARecvFrom, kNetworking, kImplemented,
                    kHighFrequency);

dword_result_t NetDll_WSAGetOverlappedResult_entry(
    dword_t caller, dword_t socket_handle,
    pointer_t<XWSAOVERLAPPED> overlapped_ptr, lpdword_t bytes_transferred,
    dword_t wait, lpdword_t flags_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return 0;
  }

  bool ret = socket->WSAGetOverlappedResult(overlapped_ptr, bytes_transferred,
                                            wait, flags_ptr);
  if (!ret) {
    XThread::SetLastError(socket->GetLastWSAError());
  }
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_WSAGetOverlappedResult, kNetworking, kImplemented);

// If the socket is a VDP socket, buffer 0 is the game data length, and buffer 1
// is the unencrypted game data.
dword_result_t NetDll_WSASendTo_entry(
    dword_t caller, dword_t socket_handle, pointer_t<XWSABUF> buffers,
    dword_t num_buffers, lpdword_t num_bytes_sent, dword_t flags,
    pointer_t<XSOCKADDR_IN> to_ptr, dword_t to_len,
    pointer_t<XWSAOVERLAPPED> overlapped, lpvoid_t completion_routine) {
  assert(!overlapped);
  assert(!completion_routine);

  if (overlapped) {
    XELOGW("NetDll_WSASendTo: overlapped!");
  }

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  // Our sockets implementation doesn't support multiple buffers, so we need
  // to combine the buffers the game has given us!
  std::vector<uint8_t> combined_buffer_mem;
  uint32_t combined_buffer_size = 0;
  uint32_t combined_buffer_offset = 0;
  for (uint32_t i = 0; i < num_buffers; i++) {
    combined_buffer_size += buffers[i].len;
    combined_buffer_mem.resize(combined_buffer_size);
    uint8_t* combined_buffer = combined_buffer_mem.data();

    std::memcpy(combined_buffer + combined_buffer_offset,
                kernel_memory()->TranslateVirtual(buffers[i].buf_ptr),
                buffers[i].len);
    combined_buffer_offset += buffers[i].len;
  }

  const int result = socket->SendTo(
      combined_buffer_mem.data(), combined_buffer_size, flags, to_ptr, to_len);

  if (result == -1) {
    XThread::SetLastError(socket->GetLastWSAError());
    return result;
  } else if (result != -1 && to_ptr && !cvars::log_mask_ips) {
    XELOGI("NetDll_WSASendTo: Send {} bytes to: {}:{}({})", result,
           ip_to_string(to_ptr->address_ip), to_ptr->address_port.get(),
           socket->GetProtocolUPnPString());
  }

  if (num_bytes_sent && !overlapped) {
    *num_bytes_sent = result;
  }
  // TODO: Instantly complete overlapped

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_WSASendTo, kNetworking, kImplemented);

dword_result_t NetDll_WSAWaitForMultipleEvents_entry(dword_t num_events,
                                                     lpdword_t events,
                                                     dword_t wait_all,
                                                     dword_t timeout,
                                                     dword_t alertable) {
  if (num_events > 64) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSA_INVALID_PARAMETER));
    return -1;
  }

  uint64_t timeout_wait = 0;
  const bool wait_all_ = !static_cast<bool>(wait_all);

  if (timeout != -1) {
    timeout_wait = -10000LL * static_cast<uint64_t>(timeout);
  }

  X_STATUS result = 0;

  while (true) {
    result = xboxkrnl::xeNtWaitForMultipleObjectsEx(
        num_events, events, wait_all_, 1, alertable, &timeout_wait);

    if (XFAILED(result)) {
      uint32_t error = xboxkrnl::xeRtlNtStatusToDosError(result);
      XThread::SetLastError(error);
      return -1;
    }

    if (!alertable || result != X_STATUS_ALERTED) {
      return result;
    }
  }
}
DECLARE_XAM_EXPORT2(NetDll_WSAWaitForMultipleEvents, kNetworking, kImplemented,
                    kBlocking);

dword_result_t NetDll_WSACreateEvent_entry() {
  XEvent* ev = new XEvent(kernel_state());
  ev->Initialize(true, false);
  return ev->handle();
}
DECLARE_XAM_EXPORT1(NetDll_WSACreateEvent, kNetworking, kImplemented);

dword_result_t NetDll_WSACloseEvent_entry(dword_t event_handle) {
  X_STATUS result = kernel_state()->object_table()->ReleaseHandle(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::xeRtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_WSACloseEvent, kNetworking, kImplemented);

dword_result_t NetDll_WSAResetEvent_entry(dword_t event_handle) {
  X_STATUS result = xboxkrnl::xeNtClearEvent(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::xeRtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_WSAResetEvent, kNetworking, kImplemented);

dword_result_t NetDll_WSASetEvent_entry(dword_t event_handle) {
  X_STATUS result = xboxkrnl::xeNtSetEvent(event_handle, nullptr);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::xeRtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_WSASetEvent, kNetworking, kImplemented);

dword_result_t XamQueryLiveHiveA_entry(lpstring_t name, lpvoid_t out_buf,
                                       dword_t out_size,
                                       dword_t type /* guess */) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamQueryLiveHiveA, kNone, kStub);

// Sets the console IP address.
dword_result_t NetDll_XNetGetTitleXnAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> XnAddr_ptr) {
  XnAddr_ptr.Zero();

  // Wait for NetDll_WSAStartup or XNetStartup to setup XLiveAPI.
  if (XLiveAPI::GetInitState() == XLiveAPI::InitState::Pending) {
    // Call of Duty 2 - does not call XNetStartup or WSAStartup before
    // XNetGetTitleXnAddr.
    XLiveAPI::Init();

    return XNADDR_STATUS::XNADDR_PENDING;
  }

  uint8_t status = 0;

  if (cvars::network_mode == NETWORK_MODE::OFFLINE) {
    status |= XNADDR_STATUS::XNADDR_ETHERNET | XNADDR_STATUS::XNADDR_STATIC;
  }

  if (cvars::network_mode != NETWORK_MODE::OFFLINE) {
    status |= XNADDR_STATUS::XNADDR_ETHERNET | XNADDR_STATUS::XNADDR_STATIC |
              XNADDR_STATUS::XNADDR_GATEWAY | XNADDR_STATUS::XNADDR_DNS;
  }

  if (cvars::network_mode == NETWORK_MODE::XBOXLIVE) {
    status |= XNADDR_STATUS::XNADDR_ONLINE;
  }

  XLiveAPI::IpGetConsoleXnAddr(XnAddr_ptr);

  // TODO(gibbed): A proper mac address.
  // RakNet's 360 version appears to depend on abEnet to create "random" 64-bit
  // numbers. A zero value will cause RakPeer::Startup to fail. This causes
  // 58411436 to crash on startup.
  // The 360-specific code is scrubbed from the RakNet repo, but there's still
  // traces of what it's doing which match the game code.
  // https://github.com/facebookarchive/RakNet/blob/master/Source/RakPeer.cpp#L382
  // https://github.com/facebookarchive/RakNet/blob/master/Source/RakPeer.cpp#L4527
  // https://github.com/facebookarchive/RakNet/blob/master/Source/RakPeer.cpp#L4467
  // "Mac address is a poor solution because you can't have multiple connections
  // from the same system"

  return status;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetTitleXnAddr, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetDebugXnAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> addr_ptr) {
  addr_ptr.Zero();

  // XNADDR_NONE causes caller to gracefully return.
  return XNADDR_STATUS::XNADDR_NONE;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetDebugXnAddr, kNetworking, kStub);

dword_result_t NetDll_XNetGetXnAddrPlatform_entry(dword_t caller,
                                                  pointer_t<XNADDR> addr_ptr,
                                                  lpdword_t platform_type) {
  // 58411457 filters session search based on platform type

  *platform_type = addr_ptr->abOnline.platform_type;

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetXnAddrPlatform, kNetworking, kStub);

dword_result_t NetDll_XNetXnAddrToMachineId_entry(dword_t caller,
                                                  pointer_t<XNADDR> addr_ptr,
                                                  lpqword_t id_ptr) {
  id_ptr.Zero();

  if (!addr_ptr->inaOnline.s_addr || !addr_ptr->wPortOnline) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  const MacAddress mac = MacAddress(addr_ptr->abEnet);
  const uint64_t machine_id = GetMachineId(mac.to_uint64());

  *id_ptr = machine_id;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetXnAddrToMachineId, kNetworking, kImplemented);

dword_result_t NetDll_XNetUnregisterInAddr_entry(dword_t caller, dword_t addr) {
  XELOGI("NetDll_XNetUnregisterInAddr({:08X})",
         cvars::log_mask_ips ? 0 : addr.value());

  // return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetUnregisterInAddr, kNetworking, kStub);

dword_result_t NetDll_XNetConnect_entry(dword_t caller, dword_t addr) {
  XELOGI("XNetConnect({:08X})", cvars::log_mask_ips ? 0 : addr.value());

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetConnect, kNetworking, kStub);

dword_result_t NetDll_XNetGetConnectStatus_entry(dword_t caller, dword_t addr) {
  XELOGI("XNetGetConnectStatus({:08X})",
         cvars::log_mask_ips ? 0 : addr.value());

  return STATUS_CONNECTED;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetConnectStatus, kNetworking, kStub);

dword_result_t NetDll_XNetServerToInAddr_entry(dword_t caller,
                                               dword_t server_addr,
                                               dword_t service_id,
                                               pointer_t<in_addr> pina) {
  XELOGI("XNetServerToInAddr");

  if (XLiveAPI::GetInitState() != XLiveAPI::InitState::Success) {
    return static_cast<uint32_t>(X_WSAError::X_WSANOTINITIALISED);
  }

  if (!server_addr || !service_id) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  pina->s_addr = htonl(server_addr);

  XELOGI("Server IP: {}", ip_to_string(*pina));

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetServerToInAddr, kNetworking, kImplemented);

dword_result_t NetDll_XNetInAddrToServer_entry(dword_t caller,
                                               dword_t server_addr,
                                               pointer_t<in_addr> pina) {
  XELOGI("XNetInAddrToServer");

  if (!server_addr) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  pina->s_addr = htonl(server_addr);

  XELOGI("Server IP: {}", ip_to_string(*pina));

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetInAddrToServer, kNetworking, kSketchy);

dword_result_t NetDll_XNetTsAddrToInAddr_entry(dword_t caller,
                                               pointer_t<TSADDR> tsaddr_ptr,
                                               dword_t service_id,
                                               pointer_t<XNKID> xnkid_ptr,
                                               pointer_t<in_addr> ina_ptr) {
  XELOGI("XNetTsAddrToInAddr");

  if (!tsaddr_ptr || !service_id || !xnkid_ptr || !ina_ptr) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  // Use XNKID to lookup security association?

  *ina_ptr = tsaddr_ptr->inaOnline;

  IsValidXNKID(xnkid_ptr->as_uintBE64());

  XELOGI("Server IP: {}, Service ID: {:08X}", ip_to_string(*ina_ptr),
         static_cast<uint32_t>(service_id));

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetTsAddrToInAddr, kNetworking, kSketchy);

dword_result_t NetDll_XNetInAddrToString_entry(dword_t caller, dword_t ina,
                                               lpstring_t string_out,
                                               dword_t string_size) {
  in_addr addr = in_addr{};
  addr.s_addr = ntohl(ina);

  strncpy(string_out, ip_to_string(addr).c_str(), string_size);

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetInAddrToString, kNetworking, kImplemented);

// This converts a XNet address to an in_addr. The in_addr is used for
// subsequent socket calls (like a handle to a XNet address)
dword_result_t NetDll_XNetXnAddrToInAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> xn_addr,
                                               pointer_t<XNKID> xid,
                                               pointer_t<in_addr> in_addr) {
  if (in_addr) {
    in_addr->s_addr = 0;
  }

  if (GetConsoleMacAddress() == MacAddress(xn_addr->abEnet)) {
    XELOGI("Resolving XNetXnAddrToInAddr to LOOPBACK!");
    in_addr->s_addr = xe::byte_swap(LOOPBACK);

    return X_ERROR_SUCCESS;
  }

  if (kernel_state()
          ->emulator()
          ->GetNetworkAdapterManager()
          ->IsConnectedToLAN()) {
    in_addr->s_addr = xn_addr->ina.s_addr;
  }

  if (XLiveAPI::IsConnectedToServer()) {
    in_addr->s_addr = xn_addr->inaOnline.s_addr;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetXnAddrToInAddr, kNetworking, kSketchy);

dword_result_t NetDll_XNetInAddrToXnAddr_entry(dword_t caller, dword_t in_addr,
                                               pointer_t<XNADDR> xn_addr,
                                               pointer_t<XNKID> xid_ptr) {
  if (xn_addr) {
    memset(xn_addr, 0, sizeof(XNADDR));
  }

  if (xid_ptr) {
    memset(xid_ptr, 0, sizeof(XNKID));
  }

  if (in_addr == BROADCAST) {
    XELOGI("Resolving XnAddr via BROADCAST!");
  }

  if (in_addr == LOOPBACK || in_addr == BROADCAST) {
    XELOGI("Resolving XnAddr via LOOPBACK!");
    XLiveAPI::IpGetConsoleXnAddr(xn_addr);

    return X_STATUS_SUCCESS;
  }

  xn_addr->ina.s_addr = ntohl(in_addr);
  xn_addr->inaOnline.s_addr = ntohl(in_addr);
  xn_addr->wPortOnline = XLiveAPI::GetPlayerPort();

  // Find cached online IP?
  if (XLiveAPI::macAddressCache.find(xn_addr->inaOnline.s_addr) ==
      XLiveAPI::macAddressCache.end()) {
    const auto player = XLiveAPI::FindPlayer(ip_to_string(xn_addr->inaOnline));

    // FIXME
    if (!XLiveAPI::systemlink_id || EXPLICIT_XBOXLIVE_KEY) {
      IsValidXNKID(player->SessionID());

      if (player->SessionID()) {
        XLiveAPI::sessionIdCache[xn_addr->inaOnline.s_addr] =
            player->SessionID();
      }

      if (player->MacAddress()) {
        XLiveAPI::macAddressCache[xn_addr->inaOnline.s_addr] =
            player->MacAddress();
      }
    } else {
      // Remote mac missing for systemlink!
      // 415607E1 (CoD 3) checks for this!
      //
      // If we're connected to server then use it
      if (player->MacAddress()) {
        XLiveAPI::macAddressCache[xn_addr->inaOnline.s_addr] =
            player->MacAddress();
      }
    }
  }

  const uint64_t remote_mac =
      XLiveAPI::macAddressCache[xn_addr->inaOnline.s_addr];
  MacAddress mac = MacAddress(static_cast<uint64_t>(0));

  if (remote_mac) {
    mac = MacAddress(XLiveAPI::macAddressCache[xn_addr->inaOnline.s_addr]);
  }

  std::memcpy(xn_addr->abEnet, mac.raw(), MacAddress::MacAddressSize);

  if (xid_ptr != nullptr) {
    XNKID* sessionId_ptr = kernel_memory()->TranslateVirtual<XNKID*>(xid_ptr);
    xe::be<uint64_t> session_id = 0;

    // FIXME
    if (XLiveAPI::systemlink_id) {
      session_id = XLiveAPI::systemlink_id;
    } else {
      session_id = XLiveAPI::sessionIdCache[xn_addr->inaOnline.s_addr];
    }

    memcpy(sessionId_ptr, &session_id, sizeof(uint64_t));

    IsValidXNKID(sessionId_ptr->as_uintBE64());
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetInAddrToXnAddr, kNetworking, kImplemented);

// https://www.google.com/patents/WO2008112448A1?cl=en
// Reserves a port for use by system link
dword_result_t NetDll_XNetSetSystemLinkPort_entry(dword_t caller, word_t port) {
  if (!xboxkrnl::XexCheckExecutablePrivilege(
          XEX_PRIVILEGE_CROSSPLATFORM_SYSTEM_LINK)) {
    XELOGW("Title not allowed to set System Link port!");
    return static_cast<uint32_t>(X_WSAError::X_WSAEACCES);
  }

  // XNET_SYSTEMLINK_PORT = port;

  XELOGI("XNetSetSystemLinkPort: {}", port.value());

  return static_cast<uint32_t>(X_WSAError::X_WSAEADDRINUSE);
}
DECLARE_XAM_EXPORT1(NetDll_XNetSetSystemLinkPort, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetSystemLinkPort_entry(dword_t caller,
                                                  lpword_t port) {
  if (!xboxkrnl::XexCheckExecutablePrivilege(
          XEX_PRIVILEGE_CROSSPLATFORM_SYSTEM_LINK)) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEACCES);
  }

  *port = XNET_SYSTEMLINK_PORT;

  XELOGI("XNetGetSystemLinkPort: {}", static_cast<uint16_t>(*port));

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetSystemLinkPort, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetBroadcastVersionStatus_entry(dword_t caller,
                                                          dword_t reset) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetBroadcastVersionStatus, kNetworking, kStub);

dword_result_t NetDll_XNetGetEthernetLinkStatus_entry(dword_t caller) {
  if (cvars::network_mode == NETWORK_MODE::OFFLINE) {
    return ETHERNET_STATUS::ETHERNET_LINK_NONE;
  }

  return ETHERNET_STATUS::ETHERNET_LINK_ACTIVE |
         ETHERNET_STATUS::ETHERNET_LINK_100MBPS |
         ETHERNET_STATUS::ETHERNET_LINK_FULL_DUPLEX;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetEthernetLinkStatus, kNetworking,
                    kImplemented);

dword_result_t NetDll_XNetDnsLookup_entry(dword_t caller, lpstring_t host,
                                          dword_t event_handle,
                                          lpdword_t pdns) {
  XELOGI("DNS Lookup: {}", std::string(host));

  if (pdns) {
    hostent* ent = gethostbyname(host);

    auto dns_guest = kernel_memory()->SystemHeapAlloc(sizeof(XNDNS));
    auto dns = kernel_memory()->TranslateVirtual<XNDNS*>(dns_guest);

    if (ent == nullptr) {
      XELOGI("DNS Lookup: Failed");
#ifdef XE_PLATFORM_WIN32
      dns->status = WSAGetLastError();
#else
      dns->status = (int32_t)X_WSAError::X_WSAENETDOWN;
#endif
    } else if (ent->h_addrtype != AF_INET) {
      dns->status = (int32_t)X_WSAError::X_WSANO_DATA;
    } else {
      XELOGI("DNS Lookup: Success");
      dns->status = 0;
      int i = 0;
      while (ent->h_addr_list[i] != nullptr && i < 8) {
        dns->aina[i] = *reinterpret_cast<in_addr*>(ent->h_addr_list[i]);
        i++;
      }
      dns->cina = i;
    }

    *pdns = dns_guest;
  }

  xboxkrnl::xeNtSetEvent(event_handle, nullptr);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetDnsLookup, kNetworking, kImplemented);

dword_result_t NetDll_XNetDnsRelease_entry(dword_t caller,
                                           pointer_t<XNDNS> dns) {
  if (!dns) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  kernel_memory()->SystemHeapFree(dns.guest_address());
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetDnsRelease, kNetworking, kStub);

dword_result_t NetDll_XNetQosServiceLookup_entry(dword_t caller, dword_t flags,
                                                 dword_t event_handle,
                                                 lpdword_t qos_ptr) {
  XELOGI("XNetQosServiceLookup({}, {:08X}, {:08X}, {:08X})", caller.value(),
         flags.value(), event_handle.value(), qos_ptr.guest_address());

  if (!qos_ptr) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  XNQOS* qos = nullptr;

  if (qos_ptr) {
    const uint32_t qos_guest = kernel_memory()->SystemHeapAlloc(sizeof(XNQOS));
    qos = kernel_memory()->TranslateVirtual<XNQOS*>(qos_guest);

    qos->count = 1;
    qos->count_pending = 0;

    qos->info[0].probes_xmit = 4;
    qos->info[0].probes_recv = 4;
    qos->info[0].data_len = 0;
    qos->info[0].data_ptr = 0;
    qos->info[0].rtt_min_in_msecs = 10;
    qos->info[0].rtt_med_in_msecs = 10;
    qos->info[0].up_bits_per_sec = static_cast<uint32_t>(1024_KiB);
    qos->info[0].down_bits_per_sec = static_cast<uint32_t>(1024_KiB);
    qos->info[0].flags =
        XNET_XNQOSINFO::COMPLETE | XNET_XNQOSINFO::TARGET_CONTACTED;

    // If COMPLETE, TARGET_CONTACTED or PARTIAL_COMPLETE flag is set then set
    // event.
    if (qos->count > 0) {
      xboxkrnl::xeNtSetEvent(event_handle, nullptr);
    }

    *qos_ptr = qos_guest;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosServiceLookup, kNetworking, kStub);

dword_result_t NetDll_XNetQosRelease_entry(dword_t caller,
                                           pointer_t<XNQOS> qos_ptr) {
  if (!qos_ptr) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEINVAL);
  }

  for (uint32_t i = 0; i < qos_ptr->count; i++) {
    if (qos_ptr[i].info->data_ptr) {
      kernel_memory()->SystemHeapFree(qos_ptr[i].info->data_ptr);
    }
  }

  kernel_memory()->SystemHeapFree(qos_ptr.guest_address());
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosRelease, kNetworking, kStub);

// Create a socket and listen for incoming probes via player port and filter by
// session id
dword_result_t NetDll_XNetQosListen_entry(
    dword_t caller, pointer_t<XNKID> sessionId, pointer_t<uint32_t> data,
    dword_t data_size, dword_t bits_per_second, dword_t flags) {
  XELOGI("XNetQosListen({:08X}, {:016X}, {:016X}, {}, {:08X}, {:08X})",
         caller.value(), sessionId.host_address(), data.host_address(),
         data_size.value(), bits_per_second.value(), flags.value());

  if (flags & LISTEN_ENABLE) {
    XELOGI("XNetQosListen LISTEN_ENABLE");
  }

  if (flags & LISTEN_DISABLE) {
    XELOGI("XNetQosListen LISTEN_DISABLE");
  }

  if (flags & LISTEN_SET_BITSPERSEC) {
    XELOGI("XNetQosListen LISTEN_SET_BITSPERSEC");
  }

  if (flags & XLISTEN_RELEASE) {
    XELOGI("XNetQosListen XLISTEN_RELEASE");
  }

  if (data_size <= 0) {
    return X_ERROR_SUCCESS;
  }

  if (data_size > (uint32_t)(xnet_startup_params.cfgQosDataLimitDiv4 * 4)) {
    assert_always();
  }

  if (data == nullptr) {
    return X_ERROR_SUCCESS;
  }

  const uint64_t session_id = sessionId->as_uintBE64();

  IsValidXNKID(session_id);

  if (flags & LISTEN_SET_DATA) {
    std::vector<uint8_t> qos_buffer(data_size);
    memcpy(qos_buffer.data(), data, data_size);

    if (XLiveAPI::UpdateQoSCache(session_id, qos_buffer)) {
      XELOGI("XNetQosListen LISTEN_SET_DATA");

      auto run = [](uint64_t sessionId, std::vector<uint8_t> qosData) {
        XLiveAPI::QoSPost(sessionId, qosData.data(), qosData.size());
      };

      std::thread qos_thread(run, session_id, qos_buffer);
      qos_thread.detach();
    }
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosListen, kNetworking, kSketchy);

dword_result_t NetDll_XNetQosLookup_entry(
    dword_t caller, dword_t num_remote_consoles,
    pointer_t<uint32_t> remote_addresses_PtrsPtr,
    pointer_t<uint32_t> sessionId_PtrsPtr,
    pointer_t<uint32_t> remote_keys_PtrsPtr, dword_t num_gateways,
    pointer_t<uint32_t> gateways_PtrsPtr,
    pointer_t<uint32_t> service_ids_PtrsPtr, dword_t probes_count,
    dword_t bits_per_second, dword_t flags, dword_t event_handle,
    lpdword_t qos_ptr) {
  if (!sessionId_PtrsPtr || !qos_ptr) {
    return static_cast<uint32_t>(X_WSAError::X_WSAEACCES);
  }

  std::vector<XNADDR> remote_addresses{};
  std::vector<XNKID> session_ids{};
  std::vector<XNKEY> remote_keys{};
  std::vector<in_addr> security_gateways{};
  std::vector<uint32_t> service_ids{};

  if (num_remote_consoles) {
    const xe::be<uint32_t>* session_id_ptrs =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(sessionId_PtrsPtr);

    const auto session_id_ptr_array = std::vector<xe::be<uint32_t>>(
        session_id_ptrs, session_id_ptrs + num_remote_consoles);

    for (uint32_t i = 0; i < num_remote_consoles; i++) {
      XNKID session_id =
          *kernel_memory()->TranslateVirtual<XNKID*>(session_id_ptr_array[i]);

      session_ids.push_back(session_id);
    }
  }

  if (remote_keys_PtrsPtr) {
    const xe::be<uint32_t>* remote_keys_ptrs =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            remote_keys_PtrsPtr);

    auto remote_keys_ptr_array = std::vector<xe::be<uint32_t>>(
        remote_keys_ptrs, remote_keys_ptrs + num_remote_consoles);

    for (uint32_t i = 0; i < num_remote_consoles; i++) {
      const XNKEY remote_key =
          *kernel_memory()->TranslateVirtual<XNKEY*>(remote_keys_ptr_array[i]);

      remote_keys.push_back(remote_key);
    }
  }

  if (remote_addresses_PtrsPtr) {
    const xe::be<uint32_t>* remote_addresses_ptrs =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            remote_addresses_PtrsPtr);

    auto remote_addresses_ptr_array = std::vector<xe::be<uint32_t>>(
        remote_addresses_ptrs, remote_addresses_ptrs + num_remote_consoles);

    for (uint32_t i = 0; i < num_remote_consoles; i++) {
      const XNADDR remote_address =
          *kernel_memory()->TranslateVirtual<XNADDR*>(remote_addresses_ptrs[i]);

      remote_addresses.push_back(remote_address);
    }
  }

  if (service_ids_PtrsPtr) {
    const xe::be<uint32_t>* service_ids_ptrs =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            service_ids_PtrsPtr);

    auto service_ids_ptr_array = std::vector<xe::be<uint32_t>>(
        service_ids_ptrs, service_ids_ptrs + num_remote_consoles);

    for (uint32_t i = 0; i < num_remote_consoles; i++) {
      const uint32_t service_id = *kernel_memory()->TranslateVirtual<uint32_t*>(
          service_ids_ptr_array[i]);

      service_ids.push_back(service_id);
    }
  }

  if (gateways_PtrsPtr) {
    const xe::be<uint32_t>* gateways_ptrs =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(gateways_PtrsPtr);

    auto gateways_ptr_array = std::vector<xe::be<uint32_t>>(
        gateways_ptrs, gateways_ptrs + num_gateways);

    for (uint32_t i = 0; i < num_gateways; i++) {
      const in_addr gateway_key =
          *kernel_memory()->TranslateVirtual<in_addr*>(gateways_ptr_array[i]);

      security_gateways.push_back(gateway_key);
    }
  }

  // const uint32_t count = num_remote_consoles + num_gateways;
  const uint32_t count = num_remote_consoles;

  uint32_t countOffset = 0;

  // Fake QoS count to fix GoW 3
  if (kernel_state()->title_id() == 0x4D5308AB) {
    countOffset = 1;
  }

  const uint32_t size =
      sizeof(XNQOS) + (sizeof(XNQOSINFO) * (count - 1) + countOffset);
  const uint32_t qos_guest = kernel_memory()->SystemHeapAlloc(size);
  XNQOS* qos = kernel_memory()->TranslateVirtual<XNQOS*>(qos_guest);

  /*
   GoW 3 - TU 0
   If qos->count is not equal to num_remote_consoles then it will join
   sessions otherwise repeats QoS lookup

   L4D2
   Removes session if QoS failed therefore adding fake entry must be valid to
   prevent removal of valid session
  */

  qos->count_pending = count;
  qos->count = count + countOffset;

  const uint32_t probes = qos->count - countOffset;

  for (uint32_t i = 0; i < probes; i++) {
    uint64_t session_id = session_ids[i].as_uintBE64();
    response_data chunk = XLiveAPI::QoSGet(session_id);

    if (chunk.http_code == HTTP_STATUS_CODE::HTTP_OK ||
        chunk.http_code == HTTP_STATUS_CODE::HTTP_NO_CONTENT) {
      qos->info[i].data_ptr = 0;
      qos->info[i].data_len = 0;
      qos->info[i].flags =
          XNET_XNQOSINFO::COMPLETE | XNET_XNQOSINFO::TARGET_CONTACTED;

      if (chunk.size) {
        uint32_t data_ptr =
            kernel_memory()->SystemHeapAlloc(static_cast<uint32_t>(chunk.size));
        uint32_t* data = kernel_memory()->TranslateVirtual<uint32_t*>(data_ptr);

        memcpy(data, chunk.response, chunk.size);

        qos->info[i].data_ptr = data_ptr;
        qos->info[i].data_len = static_cast<uint16_t>(chunk.size);
        qos->info[i].flags |= XNET_XNQOSINFO::DATA_RECEIVED;
      }

      // 415607DD and 415607D4 expect probes count, otherwise spams lookup.
      qos->info[i].probes_xmit = probes_count.value();
      qos->info[i].probes_recv = probes_count.value();
      qos->info[i].rtt_min_in_msecs = 10;
      qos->info[i].rtt_med_in_msecs = 10;
      qos->info[i].up_bits_per_sec = static_cast<uint32_t>(1024_KiB);
      qos->info[i].down_bits_per_sec = static_cast<uint32_t>(1024_KiB);

      qos->count_pending =
          std::max(static_cast<int>(qos->count_pending - 1), 0);
    }

    // Prevent L4D2 removing info[probes - 1] entry
    if (i == (probes - 1)) {
      memcpy(&qos->info[probes], &qos->info[i], sizeof(XNQOSINFO));
    }

    *qos_ptr = qos_guest;
  }

  // If COMPLETE or TARGET_CONTACTED flag is then set event.
  if (qos->count > 0) {
    xboxkrnl::xeNtSetEvent(event_handle, nullptr);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosLookup, kNetworking, kImplemented);

dword_result_t NetDll_XNetQosGetListenStats_entry(
    dword_t caller, pointer_t<XNKID> xnkid_ptr,
    pointer_t<XNQOSLISTENSTATS> qos_stats_ptr) {
  XELOGI("XNetQosGetListenStats({:08X}, {:08X}, {:08X})", caller.value(),
         xnkid_ptr.guest_address(), qos_stats_ptr.guest_address());

  if (qos_stats_ptr) {
    qos_stats_ptr->requests_received_count = 1;
    qos_stats_ptr->probes_received_count = 1;
    qos_stats_ptr->slots_full_discards_count = 1;
    qos_stats_ptr->data_replies_sent_count = 1;
    qos_stats_ptr->data_reply_bytes_sent = 1;
    qos_stats_ptr->probe_replies_sent_count = 1;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosGetListenStats, kNetworking, kImplemented);

dword_result_t XamGetServiceEndpoint_entry(
    lpstring_t service_name_ptr, lpstring_t service_endpoint_ptr,
    dword_t service_endpoint_len, pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (!service_name_ptr || !service_endpoint_ptr || !service_endpoint_len) {
    return X_ERROR_INVALID_PARAMETER;
  }

  const std::string service_name = service_name_ptr.value();

  std::string service_endpoint =
      fmt::format("{}{}", XLiveAPI::GetApiAddress(), service_name);
  const std::string fallback_service_endpoint =
      fmt::format("http://xbox.com/{}", service_name);

  CURLU* url = curl_url();
  CURLUcode rc = curl_url_set(url, CURLUPART_URL, service_endpoint.c_str(), 0);

  // Check if endpoint has scheme, protocol expected for XHttpCrackUrl.
  if (rc == CURLUE_BAD_SCHEME) {
    service_endpoint = fmt::format("http://{}", service_endpoint);
  }

  curl_url_cleanup(url);

  auto run = [=](uint32_t& extended_error, uint32_t& length) {
    extended_error = X_ERROR_SUCCESS;
    length = 0;

    std::memset(service_endpoint_ptr, 0, service_endpoint_len);

    // 41560914 uses endpoint length of 64
    if (service_endpoint_len < service_endpoint.size() + 1) {
      if (service_endpoint_len < fallback_service_endpoint.size() + 1) {
        extended_error = X_ERROR_INSUFFICIENT_BUFFER;
        return X_ERROR_FUNCTION_FAILED;
      } else {
        strcpy(service_endpoint_ptr, fallback_service_endpoint.c_str());
        XELOGI("XamGetServiceEndpoint: {}", fallback_service_endpoint.c_str());
      }
    } else {
      strcpy(service_endpoint_ptr, service_endpoint.c_str());
      XELOGI("XamGetServiceEndpoint: {}", service_endpoint.c_str());
    }

    return X_ERROR_SUCCESS;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    X_RESULT result = run(extended_error, length);

    return result == X_ERROR_SUCCESS ? result : extended_error;
  }

  kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
  return X_ERROR_IO_PENDING;
}
DECLARE_XAM_EXPORT1(XamGetServiceEndpoint, kNetworking, kSketchy);

dword_result_t XampXAuthStartup_entry(pointer_t<XAUTH_SETTINGS> setttings) {
  if (setttings->SizeOfStruct != sizeof(XAUTH_SETTINGS)) {
    return 0x80158401;
  }

  if (cvars::network_mode != NETWORK_MODE::XBOXLIVE) {
    return 0x80158406;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XampXAuthStartup, kNetworking, kStub);

void XampXAuthShutdown_entry(lpdword_t unkn) {
  *unkn = 1;

  // Causes a call to XampXAuthGetTitleBuffer
  // *unkn = 0;
}
DECLARE_XAM_EXPORT1(XampXAuthShutdown, kNetworking, kStub);

dword_result_t XampXAuthGetTitleBuffer_entry() {
  // pointer? - non-zero causes crash
  return 0;
}
DECLARE_XAM_EXPORT1(XampXAuthGetTitleBuffer, kNetworking, kStub);

dword_result_t NetDll_XHttpStartup_entry(dword_t caller, dword_t reserved,
                                         dword_t reserved_ptr) {
  // Console returns 1 even without network access

  if (kernel_state()->emulator()->title_id() == kDashboardID) {
    return 1;
  }

  // 584111F7 - Prevents Minecraft from loading
  // We're suppose to set error code if we fail function
  // XThread::SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
  return cvars::xhttp;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpStartup, kNetworking, kStub);

void NetDll_XHttpShutdown_entry(dword_t caller) {}
DECLARE_XAM_EXPORT1(NetDll_XHttpShutdown, kNetworking, kStub);

dword_result_t NetDll_XHttpCrackUrl_entry(
    dword_t caller, lpstring_t url_ptr, dword_t url_length, dword_t flags,
    pointer_t<XHTTP_URL_COMPONENTS> url_components_ptr) {
  if (!url_ptr || !url_components_ptr ||
      url_components_ptr->struct_size != sizeof(XHTTP_URL_COMPONENTS)) {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    return false;
  }

  const bool insufficient_buffer =
      url_components_ptr->scheme_ptr && !url_components_ptr->scheme_length ||
      url_components_ptr->host_name_ptr &&
          !url_components_ptr->host_name_length ||
      url_components_ptr->user_name_ptr &&
          !url_components_ptr->user_name_length ||
      url_components_ptr->password_ptr &&
          !url_components_ptr->password_length ||
      url_components_ptr->url_path_ptr &&
          !url_components_ptr->url_path_length ||
      url_components_ptr->extra_info_ptr &&
          !url_components_ptr->extra_info_length;

  // ICU decode is only supported with user provided buffers
  if (flags & X_ICU_DECODE) {
    const bool invalid_paramater =
        !url_components_ptr->scheme_ptr && url_components_ptr->scheme_length ||
        !url_components_ptr->host_name_ptr &&
            url_components_ptr->host_name_length ||
        !url_components_ptr->user_name_ptr &&
            url_components_ptr->user_name_length ||
        !url_components_ptr->password_ptr &&
            url_components_ptr->password_length ||
        !url_components_ptr->url_path_ptr &&
            url_components_ptr->url_path_length ||
        !url_components_ptr->extra_info_ptr &&
            url_components_ptr->extra_info_length;

    // Invalid or provided buffer is insufficient
    if (invalid_paramater || insufficient_buffer) {
      XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
      return false;
    }
  }

  std::string url_to_process = url_ptr.value();

  if (url_length) {
    url_to_process = url_ptr.value().substr(0, url_length);
  }

  CURLU* url = curl_url();
  CURLUcode rc = curl_url_set(url, CURLUPART_URL, url_to_process.c_str(), 0);

  // Assert if URL is bad format
  assert_zero(rc);

  if (rc) {
    url_components_ptr->scheme = -1;
  }

  if (flags & X_ICU_DECODE) {
    CURL* curl = curl_easy_init();
    int output_length = 0;

    char* decoded_output = curl_easy_unescape(curl, url_to_process.c_str(),
                                              url_length, &output_length);

    url_to_process = std::string(decoded_output, output_length);

    curl_free(decoded_output);
    curl_easy_cleanup(curl);
  }

  std::regex url_regex(
      R"(^([a-zA-Z]+)://(?:([^:@]+)(?::([^:@]*))?@)?([^/:]+)(?::(\d+))?((/[^?#]*)(\?[^#]*)?(#[^ ]*)?)?$)",
      std::regex_constants::icase);

  const char* component_names[] = {"Full Match", "Protocol", "Username",
                                   "Password",   "Host",     "Port",
                                   "Path",       "Query"};

  std::smatch matches;

  auto ProcessComponent = [kernel_state = kernel_state()](
                              const uint32_t component_result_ptr,
                              uint32_t& component_ptr,
                              uint32_t& component_length_ptr, uint32_t size) {
    if (component_ptr) {
      if (!component_length_ptr || component_length_ptr < size + 1) {
        component_length_ptr = size + 1;  // Null Terminator
        return false;
      }

      char* result_dst_ptr =
          kernel_state->memory()->TranslateVirtual<char*>(component_ptr);

      char* result_src_ptr =
          kernel_state->memory()->TranslateVirtual<char*>(component_result_ptr);

      std::memcpy(result_dst_ptr, result_src_ptr, size);
      component_length_ptr = size;
    } else if (component_length_ptr) {
      component_ptr = component_result_ptr;
      component_length_ptr = size;
    }

    return true;
  };

  bool ret = true;

  if (std::regex_match(url_to_process, matches, url_regex)) {
    for (size_t i = 0; i < matches.size(); ++i) {
      std::ssub_match sub_match = matches[i];

      if (sub_match.matched) {
        const uint32_t result_ptr = url_ptr.guest_address() +
                                    static_cast<uint32_t>(matches.position(i));

        const uint32_t length = static_cast<uint32_t>(sub_match.length());

        const X_URL_COMPONENTS current_component =
            static_cast<X_URL_COMPONENTS>(i);

        switch (current_component) {
          case X_URL_COMPONENTS::Full: {
            // Skip
            continue;
          } break;
          case X_URL_COMPONENTS::Protocol: {
            uint32_t scheme_ptr_out = url_components_ptr->scheme_ptr;
            uint32_t scheme_length_out = url_components_ptr->scheme_length;

            const bool component_result = ProcessComponent(
                result_ptr, scheme_ptr_out, scheme_length_out, length);

            url_components_ptr->scheme_length = scheme_length_out;

            if (component_result && !url_components_ptr->scheme_ptr) {
              url_components_ptr->scheme_ptr = scheme_ptr_out;
            } else {
              ret = false;
            }

            const char* scheme_data_ptr =
                kernel_state()->memory()->TranslateVirtual<char*>(result_ptr);

            std::string schema_data = std::string(scheme_data_ptr, length);

            X_INTERNET_SCHEME scheme_type = {};

            // Set default scheme and port
            if (utf8::equal_case(schema_data.c_str(), "http")) {
              scheme_type = X_INTERNET_SCHEME::HTTP;
              url_components_ptr->port = 80;
            } else if (utf8::equal_case(schema_data.c_str(), "https")) {
              scheme_type = X_INTERNET_SCHEME::HTTPS;
              url_components_ptr->port = 443;
            }

            url_components_ptr->scheme = static_cast<uint32_t>(scheme_type);
          } break;
          case X_URL_COMPONENTS::Username: {
            uint32_t username_ptr_out = url_components_ptr->user_name_ptr;
            uint32_t username_length_out = url_components_ptr->user_name_length;

            const bool component_result = ProcessComponent(
                result_ptr, username_ptr_out, username_length_out, length);

            url_components_ptr->user_name_length = username_length_out;

            if (component_result && !url_components_ptr->user_name_ptr) {
              url_components_ptr->user_name_ptr = username_ptr_out;
            } else {
              ret = false;
            }
          } break;
          case X_URL_COMPONENTS::Password: {
            uint32_t password_ptr_out = url_components_ptr->password_ptr;
            uint32_t password_length_out = url_components_ptr->password_length;

            const bool component_result = ProcessComponent(
                result_ptr, password_ptr_out, password_length_out, length);

            url_components_ptr->password_length = password_length_out;

            if (component_result && !url_components_ptr->password_ptr) {
              url_components_ptr->password_ptr = password_ptr_out;
            } else {
              ret = false;
            }
          } break;
          case X_URL_COMPONENTS::Host: {
            uint32_t host_ptr_out = url_components_ptr->host_name_ptr;
            uint32_t host_length_out = url_components_ptr->host_name_length;

            const bool component_result = ProcessComponent(
                result_ptr, host_ptr_out, host_length_out, length);

            url_components_ptr->host_name_length = host_length_out;

            if (component_result && !url_components_ptr->host_name_ptr) {
              url_components_ptr->host_name_ptr = host_ptr_out;
            } else {
              ret = false;
            }
          } break;
          case X_URL_COMPONENTS::Port: {
            const char* port_str_ptr =
                kernel_memory()->TranslateVirtual<char*>(result_ptr);

            std::string port_str = std::string(port_str_ptr, length);

            const uint16_t port =
                xe::string_util::from_string<uint16_t>(port_str);

            url_components_ptr->port = port;
          } break;
          case X_URL_COMPONENTS::Path: {
            uint32_t path_ptr_out = url_components_ptr->url_path_ptr;
            uint32_t path_length_out = url_components_ptr->url_path_length;

            const bool component_result = ProcessComponent(
                result_ptr, path_ptr_out, path_length_out, length);

            url_components_ptr->url_path_length = path_length_out;

            if (component_result && !url_components_ptr->url_path_ptr) {
              url_components_ptr->url_path_ptr = path_ptr_out;
            } else {
              ret = false;
            }
          } break;
          case X_URL_COMPONENTS::Query: {
            uint32_t extra_ptr_out = url_components_ptr->extra_info_ptr;
            uint32_t extra_length_out = url_components_ptr->extra_info_length;

            const bool component_result = ProcessComponent(
                result_ptr, extra_ptr_out, extra_length_out, length);

            url_components_ptr->extra_info_length = extra_length_out;

            if (component_result && !url_components_ptr->extra_info_ptr) {
              url_components_ptr->extra_info_ptr = extra_ptr_out;
            } else {
              ret = false;
            }
          } break;
        }
      }
    }
  }

  // Return after processing so the component length is set
  if (insufficient_buffer) {
    XThread::SetLastError(X_ERROR_INSUFFICIENT_BUFFER);
    ret = false;
  }

  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpCrackUrl, kNetworking, kImplemented);

dword_result_t NetDll_XHttpDoWork_entry(dword_t caller, dword_t handle,
                                        dword_t unk) {
  XThread::SetLastError(X_ERROR_SUCCESS);

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpDoWork, kNetworking, kStub);

dword_result_t NetDll_XHttpOpenRequest_entry(
    dword_t caller, dword_t connect_handle, lpstring_t verb, lpstring_t path,
    lpstring_t version, lpstring_t referrer, lpstring_t reserved,
    dword_t flag) {
  std::string http_verb = "";
  std::string object_name = "";

  if (verb) {
    http_verb = *verb;
  }

  if (path) {
    object_name = *path;
  }

  XELOGI("OpenRequest: {} {}", http_verb, object_name);

  // Return invalid handle (not NULL)
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpOpenRequest, kNetworking, kStub);

dword_result_t NetDll_XHttpSetStatusCallback_entry(dword_t caller,
                                                   dword_t handle,
                                                   lpdword_t callback_ptr,
                                                   dword_t flags, dword_t unk) {
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpSetStatusCallback, kNetworking, kStub);

dword_result_t NetDll_XHttpSendRequest_entry(dword_t caller, dword_t hrequest,
                                             lpstring_t headers,
                                             dword_t hlength, lpvoid_t unkn1,
                                             dword_t unkn2, dword_t unk3,
                                             dword_t unk4) {
  std::string request_headers = "";

  if (headers) {
    request_headers = *headers;
  }

  XELOGI("Headers {}", request_headers);
  return false;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpSendRequest, kNetworking, kStub);

dword_result_t NetDll_XHttpConnect_entry(dword_t caller, dword_t hSession,
                                         lpstring_t host, dword_t port,
                                         dword_t flags) {
  // XThread::SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpConnect, kNetworking, kStub);

dword_result_t NetDll_inet_addr_entry(lpstring_t addr_ptr) {
  if (!addr_ptr) {
    return -1;
  }

  uint32_t addr = inet_addr(addr_ptr);
  // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-inet_addr#return-value
  // Based on console research it seems like x360 uses old version of
  // inet_addr In case of empty string it return 0 instead of -1
  if (addr == -1 && !addr_ptr.value().length()) {
    return 0;
  }

  return xe::byte_swap(addr);
}
DECLARE_XAM_EXPORT1(NetDll_inet_addr, kNetworking, kImplemented);

bool optEnable = true;
dword_result_t NetDll_socket_entry(dword_t caller, dword_t af, dword_t type,
                                   dword_t protocol) {
  XSocket* socket = new XSocket(kernel_state());
  X_STATUS result = socket->Initialize(XSocket::AddressFamily((uint32_t)af),
                                       XSocket::Type((uint32_t)type),
                                       XSocket::Protocol((uint32_t)protocol));
  if (XFAILED(result)) {
    socket->Release();

    XThread::SetLastError(socket->GetLastWSAError());
    XELOGE("NetDll_socket: failed with error {:08X}",
           socket->GetLastWSAError());
    return -1;
  }

  // socket->SetOption(SOL_SOCKET, 0x5801, &optEnable, sizeof(BOOL));
  // if (type == SOCK_STREAM)
  //   socket->SetOption(SOL_SOCKET, 0x5802, &optEnable, sizeof(BOOL));

  return socket->handle();
}
DECLARE_XAM_EXPORT1(NetDll_socket, kNetworking, kImplemented);

dword_result_t NetDll_closesocket_entry(dword_t caller, dword_t socket_handle) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  if (upnp) {
    CleanupUPnPActions();

    auto remove_port = upnp->RemovePortAsync(socket->bound_port(),
                                             socket->GetProtocolUPnPString());
    upnp_actions_.push_back(std::move(remove_port));
  }

  // TODO: Absolutely delete this object. It is no longer valid after calling
  // closesocket.
  socket->Close();
  socket->ReleaseHandle();
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_closesocket, kNetworking, kImplemented);

int_result_t NetDll_shutdown_entry(dword_t caller, dword_t socket_handle,
                                   int_t how) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  auto ret = socket->Shutdown(how);
  if (ret == -1) {
    XThread::SetLastError(socket->GetLastWSAError());
  }
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_shutdown, kNetworking, kImplemented);

dword_result_t NetDll_setsockopt_entry(dword_t caller, dword_t socket_handle,
                                       dword_t level, dword_t optname,
                                       lpvoid_t optval_ptr, dword_t optlen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  auto ret = socket->SetOption(level, optname, optval_ptr, optlen);
  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_setsockopt, kNetworking, kImplemented);

dword_result_t NetDll_getsockopt_entry(dword_t caller, dword_t socket_handle,
                                       dword_t level, dword_t optname,
                                       lpvoid_t optval_ptr, lpdword_t optlen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  uint32_t native_len = *optlen;
  X_STATUS status = socket->GetOption(level, optname, optval_ptr, &native_len);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_getsockopt, kNetworking, kImplemented);

dword_result_t NetDll_ioctlsocket_entry(dword_t caller, dword_t socket_handle,
                                        dword_t cmd, lpvoid_t arg_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  X_STATUS status = socket->IOControl(cmd, arg_ptr.as<uint32_t*>());
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    XELOGE("NetDll_ioctlsocket: failed with error {:08X}",
           socket->GetLastWSAError());
    return -1;
  }

  // TODO
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_ioctlsocket, kNetworking, kImplemented);

dword_result_t NetDll_bind_entry(dword_t caller, dword_t socket_handle,
                                 pointer_t<XSOCKADDR_IN> name,
                                 dword_t namelen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  const auto network_adapter =
      kernel_state()->emulator()->GetNetworkAdapterManager();

  const std::string local_ip = network_adapter->GetSelectedAdapterLocalIP_Str();

  if (!network_adapter->IsSelectedAdapterWANRouting() &&
      cvars::xlink_kai_systemlink_hack) {
    // Force socket to bind to the IP of the selected interface
    name->address_ip = network_adapter->GetSelectedAdapterLocalIP().sin_addr;
  }

  X_STATUS status = socket->Bind(name, namelen);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    XELOGE("NetDll_bind: failed with error {:08X}", socket->GetLastWSAError());
    return -1;
  }

  const auto upnp = kernel_state()->emulator()->GetUPnP();

  uint16_t upnp_internal_port = name->address_port;

  if (upnp) {
    const uint16_t mapped_internal_port =
        upnp->GetMappedBindPort(name->address_port);

    // Support wildcard port
    if (!upnp_internal_port || !mapped_internal_port) {
      upnp_internal_port = socket->bound_port();
    }

    const std::string protocol = socket->GetProtocolUPnPString();

    if (upnp->IsActive()) {
      CleanupUPnPActions();

      auto open_port =
          upnp->AddPortAsync(local_ip, upnp_internal_port, protocol);
      upnp_actions_.push_back(std::move(open_port));
    } else {
      upnp->TrackPort(upnp_internal_port, protocol);
    }
  }

  if (cvars::logging) {
    XELOGI("Bind port {}", upnp_internal_port);
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_bind, kNetworking, kImplemented);

dword_result_t NetDll_connect_entry(dword_t caller, dword_t socket_handle,
                                    pointer_t<XSOCKADDR_IN> name,
                                    dword_t namelen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  X_STATUS status = socket->Connect(name, namelen);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_connect, kNetworking, kImplemented);

dword_result_t NetDll_listen_entry(dword_t caller, dword_t socket_handle,
                                   int_t backlog) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  X_STATUS status = socket->Listen(backlog);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_listen, kNetworking, kImplemented);

dword_result_t NetDll_accept_entry(dword_t caller, dword_t socket_handle,
                                   pointer_t<XSOCKADDR_IN> addr_ptr,
                                   lpdword_t addrlen_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int* name_len_host_ptr = nullptr;
  if (addrlen_ptr) {
    name_len_host_ptr = reinterpret_cast<int*>(addrlen_ptr.host_address());
  }
  auto new_socket = socket->Accept(addr_ptr, name_len_host_ptr);
  if (!new_socket) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }
  return new_socket->handle();
}
DECLARE_XAM_EXPORT1(NetDll_accept, kNetworking, kImplemented);

struct x_fd_set {
  xe::be<uint32_t> fd_count;
  xe::be<uint32_t> fd_array[64];
};

struct host_set {
  uint32_t count;
  object_ref<XSocket> sockets[64];

  void Load(const x_fd_set* guest_set) {
    assert_true(guest_set->fd_count < 64);

    count = guest_set->fd_count;
    for (uint32_t i = 0; i < count; ++i) {
      auto socket_handle = static_cast<X_HANDLE>(guest_set->fd_array[i]);
      if (socket_handle == -1) {
        count = i;
        break;
      }
      // Convert from Xenia -> native
      auto socket =
          kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
      assert_not_null(socket);
      sockets[i] = socket;
    }
  }

  void Store(x_fd_set* guest_set) {
    guest_set->fd_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
      auto socket = sockets[i];
      guest_set->fd_array[guest_set->fd_count++] = socket->handle();
    }
  }

  void Store(fd_set* native_set) {
    FD_ZERO(native_set);
    for (uint32_t i = 0; i < count; ++i) {
      FD_SET(sockets[i]->native_handle(), native_set);
    }
  }

  void UpdateFrom(fd_set* native_set) {
    uint32_t new_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
      auto socket = sockets[i];
      if (FD_ISSET(socket->native_handle(), native_set)) {
        sockets[new_count++] = socket;
      }
    }
    count = new_count;
  }
};

bool verify_x_fd_set(const x_fd_set* guest_set) {
  for (uint32_t i = 0; i < guest_set->fd_count; ++i) {
    auto socket_handle = static_cast<X_HANDLE>(guest_set->fd_array[i]);
    if (socket_handle == -1) {
      break;
    }
    // Convert from Xenia -> native
    auto socket =
        kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
    if (!socket) {
      return false;
    }
  }
  return true;
}

int_result_t NetDll_select_entry(dword_t caller, dword_t nfds,
                                 pointer_t<x_fd_set> readfds,
                                 pointer_t<x_fd_set> writefds,
                                 pointer_t<x_fd_set> exceptfds,
                                 lpvoid_t timeout_ptr) {
  host_set host_readfds = {0};
  fd_set native_readfds = {0};
  if (readfds) {
    if (!verify_x_fd_set(readfds)) {
      XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
      return -1;
    }

    host_readfds.Load(readfds);
    host_readfds.Store(&native_readfds);
  }
  host_set host_writefds = {0};
  fd_set native_writefds = {0};
  if (writefds) {
    if (!verify_x_fd_set(writefds)) {
      XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
      return -1;
    }

    host_writefds.Load(writefds);
    host_writefds.Store(&native_writefds);
  }
  host_set host_exceptfds = {0};
  fd_set native_exceptfds = {0};
  if (exceptfds) {
    if (!verify_x_fd_set(exceptfds)) {
      XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
      return -1;
    }

    host_exceptfds.Load(exceptfds);
    host_exceptfds.Store(&native_exceptfds);
  }
  timeval* timeout_in = nullptr;
  timeval timeout;
  if (timeout_ptr) {
    timeout = {static_cast<int32_t>(timeout_ptr.as_array<int32_t>()[0]),
               static_cast<int32_t>(timeout_ptr.as_array<int32_t>()[1])};
    Clock::ScaleGuestDurationTimeval(
        reinterpret_cast<int32_t*>(&timeout.tv_sec),
        reinterpret_cast<int32_t*>(&timeout.tv_usec));
    timeout_in = &timeout;
  }
  int ret = select(nfds, readfds ? &native_readfds : nullptr,
                   writefds ? &native_writefds : nullptr,
                   exceptfds ? &native_exceptfds : nullptr, timeout_in);
  if (readfds) {
    host_readfds.UpdateFrom(&native_readfds);
    host_readfds.Store(readfds);
  }
  if (writefds) {
    host_writefds.UpdateFrom(&native_writefds);
    host_writefds.Store(writefds);
  }
  if (exceptfds) {
    host_exceptfds.UpdateFrom(&native_exceptfds);
    host_exceptfds.Store(exceptfds);
  }

  // TODO(gibbed): modify ret to be what's actually copied to the guest
  // fd_sets?
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_select, kNetworking, kImplemented);

dword_result_t NetDll_recv_entry(dword_t caller, dword_t socket_handle,
                                 lpvoid_t buf_ptr, dword_t buf_len,
                                 dword_t flags) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int ret = socket->Recv(buf_ptr, buf_len, flags);
  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
  }
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_recv, kNetworking, kImplemented);

dword_result_t NetDll_recvfrom_entry(dword_t caller, dword_t socket_handle,
                                     lpvoid_t buf_ptr, dword_t buf_len,
                                     dword_t flags,
                                     pointer_t<XSOCKADDR_IN> from_ptr,
                                     lpdword_t fromlen_ptr) {
  // Fixed 415607D6, 4E4D07DC
  InitalizeSockaddr(from_ptr);

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  socklen_t native_fromlen = fromlen_ptr ? fromlen_ptr.value() : 0;
  int ret = socket->RecvFrom(buf_ptr, buf_len, flags, from_ptr,
                             fromlen_ptr ? &native_fromlen : nullptr);
  if (fromlen_ptr) {
    *fromlen_ptr = native_fromlen;
  }

  if (ret == -1) {
    XThread::SetLastError(socket->GetLastWSAError());
  } else if (ret >= 0 && !cvars::log_mask_ips && from_ptr) {
    XELOGI("NetDll_recvfrom: Received {} bytes from: {}:{}({})", ret,
           ip_to_string(from_ptr->address_ip), from_ptr->address_port.get(),
           socket->GetProtocolUPnPString());
  }

  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_recvfrom, kNetworking, kImplemented);

dword_result_t NetDll_send_entry(dword_t caller, dword_t socket_handle,
                                 lpvoid_t buf_ptr, dword_t buf_len,
                                 dword_t flags) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int ret = socket->Send(buf_ptr, buf_len, flags);
  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
  }
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_send, kNetworking, kImplemented);

dword_result_t NetDll_sendto_entry(dword_t caller, dword_t socket_handle,
                                   lpvoid_t buf_ptr, dword_t buf_len,
                                   dword_t flags,
                                   pointer_t<XSOCKADDR_IN> to_ptr,
                                   dword_t to_len) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int ret = socket->SendTo(buf_ptr, buf_len, flags, to_ptr, to_len);
  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
  } else if (ret >= 0 && to_ptr && !cvars::log_mask_ips) {
    XELOGI("NetDll_sendto: Send {} bytes to: {}:{}({})", ret,
           ip_to_string(to_ptr->address_ip), to_ptr->address_port.get(),
           socket->GetProtocolUPnPString());
  }

  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_sendto, kNetworking, kImplemented);

dword_result_t NetDll_WSAEventSelect_entry(dword_t caller,
                                           dword_t socket_handle,
                                           dword_t event_handle,
                                           dword_t flags) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  auto ev = kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
  if (!ev) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int ret = socket->WSAEventSelect(socket->native_handle(), ev->native_handle(),
                                   flags);

  if (ret < 0) {
    XThread::SetLastError(socket->GetLastWSAError());
  }

  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_WSAEventSelect, kNetworking, kImplemented);

dword_result_t NetDll___WSAFDIsSet_entry(dword_t socket_handle,
                                         pointer_t<x_fd_set> fd_set) {
  const uint8_t max_fd_count =
      std::min((uint32_t)fd_set->fd_count, uint32_t(64));
  for (uint8_t i = 0; i < max_fd_count; i++) {
    if (fd_set->fd_array[i] == socket_handle) {
      return 1;
    }
  }
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll___WSAFDIsSet, kNetworking, kImplemented);

void NetDll_WSASetLastError_entry(dword_t error_code) {
  XThread::SetLastError(error_code);
}
DECLARE_XAM_EXPORT1(NetDll_WSASetLastError, kNetworking, kImplemented);

dword_result_t NetDll_getpeername_entry(dword_t caller, dword_t socket_handle,
                                        pointer_t<XSOCKADDR_IN> addr_ptr,
                                        lpdword_t addrlen_ptr) {
  if (!addr_ptr) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAEFAULT));
    return -1;
  }

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int native_len = *addrlen_ptr;
  X_STATUS status = socket->GetPeerName(addr_ptr, &native_len);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  *addrlen_ptr = native_len;
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_getpeername, kNetworking, kImplemented);

dword_result_t NetDll_getsockname_entry(dword_t caller, dword_t socket_handle,
                                        pointer_t<XSOCKADDR_IN> addr_ptr,
                                        lpdword_t addrlen_ptr) {
  InitalizeSockaddr(addr_ptr);

  if (!addr_ptr) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAEFAULT));
    return -1;
  }

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  int native_len = *addrlen_ptr;
  X_STATUS status = socket->GetSockName(addr_ptr, &native_len);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  *addrlen_ptr = native_len;
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_getsockname, kNetworking, kImplemented);

dword_result_t NetDll_XNetCreateKey_entry(dword_t caller,
                                          pointer_t<XNKID> session_key,
                                          pointer_t<XNKEY> exchange_key) {
  const xe::be<uint64_t> xnkid = GenerateSessionId(XNKID_SYSTEM_LINK);
  memcpy(session_key->ab, &xnkid, sizeof(XNKID));

  GenerateIdentityExchangeKey(exchange_key);
  // memcpy(exchange_key, kernel_state()->title_lan_key(), sizeof(XNKEY));

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetCreateKey, kNetworking, kStub);

dword_result_t NetDll_XNetRegisterKey_entry(dword_t caller,
                                            pointer_t<XNKID> session_key,
                                            pointer_t<XNKEY> exchange_key) {
  if (IsSystemlink(session_key->as_uintBE64())) {
    XELOGI("XNetRegisterKey: Systemlink");
    XLiveAPI::systemlink_id = session_key->as_uintBE64();
    return 0;
  }

  if (IsOnlinePeer(session_key->as_uintBE64())) {
    XELOGI("XNetRegisterKey: Xbox Live");
    EXPLICIT_XBOXLIVE_KEY = true;
    return 0;
  }

  if (IsServer(session_key->as_uintBE64())) {
    XELOGI("XNetRegisterKey: Server");
    return 0;
  }

  XELOGI(fmt::format("XNetRegisterKey: {:016X} (Unknown)",
                     session_key->as_uintBE64()));

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetRegisterKey, kNetworking, kStub);

dword_result_t NetDll_XNetUnregisterKey_entry(dword_t caller,
                                              pointer_t<XNKID> session_key) {
  if (XLiveAPI::systemlink_id) {
    if (IsSystemlink(XLiveAPI::systemlink_id)) {
      XELOGI("XNetUnregisterKey: Systemlink");
    }

    XLiveAPI::systemlink_id = 0;
  }

  if (EXPLICIT_XBOXLIVE_KEY) {
    EXPLICIT_XBOXLIVE_KEY = false;

    XELOGI("XNetUnregisterKey: Xbox Live");
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetUnregisterKey, kNetworking, kStub);

// Remove completed UPnP actions
void CleanupUPnPActions() {
  auto completed_actions =
      std::remove_if(upnp_actions_.begin(), upnp_actions_.end(),
                     [](const std::future<int32_t>& f) {
                       return f.wait_for(0s) == std::future_status::ready;
                     });

  upnp_actions_.erase(completed_actions, upnp_actions_.end());
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Net);
