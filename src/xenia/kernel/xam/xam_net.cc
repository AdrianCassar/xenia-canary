/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_net.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_error.h"
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
#include <natupnp.h>
#include <wrl/client.h>
#elif XE_PLATFORM_LINUX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#endif
#include <random>
#include <third_party/libcurl/include/curl/curl.h>
#define RAPIDJSON_HAS_STDSTRING 1
#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/rapidjson/include/rapidjson/stringbuffer.h>

using namespace rapidjson;

DEFINE_string(api_address, "http://xbl.craftycodie.com:36000", "Xenia Master Server Address", "Live");

// TODO: Remove - Codie
std::size_t callback(const char* in, std::size_t size, std::size_t num,
                     char* out) {
  std::string data(in, (std::size_t)size * num);
  *((std::stringstream*)out) << data;
  return size * num;
} 

xe::be<uint64_t> MacAddresstoUint64(const unsigned char* macAddress) {
  int i;
  xe::be<uint64_t> macAddress64 = 0;
  for (i = 5; i >= 0; --i) {
    macAddress64 = macAddress64 << 8;
    macAddress64 |= (uint64_t)macAddress[5 - i];
  }

  return macAddress64;
}

xe::be<uint64_t> SessionIdtoUint64(const unsigned char* sessionID) {
  int i;
  xe::be<uint64_t> sessionId64 = 0;
  for (i = 7; i >= 0; --i) {
    sessionId64 = sessionId64 << 8;
    sessionId64 |= (uint64_t)sessionID[7 - i];
  }

  return sessionId64;
}

void Uint64toSessionId(xe::be<uint64_t> sessionID, unsigned char* sessionIdOut) {
  for (int i = 0; i < 8; i++) {
    sessionIdOut[7-i] = ((sessionID >> (8 * i)) & 0XFF);
  }
}

void Uint64toMacAddress(xe::be<uint64_t> macAddress, unsigned char* macAddressOut) {
  for (int i = 0; i < 6; i++) {
    macAddressOut[5-i] = ((macAddress >> (8 * i)) & 0XFF);
  }
}

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

// https://github.com/pmrowla/hl2sdk-csgo/blob/master/common/xbox/xboxstubs.h
typedef struct {
  // FYI: IN_ADDR should be in network-byte order.
  in_addr ina;                   // IP address (zero if not static/DHCP)
  in_addr inaOnline;             // Online IP address (zero if not online)
  xe::be<uint16_t> wPortOnline;  // Online port
  uint8_t abEnet[6];             // Ethernet MAC address
  uint8_t abOnline[20];          // Online identification
} XNADDR;

typedef struct {
  xe::be<int32_t> status;
  xe::be<uint32_t> cina;
  in_addr aina[8];
} XNDNS;

typedef struct {
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
} XNQOSINFO;

typedef struct {
  xe::be<uint32_t> count;
  xe::be<uint32_t> count_pending;
  XNQOSINFO info[1];
} XNQOS;

struct Xsockaddr_t {
  xe::be<uint16_t> sa_family;
  char sa_data[14];
};

struct X_WSADATA {
  xe::be<uint16_t> version;
  xe::be<uint16_t> version_high;
  char description[256 + 1];
  char system_status[128 + 1];
  xe::be<uint16_t> max_sockets;
  xe::be<uint16_t> max_udpdg;
  xe::be<uint32_t> vendor_info_ptr;
};

void LoadSockaddr(const uint8_t* ptr, sockaddr* out_addr) {
  out_addr->sa_family = xe::load_and_swap<uint16_t>(ptr + 0);
  switch (out_addr->sa_family) {
    case AF_INET: {
      auto in_addr = reinterpret_cast<sockaddr_in*>(out_addr);
      in_addr->sin_port = xe::load_and_swap<uint16_t>(ptr + 2);
      // Maybe? Depends on type.
      in_addr->sin_addr.s_addr = *(uint32_t*)(ptr + 4);
      break;
    }
    default:
      assert_unhandled_case(out_addr->sa_family);
      break;
  }
}

void StoreSockaddr(const sockaddr& addr, uint8_t* ptr) {
  switch (addr.sa_family) {
    case AF_UNSPEC:
      std::memset(ptr, 0, sizeof(addr));
      break;
    case AF_INET: {
      auto& in_addr = reinterpret_cast<const sockaddr_in&>(addr);
      xe::store_and_swap<uint16_t>(ptr + 0, in_addr.sin_family);
      xe::store_and_swap<uint16_t>(ptr + 2, in_addr.sin_port);
      // Maybe? Depends on type.
      xe::store_and_swap<uint32_t>(ptr + 4, in_addr.sin_addr.s_addr);
      break;
    }
    default:
      assert_unhandled_case(addr.sa_family);
      break;
  }
}

// https://github.com/joolswills/mameox/blob/master/MAMEoX/Sources/xbox_Network.cpp#L136
struct XNetStartupParams {
  uint8_t cfgSizeOfStruct;
  uint8_t cfgFlags;
  uint8_t cfgSockMaxDgramSockets;
  uint8_t cfgSockMaxStreamSockets;
  uint8_t cfgSockDefaultRecvBufsizeInK;
  uint8_t cfgSockDefaultSendBufsizeInK;
  uint8_t cfgKeyRegMax;
  uint8_t cfgSecRegMax;
  uint8_t cfgQosDataLimitDiv4;
  uint8_t cfgQosProbeTimeoutInSeconds;
  uint8_t cfgQosProbeRetries;
  uint8_t cfgQosSrvMaxSimultaneousResponses;
  uint8_t cfgQosPairWaitTimeInSeconds;
};

XNetStartupParams xnet_startup_params = {0};

