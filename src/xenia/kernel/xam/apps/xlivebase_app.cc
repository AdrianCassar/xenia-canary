/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xlivebase_app.h"
#include "xenia/kernel/xenumerator.h"

#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/util/shim_utils.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <winsock2.h>                    // NOLINT(build/include_order)
#elif XE_PLATFORM_LINUX
#include <netinet/in.h>
#endif

#include <xenia/kernel/XLiveAPI.h>

DEFINE_bool(stub_xlivebase, false,
            "Return success for all unimplemented XLiveBase calls.", "Live");

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

// TODO(Gliniak): Find better names for these structures!
struct X_ARGUEMENT_ENTRY {
  xe::be<uint32_t> magic_number;
  xe::be<uint32_t> unk_1;
  xe::be<uint32_t> unk_2;
  xe::be<uint32_t> object_ptr;
};
static_assert_size(X_ARGUEMENT_ENTRY, 0x10);

struct X_ARGUMENT_LIST {
  X_ARGUEMENT_ENTRY entry[32];
  xe::be<uint32_t> argument_count;
};
static_assert_size(X_ARGUMENT_LIST, 0x204);

XLiveBaseApp::XLiveBaseApp(KernelState* kernel_state)
    : App(kernel_state, 0xFC) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

X_HRESULT XLiveBaseApp::DispatchMessageSync(uint32_t message,
                                            uint32_t buffer_ptr,
                                            uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x00058004: {
      // Called on startup, seems to just return a bool in the buffer.
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetLogonId({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // ?
      return X_E_SUCCESS;
    }
    case 0x00058006: {
      assert_true(!buffer_length || buffer_length == 4);
      XELOGD("XLiveBaseGetNatType({:08X})", buffer_ptr);
      xe::store_and_swap<uint32_t>(buffer + 0, 1);  // XONLINE_NAT_OPEN
      return X_E_SUCCESS;
    }
    case 0x00058007: {
      // Occurs if title calls XOnlineGetServiceInfo, expects dwServiceId
      // and pServiceInfo. pServiceInfo should contain pointer to
      // XONLINE_SERVICE_INFO structure.
      XELOGD("CXLiveLogon::GetServiceInfo({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return GetServiceInfo(buffer_ptr, buffer_length);
    }
    case 0x00050008: {
      // Required to be successful for 534507D4
      XELOGD("XUserCheckPrivilege({:08x}, {:08x}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050079: {
      // Fixes Xbox Live error for 454107DB
      XELOGD("XLiveBaseUnk50079({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058020: {
      // 0x00058004 is called right before this.
      // We should create a XamEnumerate-able empty list here, but I'm not
      // sure of the format.
      // buffer_length seems to be the same ptr sent to 0x00058004.
      XELOGD("CXLiveFriends::Enumerate({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return CreateFriendsEnumerator(buffer_length);
    }
    case 0x00058023: {
      XELOGD(
          "CXLiveMessaging::XMessageGameInviteGetAcceptedInfo({:08X}, {:08X}) "
          "unimplemented",
          buffer_ptr, buffer_length);
      return X_E_FAIL;
    }
    case 0x00058037: {
      XELOGD("XPresenceInitialize({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058046: {
      // Required to be successful for 4D530910 to detect signed-in profile
      // Doesn't seem to set anything in the given buffer, probably only takes
      // input
      XELOGD("XLiveBaseUnk58046({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058017: {
      XELOGD("UserFindUsers({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005000B: {
      // Fixes Xbox Live error for 43430821
      XELOGD("XLiveBaseUnk5000B({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005800E: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XLiveBaseUnk5800E({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005000D: {
      // Fixes hang when leaving session for 545107D5
      XELOGD("XLiveBaseUnk5000D({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058035: {
      // Fixes Xbox Live error for 513107D9
      // Required for 534507D4
      XELOGD("XLiveBaseUnk58035({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050036: {
      XELOGD("XOnlineQuerySearch({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050009: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XLiveBaseUnk50009({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
  }

  auto xlivebase_log = fmt::format(
      "{} XLIVEBASE message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      cvars::stub_xlivebase ? "Stubbed" : "Unimplemented", app_id(), message,
      buffer_ptr, buffer_length);

  XELOGE("{}", xlivebase_log);

  return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
}

X_HRESULT XLiveBaseApp::GetServiceInfo(uint32_t serviceid,
                                       uint32_t serviceinfo) {
  if (serviceinfo == NULL) {
    return X_E_SUCCESS;
  }

  XLiveAPI::XONLINE_SERVICE_INFO* service_info =
      reinterpret_cast<XLiveAPI::XONLINE_SERVICE_INFO*>(
          memory_->TranslateVirtual(serviceinfo));

  memset(service_info, 0, sizeof(XLiveAPI::XONLINE_SERVICE_INFO));

  XLiveAPI::XONLINE_SERVICE_INFO retrieved_service_info =
      XLiveAPI::GetServiceInfoById(serviceid);

  if (retrieved_service_info.ip.s_addr == 0) {
    return 0x80151100;  // ERROR_SERVICE_NOT_FOUND
    // return 0x80151802;   // ERROR_CONNECTION_INVALID
    // return -1;           // ERROR_FUNCTION_FAILED
  }

  service_info->ip.s_addr = retrieved_service_info.ip.s_addr;
  service_info->port = retrieved_service_info.port;

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::CreateFriendsEnumerator(uint32_t buffer_args) {
  if (!buffer_args) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_ARGUMENT_LIST* arg_list =
      memory->TranslateVirtual<X_ARGUMENT_LIST*>(buffer_args);

  if (arg_list->argument_count <= 3) {
    assert_always(
        "XLiveBaseApp::CreateFriendsEnumerator - Invalid argument count!");
  }

  const uint32_t user_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[0].object_ptr));
  const uint32_t friends_starting_index = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[1].object_ptr));
  const uint32_t friends_amount = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(arg_list->entry[2].object_ptr));

  if (friends_starting_index >= 0x64) {
    return X_E_INVALIDARG;
  }

  if (friends_amount > 0x64) {
    return X_E_INVALIDARG;
  }

  const uint32_t buffer_address = arg_list->entry[3].object_ptr;
  const uint32_t handle_address = arg_list->entry[4].object_ptr;

  if (!buffer_address) {
    return X_E_INVALIDARG;
  }

  if (!handle_address) {
    return X_E_INVALIDARG;
  }

  uint32_t* buffer_ptr = memory->TranslateVirtual<uint32_t*>(buffer_address);
  uint32_t* handle_ptr = memory->TranslateVirtual<uint32_t*>(handle_address);

  // TODO(Gliniak): Enumerator itself stores user_index XUID at enumerator
  // address + 0x24
  auto e =
      make_object<XStaticUntypedEnumerator>(kernel_state_, friends_amount, 0);
  auto result = e->Initialize(-1, app_id(), 0x58021, 0x58022, 0, 0x10, nullptr);
  
  const uint32_t received_friends_count = 0; 
  *buffer_ptr = xe::byte_swap<uint32_t>(received_friends_count * 0xC4);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());
  return X_E_SUCCESS;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