dword_result_t NetDll_XNetStartup_entry(dword_t caller,
                                        pointer_t<XNetStartupParams> params) {
  if (params) {
    assert_true(params->cfgSizeOfStruct == sizeof(XNetStartupParams));
    std::memcpy(&xnet_startup_params, params, sizeof(XNetStartupParams));
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

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetCleanup, kNetworking, kImplemented);

dword_result_t XNetLogonGetTitleID_entry(dword_t caller, lpvoid_t params) {
  return kernel_state()->title_id();
}
DECLARE_XAM_EXPORT1(XNetLogonGetTitleID, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetOpt_entry(dword_t one, dword_t option_id,
                                       lpvoid_t buffer_ptr,
                                       lpdword_t buffer_size) {
  assert_true(one == 1);
  switch (option_id) {
    case 1:
      if (*buffer_size < sizeof(XNetStartupParams)) {
        *buffer_size = sizeof(XNetStartupParams);
        return uint32_t(X_WSAError::X_WSAEMSGSIZE);
      }
      std::memcpy(buffer_ptr, &xnet_startup_params, sizeof(XNetStartupParams));
      return 0;
    default:
      XELOGE("NetDll_XNetGetOpt: option {} unimplemented",
             static_cast<uint32_t>(option_id));
      return uint32_t(X_WSAError::X_WSAEINVAL);
  }
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetOpt, kNetworking, kSketchy);

dword_result_t NetDll_XNetRandom_entry(dword_t caller, lpvoid_t buffer_ptr,
                                       dword_t length) {
  std::random_device rd;
  std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
  std::vector<char> data(length);
  int offset = 0;
  uint32_t bits = 0;

  for (unsigned int i = 0; i < length; i++) {

    if (offset == 0) bits = dist(rd);
    *(unsigned char*)(buffer_ptr.host_address() + i) = static_cast<unsigned char>(bits & 0xFF);
    bits >>= 8;
    if (++offset >= 4) offset = 0;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetRandom, kNetworking, kStub);

dword_result_t NetDll_WSAStartup_entry(dword_t caller, word_t version,
                                       pointer_t<X_WSADATA> data_ptr) {
// TODO(benvanik): abstraction layer needed.
#ifdef XE_PLATFORM_WIN32
  WSADATA wsaData;
  ZeroMemory(&wsaData, sizeof(WSADATA));
  int ret = WSAStartup(version, &wsaData);

  auto data_out = kernel_state()->memory()->TranslateVirtual(data_ptr);

  if (data_ptr) {
    data_ptr->version = wsaData.wVersion;
    data_ptr->version_high = wsaData.wHighVersion;
    std::memcpy(&data_ptr->description, wsaData.szDescription, 0x100);
    std::memcpy(&data_ptr->system_status, wsaData.szSystemStatus, 0x80);
    data_ptr->max_sockets = wsaData.iMaxSockets;
    data_ptr->max_udpdg = wsaData.iMaxUdpDg;

    // Some games (5841099F) want this value round-tripped - they'll compare if
    // it changes and bugcheck if it does.
    uint32_t vendor_ptr = xe::load_and_swap<uint32_t>(data_out + 0x190);
    xe::store_and_swap<uint32_t>(data_out + 0x190, vendor_ptr);
  }
#else
  int ret = 0;
  if (data_ptr) {
    // Guess these values!
    data_ptr->version = version.value();
    data_ptr->description[0] = '\0';
    data_ptr->system_status[0] = '\0';
    data_ptr->max_sockets = 100;
    data_ptr->max_udpdg = 1024;
  }
#endif

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
    pointer_t<XSOCKADDR> from_ptr, lpdword_t fromlen_ptr,
    pointer_t<XWSAOVERLAPPED> overlapped_ptr, lpvoid_t completion_routine_ptr) {
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
    pointer_t<XSOCKADDR> to_ptr, dword_t to_len,
    pointer_t<XWSAOVERLAPPED> overlapped, lpvoid_t completion_routine) {
  assert(!overlapped);
  assert(!completion_routine);

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

  socket->SendTo(combined_buffer_mem.data(), combined_buffer_size, flags,
                 to_ptr, to_len);

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
    return ~0u;
  }

  uint64_t timeout_wait = (uint64_t)timeout;

  X_STATUS result = 0;
  do {
    result = xboxkrnl::xeNtWaitForMultipleObjectsEx(
        num_events, events, !wait_all, 1, alertable,
        timeout != -1 ? &timeout_wait : nullptr);
  } while (result == X_STATUS_ALERTED);

  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::xeRtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return ~0u;
  }
  return 0;
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

struct XnAddrStatus {
  // Address acquisition is not yet complete
  static const uint32_t XNET_GET_XNADDR_PENDING = 0x00000000;
  // XNet is uninitialized or no debugger found
  static const uint32_t XNET_GET_XNADDR_NONE = 0x00000001;
  // Host has ethernet address (no IP address)
  static const uint32_t XNET_GET_XNADDR_ETHERNET = 0x00000002;
  // Host has statically assigned IP address
  static const uint32_t XNET_GET_XNADDR_STATIC = 0x00000004;
  // Host has DHCP assigned IP address
  static const uint32_t XNET_GET_XNADDR_DHCP = 0x00000008;
  // Host has PPPoE assigned IP address
  static const uint32_t XNET_GET_XNADDR_PPPOE = 0x00000010;
  // Host has one or more gateways configured
  static const uint32_t XNET_GET_XNADDR_GATEWAY = 0x00000020;
  // Host has one or more DNS servers configured
  static const uint32_t XNET_GET_XNADDR_DNS = 0x00000040;
  // Host is currently connected to online service
  static const uint32_t XNET_GET_XNADDR_ONLINE = 0x00000080;
  // Network configuration requires troubleshooting
  static const uint32_t XNET_GET_XNADDR_TROUBLESHOOT = 0x00008000;
};

bool upnp_fetched_ = false;
in_addr local_ip_ = {};
in_addr online_ip_ = {};
unsigned char* macAddress = nullptr;

static void FetchUPnP() {
  auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    return;
  }

#pragma region CURL, Refactor Out
  // TODO Codie
  // Get the external IP via curl in case upnp fails but the user has port-forwarded.

  CURL* curl;
  CURLcode res;

      std::stringstream url;
  url << cvars::api_address << "/whoami";

  std::stringstream out;

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (curl == NULL) {
    goto no_curl;
  }

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");


  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);

  res = curl_easy_perform(curl);

  curl_easy_cleanup(curl);
  int httpCode(0);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_global_cleanup();

  if (httpCode == 200) {
    rapidjson::Document d;
    d.Parse(out.str());

    online_ip_.S_un.S_addr = inet_addr(d["address"].GetString());
  }
no_curl:
#pragma endregion


  sockaddr_in addrin;
  addrin.sin_family = AF_INET;
  addrin.sin_port = 65535;
  addrin.sin_addr.s_addr = inet_addr("1.1.1.1");
  auto ret = connect(sock, (struct sockaddr*)&addrin, sizeof(addrin));
  if (ret < 0) {
    auto err = WSAGetLastError();
    XELOGE("Failed to reach DNS {:08X}", (uint32_t)err);
    return;
  }

  upnp_fetched_ = true;

  int socklen = sizeof(addrin);
  ret = getsockname(sock, (struct sockaddr*)&addrin, &socklen);
  if (ret < 0) {
    return;
  }
  local_ip_ = addrin.sin_addr;

  closesocket(sock);

#ifdef XE_PLATFORM_WIN32
  Microsoft::WRL::ComPtr<IUPnPNAT> nat;
  HRESULT hr =
      CoCreateInstance(CLSID_UPnPNAT, NULL, CLSCTX_ALL, IID_PPV_ARGS(&nat));
  if (FAILED(hr)) {
    XELOGE("Failed to create UPnP instance: {:08X}", (uint32_t)hr);
    return;
  }

  Microsoft::WRL::ComPtr<IStaticPortMappingCollection> staticPortMappings;
  hr = nat->get_StaticPortMappingCollection(&staticPortMappings);
  if (FAILED(hr) || staticPortMappings.Get() == nullptr) {
    XELOGE("Failed to get UPnP port mapping collection: {:08X}", (uint32_t)hr);
    return;
  }

  wchar_t local_ip_str[INET_ADDRSTRLEN];
  InetNtopW(AF_INET, &addrin.sin_addr, local_ip_str, INET_ADDRSTRLEN);

  Microsoft::WRL::ComPtr<IStaticPortMapping> portMapping;
  hr = staticPortMappings->Add(36000, BSTR{L"UDP"}, 36009,
                               BSTR{local_ip_str}, VARIANT_TRUE,
                               BSTR{L"Xenia"}, &portMapping);
  if (FAILED(hr)) {
    XELOGE("Failed to create UPnP port mapping: {:08X}", (uint32_t)hr);
    return;
  }
#endif
}

uint32_t xeXOnlineGetNatType() {
  if (!upnp_fetched_) {
    // TODO: this should be done ahead of time.
    FetchUPnP();
  }

  // 1 = open, 3 = strict
  return (online_ip_.s_addr != 0) ? 1 : 3;
}


in_addr getOnlineIp() {
  return online_ip_;
}

// TODO: This shouldn't be random and probably doesn't belong in this file. - Codie
const unsigned char* getMacAddress() { 
  if (macAddress == nullptr) {
    macAddress = new unsigned char[6];
    NetDll_XNetRandom_entry(0, macAddress, 6);
  }
  return macAddress;
}

uint64_t getMachineId() {
  auto macAddress = getMacAddress();

  uint64_t machineId = 0;

  int i;
  for (i = 5; i >= 0; --i) {
    machineId = machineId << 8;
    machineId |= macAddress[5 - i];
  }

  machineId += 0xFA00000000000000;

  return machineId;
}

uint16_t getPort() { return 36000; }

void RegisterPlayer() {
#pragma region Curl
  /*
      TODO:
          - Refactor the CURL out to a separate class.
          - Use the overlapped task to do this asyncronously.
  */

  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &getOnlineIp(), str, INET_ADDRSTRLEN);

  Document d;
  d.SetObject();

  Document::AllocatorType& allocator = d.GetAllocator();

  size_t sz = allocator.Size();

            std::stringstream macAddressString;
  macAddressString << std::hex << std::noshowbase << std::setw(12)
                   << std::setfill('0') << MacAddresstoUint64(getMacAddress());

  char ipString[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &getOnlineIp(), ipString, INET_ADDRSTRLEN);

  std::stringstream machineIdStr;
  machineIdStr << std::hex << std::noshowbase << std::setw(16)
               << std::setfill('0') << getMachineId();

  auto xuid = kernel_state()->xam_state()->GetUserProfile((uint32_t)0)->xuid();
  std::stringstream xuidStr;
  xuidStr << std::hex << std::noshowbase << std::setw(16)
               << std::setfill('0') << xuid;

  d.AddMember("xuid", xuidStr.str(), allocator);
  d.AddMember("machineId", machineIdStr.str(), allocator);
  d.AddMember("hostAddress", std::string(ipString), allocator);
  d.AddMember("macAddress", macAddressString.str(), allocator);

  rapidjson::StringBuffer strbuf;
  PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
  d.Accept(writer);

  CURL* curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (curl == NULL) {
    return;
  }

  std::stringstream out;

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  std::stringstream url;
  url << GetApiAddress() << "/players";
  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strbuf.GetString());

  res = curl_easy_perform(curl);

  curl_easy_cleanup(curl);
  int httpCode(0);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_global_cleanup();

#pragma endregion
}

std::map<uint16_t, uint16_t> mappedConnectPorts{};
std::map<uint16_t, uint16_t> mappedBindPorts{};


uint16_t GetMappedConnectPort(uint16_t port) {
  if (mappedConnectPorts.find(port) != mappedConnectPorts.end())
    return mappedConnectPorts[port];
  else {
    if (mappedConnectPorts.find(0) != mappedConnectPorts.end()) {
      XELOGI("Using wildcard connect port for guest port {}!", port);
      return mappedConnectPorts[0];
    }

    XELOGW("No mapped connect port found for {}!", port);
    return port;
  }
}

uint16_t GetMappedBindPort(uint16_t port) {
  if (mappedBindPorts.find(port) != mappedBindPorts.end())
    return mappedBindPorts[port];
  else {
    if (mappedBindPorts.find(0) != mappedBindPorts.end()) {
      XELOGI("Using wildcard bind port for guest port {}!", port);
      return mappedBindPorts[0];
    }

    XELOGW("No mapped bind port found for {}!", port);
    return port;
  }
}

std::string GetApiAddress() { return cvars::api_address; }

void DownloadPortMappings() {
#pragma region Curl
  /*
      TODO:
          - Refactor the CURL out to a separate class.
          - Use the overlapped task to do this asyncronously.
  */
  CURL* curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (curl == NULL) {
    return;
  }

  std::stringstream out;

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  std::stringstream titleId;
  titleId << std::hex << std::noshowbase << std::setw(8) << std::setfill('0')
          << kernel_state()->title_id();

  std::stringstream url;
  url << cvars::api_address << "/title/" << titleId.str()
      << "/ports";

  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);

  res = curl_easy_perform(curl);

  curl_easy_cleanup(curl);
  int httpCode(0);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_global_cleanup();

  if (httpCode == 200) {
    rapidjson::Document d;
    d.Parse(out.str());

    if (d.HasMember("connect")) {
      for (const auto& mapping : d["connect"].GetArray()) {
        mappedConnectPorts[mapping["port"].GetInt()] =
            mapping["mappedTo"].GetInt();
      }
    }

    if (d.HasMember("bind")) {
      for (const auto& mapping : d["bind"].GetArray()) {
        mappedBindPorts[mapping["port"].GetInt()] =
            mapping["mappedTo"].GetInt();
      }
    }
  }
#pragma endregion
}


dword_result_t NetDll_XNetGetTitleXnAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> addr_ptr) {
  if (!upnp_fetched_) {
    // TODO: this should be done ahead of time, or
    // asynchronously returning XNET_GET_XNADDR_PENDING
    FetchUPnP();
    RegisterPlayer();
    DownloadPortMappings();
  }

  addr_ptr->ina = online_ip_;
  addr_ptr->inaOnline = online_ip_;
  addr_ptr->wPortOnline = getPort();
  memcpy(addr_ptr->abEnet, getMacAddress(), 6);
  memcpy(addr_ptr->abOnline, getMacAddress(), 6);

  auto status = XnAddrStatus::XNET_GET_XNADDR_STATIC |
                XnAddrStatus::XNET_GET_XNADDR_GATEWAY |
                XnAddrStatus::XNET_GET_XNADDR_DNS;

  if (online_ip_.s_addr != 0) {
    status |= XnAddrStatus::XNET_GET_XNADDR_ONLINE;
  } else {
    addr_ptr->inaOnline.S_un.S_addr = 0;
  }

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
DECLARE_XAM_EXPORT1(NetDll_XNetGetTitleXnAddr, kNetworking, kStub);

dword_result_t NetDll_XNetGetDebugXnAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> addr_ptr) {
  addr_ptr.Zero();

  // XNET_GET_XNADDR_NONE causes caller to gracefully return.
  return XnAddrStatus::XNET_GET_XNADDR_NONE;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetDebugXnAddr, kNetworking, kStub);

// TODO: Move - Codie
bool StringToHex(const std::string& inStr, unsigned char* outStr) {
  size_t len = inStr.length();
  for (size_t i = 0; i < len; i += 2) {
    sscanf(inStr.c_str() + i, "%2hhx", outStr);
    ++outStr;
  }
  return true;
}

// TODO: Move - Codie
std::map<xe::be<uint32_t>, xe::be<uint64_t>> machineIdCache{};
std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionIdCache{};
std::map<xe::be<uint32_t>, xe::be<uint64_t>> macAddressCache{};

void clearXnaddrCache() { 
    sessionIdCache.clear();
    macAddressCache.clear();
}

// this works dont touch it
dword_result_t NetDll_XNetXnAddrToMachineId_entry(dword_t caller,
                                                  pointer_t<XNADDR> addr_ptr,
                                                  pointer_t<be<uint64_t>> id_ptr) {
  // Tell the caller we're not signed in to live (non-zero ret)
  //if (addr_ptr->inaOnline.S_un.S_un_b.s_b4 == 170) *id_ptr = 0xFA000000049B679F;
  //else
  //  *id_ptr = 0xFA000000039E7542;

      #pragma region Curl
  /*
      TODO:
          - Refactor the CURL out to a separate class.
          - Use the overlapped task to do this asyncronously.
  */

    if (machineIdCache.find(addr_ptr->inaOnline.S_un.S_addr) != machineIdCache.end()) {
        *id_ptr = machineIdCache[addr_ptr->inaOnline.S_un.S_addr];
        return 0;
    }

  Document d;
  d.SetObject();

  Document::AllocatorType& allocator = d.GetAllocator();

  size_t sz = allocator.Size();

  char ipString[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr_ptr->inaOnline, ipString, INET_ADDRSTRLEN);

  d.AddMember("hostAddress", std::string(ipString), allocator);

  rapidjson::StringBuffer strbuf;
  PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
  d.Accept(writer);

  CURL* curl;
  CURLcode res;

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (curl == NULL) {
    return 128;
  }

  std::stringstream out;

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");

  std::stringstream url;
  url << cvars::api_address << "/players/find";

  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strbuf.GetString());

  res = curl_easy_perform(curl);

  curl_easy_cleanup(curl);
  int httpCode(0);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_global_cleanup();

  if (httpCode == 201) {
    rapidjson::Document d;
    d.Parse(out.str());

    unsigned char machineId[8];

    StringToHex(d["machineId"].GetString(), machineId);

    *id_ptr = SessionIdtoUint64(machineId);
    machineIdCache.emplace(addr_ptr->inaOnline.S_un.S_addr, SessionIdtoUint64(machineId));
  }
#pragma endregion

  //*id_ptr = htonl(addr_ptr->inaOnline.S_un.S_addr);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetXnAddrToMachineId, kNetworking, kStub);

dword_result_t NetDll_XNetConnect_entry(dword_t caller, dword_t) {
  //XELOGI("XNetConnect({:08X}:{})", addr->ina.S_un.S_addr, addr->wPortOnline);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetConnect, kNetworking, kImplemented);

dword_result_t NetDll_XNetGetConnectStatus_entry(dword_t caller,
                                                 dword_t addr) {
  XELOGI("XNetGetConnectStatus({:08X})", addr.value());
  return 2;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetConnectStatus, kNetworking, kImplemented);

dword_result_t NetDll_XNetServerToInAddr_entry(dword_t caller, dword_t addr,
                                               dword_t serviceId,
                                               pointer_t<in_addr> pina) {
  XELOGI("XNetServerToInAddr({:08X} {:08X})", addr.value(),
         pina.guest_address());
  pina->S_un.S_addr = htonl(addr);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetServerToInAddr, kNetworking, kImplemented);

void NetDll_XNetInAddrToString_entry(dword_t caller, dword_t in_addr,
                                     lpstring_t string_out,
                                     dword_t string_size) {
  strncpy(string_out, "666.666.666.666", string_size);
}
DECLARE_XAM_EXPORT1(NetDll_XNetInAddrToString, kNetworking, kStub);

// This converts a XNet address to an IN_ADDR. The IN_ADDR is used for
// subsequent socket calls (like a handle to a XNet address)
dword_result_t NetDll_XNetXnAddrToInAddr_entry(dword_t caller,
                                               pointer_t<XNADDR> xn_addr,
                                               lpvoid_t xid,
                                               pointer_t<in_addr> in_addr) {
  if (xn_addr->inaOnline.s_addr != 0) {
    in_addr->s_addr = xn_addr->inaOnline.s_addr;

    return 0;
  }

  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XNetXnAddrToInAddr, kNetworking, kSketchy);

//static const unsigned char codieSession[] = {0xC0, 0xDE, 0xC0, 0xDE,
//                                           0xC0, 0xDE, 0xC0, 0xDE};
// Does the reverse of the above.
// FIXME: Arguments may not be correct.
dword_result_t NetDll_XNetInAddrToXnAddr_entry(dword_t caller, dword_t in_addr,
                                               pointer_t<XNADDR> xn_addr,
                                               dword_t xid_ptr) {
  xn_addr->ina.S_un.S_addr = ntohl(in_addr);

  //xn_addr->ina.S_un.S_un_b.s_b1 = 0;


  xn_addr->inaOnline.S_un.S_addr = ntohl(in_addr);
  xn_addr->wPortOnline = 36020;

  if (macAddressCache.find(xn_addr->inaOnline.S_un.S_addr) == macAddressCache.end()) {
#pragma region Curl
    /*
        TODO:
            - Refactor the CURL out to a separate class.
            - Use the overlapped task to do this asyncronously.
    */

    Document d;
    d.SetObject();

    Document::AllocatorType& allocator = d.GetAllocator();

    size_t sz = allocator.Size();

    char ipString[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &xn_addr->inaOnline, ipString, INET_ADDRSTRLEN);

    d.AddMember("hostAddress", std::string(ipString), allocator);

    rapidjson::StringBuffer strbuf;
    PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
    d.Accept(writer);

    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl == NULL) {
      return 128;
    }

    std::stringstream out;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "charset: utf-8");

    std::stringstream url;
    url << cvars::api_address << "/players/find";

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strbuf.GetString());

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    int httpCode(0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_global_cleanup();

    if (httpCode == 201) {
      rapidjson::Document d;
      d.Parse(out.str());

      unsigned char macAddress[6];
      unsigned char sessionId[8];

      StringToHex(d["sessionId"].GetString(), sessionId);
      StringToHex(d["macAddress"].GetString(), macAddress);

      sessionIdCache.emplace(xn_addr->inaOnline.S_un.S_addr, SessionIdtoUint64(sessionId));
      macAddressCache.emplace(xn_addr->inaOnline.S_un.S_addr, MacAddresstoUint64(macAddress));
    }
#pragma endregion
  }

  Uint64toMacAddress(macAddressCache[xn_addr->inaOnline.S_un.S_addr], xn_addr->abEnet);
  Uint64toMacAddress(macAddressCache[xn_addr->inaOnline.S_un.S_addr], xn_addr->abOnline);

  if (xid_ptr != 0) {
    auto sessionIdPtr = kernel_memory()->TranslateVirtual<unsigned char*>(xid_ptr);
    auto sessionId = sessionIdCache[xn_addr->inaOnline.S_un.S_addr];

    std::stringstream sessionIdStr;
    sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << sessionId;


    Uint64toSessionId(sessionId, sessionIdPtr);


    // old
    //memcpy(kernel_memory()->TranslateVirtual<unsigned char*>(xid_ptr), mySessionId, 8);
  }
  return 0;

}
DECLARE_XAM_EXPORT1(NetDll_XNetInAddrToXnAddr, kNetworking, kImplemented);

// https://www.google.com/patents/WO2008112448A1?cl=en
// Reserves a port for use by system link
dword_result_t NetDll_XNetSetSystemLinkPort_entry(dword_t caller,
                                                  dword_t port) {
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetSetSystemLinkPort, kNetworking, kStub);

// https://github.com/ILOVEPIE/Cxbx-Reloaded/blob/master/src/CxbxKrnl/EmuXOnline.h#L39
struct XEthernetStatus {
  static const uint32_t XNET_ETHERNET_LINK_ACTIVE = 0x01;
  static const uint32_t XNET_ETHERNET_LINK_100MBPS = 0x02;
  static const uint32_t XNET_ETHERNET_LINK_10MBPS = 0x04;
  static const uint32_t XNET_ETHERNET_LINK_FULL_DUPLEX = 0x08;
  static const uint32_t XNET_ETHERNET_LINK_HALF_DUPLEX = 0x10;
};

dword_result_t NetDll_XNetGetEthernetLinkStatus_entry(dword_t caller) {
  return 1 | 2 | 8;
}
DECLARE_XAM_EXPORT1(NetDll_XNetGetEthernetLinkStatus, kNetworking, kStub);

dword_result_t NetDll_XNetDnsLookup_entry(dword_t caller, lpstring_t host,
                                          dword_t event_handle,
                                          lpdword_t pdns) {
  if (pdns) {
    hostent* ent = gethostbyname(host);

    auto dns_guest = kernel_memory()->SystemHeapAlloc(sizeof(XNDNS));
    auto dns = kernel_memory()->TranslateVirtual<XNDNS*>(dns_guest);

    if (ent == nullptr) {
#ifdef XE_PLATFORM_WIN32
      dns->status = WSAGetLastError();
#else
      dns->status = (int32_t)X_WSAError::X_WSAENETDOWN;
#endif
    } else if (ent->h_addrtype != AF_INET) {
      dns->status = (int32_t)X_WSAError::X_WSANO_DATA;
    } else {
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
  if (event_handle) {
    auto ev =
        kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
    assert_not_null(ev);
    ev->Set(0, false);
  }
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetDnsLookup, kNetworking, kImplemented);

dword_result_t NetDll_XNetDnsRelease_entry(dword_t caller,
                                           pointer_t<XNDNS> dns) {
  if (!dns) {
    return X_STATUS_INVALID_PARAMETER;
  }
  kernel_memory()->SystemHeapFree(dns.guest_address());
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetDnsRelease, kNetworking, kStub);

dword_result_t NetDll_XNetQosServiceLookup_entry(dword_t caller, dword_t flags,
                                                 dword_t event_handle,
                                                 lpdword_t pqos) {
  XELOGI("XNetQosServiceLookup({:08X}, {:08X}, {:08X}, {:08X})", caller.value(),
         flags.value(), event_handle.value(), pqos.guest_address());

  if (pqos) {
    auto qos_guest = kernel_memory()->SystemHeapAlloc(sizeof(XNQOS));
    auto qos = kernel_memory()->TranslateVirtual<XNQOS*>(qos_guest);
    qos->count = 1;

    qos->info[0].probes_xmit = 4;
    qos->info[0].probes_recv = 4;
    qos->info[0].data_len = 1;
    qos->info[0].data_ptr = *(BYTE*)"A";
    qos->info[0].rtt_min_in_msecs = 4;
    qos->info[0].rtt_med_in_msecs = 10;
    qos->info[0].up_bits_per_sec = 13125;
    qos->info[0].down_bits_per_sec = 21058;
    qos->info[0].flags = 1 | 2 | 3;
    qos->count_pending = 0;

    *pqos = qos_guest;
  }
  if (event_handle) {
    auto ev =
        kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
    assert_not_null(ev);
    ev->Set(0, false);
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosServiceLookup, kNetworking, kStub);

dword_result_t NetDll_XNetQosRelease_entry(dword_t caller,
                                           pointer_t<XNQOS> qos) {
  if (!qos) {
    return X_STATUS_INVALID_PARAMETER;
  }
  kernel_memory()->SystemHeapFree(qos.guest_address());
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosRelease, kNetworking, kStub);

// Because qos is handled synronously right now and does a curl,
// and this would be called every second in game for some titles,
// we cache the first request and clear the cache on session modification.
//std::list<xe::be<uint64_t>> qosSentList{};
bool qosSent;
dword_result_t NetDll_XNetQosListen_entry(dword_t caller, lpvoid_t sessionId,
                                          lpvoid_t data, dword_t data_size,
                                          dword_t r7, dword_t flags) {

  XELOGI("XNetQosListen({:08X}, {:016X}, {:016X}, {}, {:08X}, {:08X})",
         caller.value(), sessionId.host_address(), data.host_address(),
         data_size.value(), r7.value(), flags.value());

    if (data_size <= 0) return 0;

  const void* data_ptr = (void*)data.host_address();
    const xe::be<uint64_t>* sessionId_ptr =
        (xe::be<uint64_t>*)sessionId.host_address();

  if (!qosSent) {
#pragma region Curl
      /*
          TODO:
              - Refactor the CURL out to a separate class.
              - Use the overlapped task to do this asyncronously.
      */

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0') << *sessionId_ptr;

      CURL* curl;
      CURLcode res;

      curl_global_init(CURL_GLOBAL_ALL);
      curl = curl_easy_init();
      if (curl == NULL) {
        return 128;
      }

      std::stringstream out;

      struct curl_slist* headers = NULL;

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream url;
      url << cvars::api_address << "/title/" << titleId.str() << "/sessions/"
          << sessionIdStr.str() << "/qos";

      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                       (curl_off_t)data_size);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_ptr);
      curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

      /* enable verbose for easier tracing */
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
      // curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");

      res = curl_easy_perform(curl);

      curl_easy_cleanup(curl);
      curl_global_cleanup();

      qosSent = true;
#pragma endregion
    }


  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosListen, kNetworking, kStub);

void resetQosCache() { qosSent = false; }

dword_result_t NetDll_XNetQosLookup_entry(dword_t caller, dword_t sessionsCount, dword_t unk2, lpvoid_t sessionIdPtrsPtr,
    dword_t unk4,
                                          dword_t unk5, dword_t unk6,
                                          dword_t service_id, dword_t probes_count,
                                          dword_t bits_per_second, dword_t flags,
                                          dword_t event_handle, lpdword_t qos_ptr) {
  if (qos_ptr) {
    auto qos_guest = kernel_memory()->SystemHeapAlloc(sizeof(XNQOS));
    auto qos = kernel_memory()->TranslateVirtual<XNQOS*>(qos_guest);
    qos->count = 1;

    const xe::be<uint32_t> sessionId_ptrs_ptr = *(xe::be<uint32_t>*)sessionIdPtrsPtr.host_address();

    const xe::be<uint64_t>* sessionId_ptr =
        kernel_memory()->TranslateVirtual<xe::be<uint64_t>*>(sessionId_ptrs_ptr);


        #pragma region Curl
    /*
        TODO:
            - Refactor the CURL out to a separate class.
            - Use the overlapped task to do this asyncronously.
    */

    std::stringstream sessionIdStr;
    sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << *sessionId_ptr;

    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl == NULL) {
      return 128;
    }

    std::stringstream out;

    struct curl_slist* headers = NULL;

    std::stringstream titleId;
    titleId << std::hex << std::noshowbase << std::setw(8) << std::setfill('0')
            << kernel_state()->title_id();

    std::stringstream url;
    url << cvars::api_address << "/title/" << titleId.str() << "/sessions/" << sessionIdStr.str() << "/qos";

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    int httpCode(0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_global_cleanup();

    if (httpCode == 200) {
      auto data_ptr = kernel_memory()->SystemHeapAlloc((uint32_t)out.str().length());
      auto data = kernel_memory()->TranslateVirtual<int32_t*>(data_ptr);

      qos->info[0].data_ptr = data_ptr;
      memcpy(data, out.str().c_str(), out.str().length());
      qos->info[0].data_len = (uint16_t)out.str().length();
    }

#pragma endregion




    qos->info[0].probes_xmit = 4;
    qos->info[0].probes_recv = 4;
    qos->info[0].rtt_min_in_msecs = 4;
    qos->info[0].rtt_med_in_msecs = 10;
    qos->info[0].up_bits_per_sec = 13125;
    qos->info[0].down_bits_per_sec = 21058;
    qos->info[0].flags = 1 | 2 | 0x8;
    qos->count_pending = 0;

    *qos_ptr = qos_guest;
  }
  if (event_handle) {
    auto ev =
        kernel_state()->object_table()->LookupObject<XEvent>(event_handle);
    assert_not_null(ev);
    ev->Set(0, false);
  }

  return ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosLookup, kNetworking, kImplemented);

typedef struct {
  DWORD size_of_struct;
  DWORD requests_received_count;
  DWORD probes_received_count;
  DWORD unk;
  DWORD data_replies_sent_count;
  DWORD data_reply_bytes_sent;
  DWORD probe_replies_sent_count;
} XNQOSLISTENSTATS;


dword_result_t NetDll_XNetQosGetListenStats_entry(
    dword_t caller, dword_t unk, dword_t pxnkid, lpdword_t pQosListenStats) {
  XELOGI("XNetQosGetListenStats({:08X}, {:08X}, {:08X}, {:08X})",
         caller.value(), unk.value(), caller.value(), unk.value(),
         pxnkid.value(), pQosListenStats.guest_address());

  if (pQosListenStats) {
    auto qos =
        kernel_memory()->TranslateVirtual<XNQOSLISTENSTATS*>(pQosListenStats.guest_address());
    
    qos->requests_received_count = 1;
    qos->probes_received_count = 1;
    qos->unk = 1;
    qos->data_replies_sent_count = 1;
    qos->data_reply_bytes_sent = 1;
    qos->probe_replies_sent_count = 1;
  }

  return ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(NetDll_XNetQosGetListenStats, kNetworking, kImplemented);

dword_result_t NetDll_inet_addr_entry(lpstring_t addr_ptr) {
  if (!addr_ptr) {
    return -1;
  }

  uint32_t addr = inet_addr(addr_ptr);
  // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-inet_addr#return-value
  // Based on console research it seems like x360 uses old version of inet_addr
  // In case of empty string it return 0 instead of -1
  if (addr == -1 && !addr_ptr.value().length()) {
    return 0;
  }

  return xe::byte_swap(addr);
}
DECLARE_XAM_EXPORT1(NetDll_inet_addr, kNetworking, kImplemented);

BOOL optEnable = TRUE;
dword_result_t NetDll_socket_entry(dword_t caller, dword_t af, dword_t type,
                                   dword_t protocol) {
  XSocket* socket = new XSocket(kernel_state());
  X_STATUS result = socket->Initialize(XSocket::AddressFamily((uint32_t)af),
                                       XSocket::Type((uint32_t)type),
                                       XSocket::Protocol((uint32_t)protocol));

  if (XFAILED(result)) {
    socket->Release();

    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  //socket->SetOption(SOL_SOCKET, 0x5801, &optEnable, sizeof(BOOL));
  //if (type == SOCK_STREAM)
  //  socket->SetOption(SOL_SOCKET, 0x5802, &optEnable, sizeof(BOOL));

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

  X_STATUS status = socket->SetOption(level, optname, optval_ptr, optlen);
  if (XFAILED(status)) {
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

  int native_len = *optlen;
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

  X_STATUS status = socket->IOControl(cmd, arg_ptr);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  // TODO
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_ioctlsocket, kNetworking, kImplemented);

dword_result_t NetDll_bind_entry(dword_t caller, dword_t socket_handle,
                                 pointer_t<XSOCKADDR> name, dword_t namelen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  X_STATUS status = socket->Bind(name, namelen);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_bind, kNetworking, kImplemented);

dword_result_t NetDll_connect_entry(dword_t caller, dword_t socket_handle,
                                    pointer_t<XSOCKADDR> name,
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
                                   pointer_t<XSOCKADDR> addr_ptr,
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
  auto new_socket = socket->Accept(addr_ptr, &native_len);
  if (new_socket) {
    *addrlen_ptr = native_len;

    return new_socket->handle();
  } else {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }
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
    this->count = guest_set->fd_count;
    for (uint32_t i = 0; i < this->count; ++i) {
      auto socket_handle = static_cast<X_HANDLE>(guest_set->fd_array[i]);
      if (socket_handle == -1) {
        this->count = i;
        break;
      }
      // Convert from Xenia -> native
      auto socket =
          kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
      assert_not_null(socket);
      this->sockets[i] = socket;
    }
  }

  void Store(x_fd_set* guest_set) {
    guest_set->fd_count = 0;
    for (uint32_t i = 0; i < this->count; ++i) {
      auto socket = this->sockets[i];
      guest_set->fd_array[guest_set->fd_count++] = socket->handle();
    }
  }

  void Store(fd_set* native_set) {
    FD_ZERO(native_set);
    for (uint32_t i = 0; i < this->count; ++i) {
      FD_SET(this->sockets[i]->native_handle(), native_set);
    }
  }

  void UpdateFrom(fd_set* native_set) {
    uint32_t new_count = 0;
    for (uint32_t i = 0; i < this->count; ++i) {
      auto socket = this->sockets[i];
      if (FD_ISSET(socket->native_handle(), native_set)) {
        this->sockets[new_count++] = socket;
      }
    }
    this->count = new_count;
  }
};

int_result_t NetDll_select_entry(dword_t caller, dword_t nfds,
                                 pointer_t<x_fd_set> readfds,
                                 pointer_t<x_fd_set> writefds,
                                 pointer_t<x_fd_set> exceptfds,
                                 lpvoid_t timeout_ptr) {
  host_set host_readfds = {0};
  fd_set native_readfds = {0};
  if (readfds) {
    host_readfds.Load(readfds);
    host_readfds.Store(&native_readfds);
  }
  host_set host_writefds = {0};
  fd_set native_writefds = {0};
  if (writefds) {
    host_writefds.Load(writefds);
    host_writefds.Store(&native_writefds);
  }
  host_set host_exceptfds = {0};
  fd_set native_exceptfds = {0};
  if (exceptfds) {
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

  // TODO(gibbed): modify ret to be what's actually copied to the guest fd_sets?
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
                                     pointer_t<XSOCKADDR> from_ptr,
                                     lpdword_t fromlen_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    XThread::SetLastError(uint32_t(X_WSAError::X_WSAENOTSOCK));
    return -1;
  }

  uint32_t native_fromlen = fromlen_ptr ? fromlen_ptr.value() : 0;
  int ret = socket->RecvFrom(buf_ptr, buf_len, flags, from_ptr,
                             fromlen_ptr ? &native_fromlen : 0);
  if (fromlen_ptr) {
    *fromlen_ptr = native_fromlen;
  }

  if (ret == -1) {
    XThread::SetLastError(socket->GetLastWSAError());
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
                                   dword_t flags, pointer_t<XSOCKADDR> to_ptr,
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
  }
  return ret;
}
DECLARE_XAM_EXPORT1(NetDll_sendto, kNetworking, kImplemented);

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
                                        pointer_t<XSOCKADDR> addr_ptr,
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
                                        pointer_t<XSOCKADDR> addr_ptr,
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
  X_STATUS status = socket->GetSockName(addr_ptr, &native_len);
  if (XFAILED(status)) {
    XThread::SetLastError(socket->GetLastWSAError());
    return -1;
  }

  *addrlen_ptr = native_len;
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_getsockname, kNetworking, kImplemented);

dword_result_t NetDll_XNetCreateKey_entry(dword_t caller, lpdword_t key_id,
                                          lpdword_t exchange_key) {
  kernel_memory()->Fill(key_id.guest_address(), 8, 0xBE);
  kernel_memory()->Fill(exchange_key.guest_address(), 16, 0xBE);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetCreateKey, kNetworking, kStub);

dword_result_t NetDll_XNetRegisterKey_entry(dword_t caller, lpdword_t key_id,
                                            lpdword_t exchange_key) {
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetRegisterKey, kNetworking, kStub);

dword_result_t NetDll_XNetUnregisterKey_entry(dword_t caller, lpdword_t key_id,
                                              lpdword_t exchange_key) {
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XNetUnregisterKey, kNetworking, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Net);
