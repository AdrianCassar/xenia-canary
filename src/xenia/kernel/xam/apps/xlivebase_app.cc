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

#include "xenia/kernel/XLiveAPI.h"

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
    case 0x00058003: {
      // Called on startup of dashboard (netplay build)
      XELOGD("XLiveBaseLogonGetHR({:08X}, {:08X})", buffer_ptr, buffer_length);
      return X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED;
    }
    case 0x0005008C: {
      // Called on startup of blades dashboard v1888 to v2858
      XELOGD("XLiveBaseUnk5008C, unimplemented");
      return X_E_FAIL;
    }
    case 0x00050094: {
      // Called on startup of blades dashboard v4532 to v4552
      XELOGD("XLiveBaseUnk50094, unimplemented");
      return X_E_FAIL;
    }
    case 0x00050008: {
      // Required to be successful for 534507D4
      // Guess:
      // XStorageDownloadToMemory -> XStorageDownloadToMemoryGetProgress
      XELOGD(
          "XStorageDownloadToMemoryGetProgress({:08x}, {:08x}) unimplemented",
          buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050009: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XStorageDownloadToMemory({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XStorageDownloadToMemory(buffer_ptr);
    }
    case 0x0005000A: {
      XELOGD("XStorageEnumerate({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005000B: {
      // Fixes Xbox Live error for 43430821
      XELOGD("XStorageUploadFromMemory({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XStorageUploadFromMemory(buffer_ptr);
    }
    case 0x0005000C: {
      XELOGD("XStringVerify({:08X} {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr, buffer_length);
    }
    case 0x0005000D: {
      // Fixes hang when leaving session for 545107D5
      // 415607D2 says this is XStringVerify
      XELOGD("XStringVerify({:08X}, {:08X})", buffer_ptr, buffer_length);
      return XStringVerify(buffer_ptr, buffer_length);
    }
    case 0x0005000E: {
      // Before every call there is a call to XUserFindUsers
      // Success stub:
      // 584113E8 successfully creates session.
      // 58410B5D craches.
      XELOGD("XUserFindUsersResponseSize({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
    }
    case 0x0005000F: {
      // 41560855 included from TU 7
      // Attempts to set a dvar for ui_email_address but fails on
      // WideCharToMultiByte
      //
      // 4D530AA5 encounters "Failed to retrieve account credentials".
      XELOGD("_XAccountGetUserInfo({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_ERROR_FUNCTION_FAILED;
    }
    case 0x00050010: {
      XELOGD("XAccountGetUserInfo({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_ERROR_FUNCTION_FAILED;
    }
    case 0x0005801C: {
      // Called on blades dashboard v1888
      XELOGD("XLiveBaseUnk5801C({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return Unk5801C(buffer_length);
    }
    case 0x00058024: {
      // Called on blades dashboard v1888
      XELOGD("XLiveBaseUnk58024({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return Unk58024(buffer_length);
    }
    case 0x00050036: {
      XELOGD("XOnlineQuerySearch({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050077: {
      // Called on blades dashboard v1888
      // Current Balance in sub menus:
      // All New Demos and Trailers
      // More Videos and Downloads
      XELOGD("XLiveBaseUnk50077({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050079: {
      // Fixes Xbox Live error for 454107DB
      XELOGD("XLiveBaseUnk50079({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005008B: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Featured Downloads.
      XELOGD("XLiveBaseUnk5008B({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005008F: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace sub menus:
      // All New Demos and Trailers
      // More Videos and Downloads
      XELOGD("XLiveBaseUnk5008F({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050090: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Game Downloads->All Games->Xbox Live Arcade
      // sub menu.
      XELOGD("XLiveBaseUnk50090({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050091: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Game Downloads.
      XELOGD("XLiveBaseUnk50091({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00050097: {
      // Called on blades dashboard v1888
      // Fixes accessing marketplace Memberships.
      XELOGD("XLiveBaseUnk50097({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
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
    case 0x00058009: {
      XELOGD("XContentGetMarketplaceCounts({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005800C: {
      XELOGD("XUserMuteListSetState({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);

      struct MuteListSetState {
        xe::be<uint32_t> user_index;
        xe::be<uint64_t> remote_xuid;
        bool set_muted;
      };

      MuteListSetState* mute_list_ptr =
          memory_->TranslateVirtual<MuteListSetState*>(buffer_ptr);

      mute_list_ptr->set_muted = !mute_list_ptr->set_muted;

      return X_E_SUCCESS;
    }
    case 0x0005800E: {
      // Fixes Xbox Live error for 513107D9
      XELOGD("XUserMuteListQuery({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058017: {
      XELOGD("XUserFindUsers({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058019: {
      XELOGD("XPresenceCreateEnumerator({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
    case 0x0005801E: {
      XELOGD("XPresenceSubscribe({:08X}, {:08X}) unimplemented", buffer_ptr,
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
      return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
    }
    case 0x00058032: {
      XELOGD("XGetTaskProgress({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058035: {
      // Fixes Xbox Live error for 513107D9
      // Required for 534507D4
      XELOGD("XStorageBuildServerPath({:08X}, {:08X})", buffer_ptr,
             buffer_length);
      return XStorageBuildServerPath(buffer_ptr);
    }
    case 0x00058037: {
      // Used in older games such as Crackdown, FM2, Saints Row 1
      XELOGD("XPresenceInitializeLegacy({:08X}, {:08X}) unimplemented",
             buffer_ptr, buffer_length);
      return XPresenceInitialize(buffer_length);
    }
    case 0x00058044: {
      XELOGD("XPresenceUnsubscribe({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return X_E_SUCCESS;
    }
    case 0x00058046: {
      // Used in newer games such as Forza 4, MW3, FH2
      //
      // Required to be successful for 4D530910 to detect signed-in profile
      // Doesn't seem to set anything in the given buffer, probably only takes
      // input
      XELOGD("XPresenceInitialize({:08X}, {:08X}) unimplemented", buffer_ptr,
             buffer_length);
      return XPresenceInitialize(buffer_length);
    }
  }

  auto xlivebase_log = fmt::format(
      "{} XLIVEBASE message app={:08X}, msg={:08X}, buffer_ptr={:08X}, "
      "buffer_length={:08X}",
      cvars::stub_xlivebase ? "Stubbed" : "Unimplemented", app_id(), message,
      buffer_ptr, buffer_length);

  XELOGE("{}", xlivebase_log);

  return cvars::stub_xlivebase ? X_E_SUCCESS : X_E_FAIL;
}

X_HRESULT XLiveBaseApp::XPresenceInitialize(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  X_ARGUEMENT_ENTRY* entry =
      memory->TranslateVirtual<X_ARGUEMENT_ENTRY*>(buffer_length);

  uint32_t max_peer_subscriptions =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(entry->object_ptr));

  if (max_peer_subscriptions > 0x190) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::GetServiceInfo(uint32_t serviceid,
                                       uint32_t serviceinfo) {
  if (serviceinfo == NULL) {
    return X_E_SUCCESS;
  }

  XONLINE_SERVICE_INFO* service_info = reinterpret_cast<XONLINE_SERVICE_INFO*>(
      memory_->TranslateVirtual(serviceinfo));

  memset(service_info, 0, sizeof(XONLINE_SERVICE_INFO));

  XONLINE_SERVICE_INFO retrieved_service_info =
      XLiveAPI::GetServiceInfoById(serviceid);

  if (retrieved_service_info.ip.s_addr == 0) {
    return X_ERROR_SERVICE_NOT_FOUND;
    // return X_ERROR_CONNECTION_INVALID;
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

  if (XFAILED(result)) {
    return result;
  }

  const uint32_t received_friends_count = 0;
  *buffer_ptr = xe::byte_swap<uint32_t>(received_friends_count * 0xC4);

  *handle_ptr = xe::byte_swap<uint32_t>(e->handle());
  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStringVerify(uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t* data_ptr =
      kernel_state_->memory()->TranslateVirtual<uint32_t*>(buffer_ptr);

  // TODO(Gliniak): Figure out structure after marshaling.
  // Based on what game does there must be some structure that
  // checks if string is proper.
  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageDownloadToMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::XStorageUploadFromMemory(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  return X_E_SUCCESS;
}

struct XStorageBuildServerPathArgs {
  xe::be<uint32_t> user_index;
  char unk[12];
  xe::be<uint32_t> storage_location;  // 2 means title specific storage,
                                      // something like developers storage.
  xe::be<uint32_t> storage_location_info_ptr;
  xe::be<uint32_t> storage_location_info_size;
  xe::be<uint32_t> file_name_ptr;
  xe::be<uint32_t> server_path_ptr;
  xe::be<uint32_t> server_path_length_ptr;
};

X_HRESULT XLiveBaseApp::XStorageBuildServerPath(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return X_E_INVALIDARG;
  }

  XStorageBuildServerPathArgs* args =
      kernel_state_->memory()->TranslateVirtual<XStorageBuildServerPathArgs*>(
          buffer_ptr);

  uint8_t* filename_ptr =
      kernel_state_->memory()->TranslateVirtual<uint8_t*>(args->file_name_ptr);
  const std::string filename =
      xe::to_utf8(load_and_swap<std::u16string>(filename_ptr));

  XELOGI("XStorageBuildServerPath: Requesting file: {} From storage type: {}",
         filename, static_cast<uint32_t>(args->storage_location));

  if (args->server_path_ptr) {
    const std::string server_path = fmt::format(
        "title/{:08X}/storage/{}", kernel_state()->title_id(), filename);

    const std::string endpoint_API =
        fmt::format("{}{}", XLiveAPI::GetApiAddress(), server_path);

    uint8_t* server_path_ptr =
        kernel_state_->memory()->TranslateVirtual<uint8_t*>(
            args->server_path_ptr);

    std::memcpy(server_path_ptr, endpoint_API.c_str(), endpoint_API.size());

    uint32_t* server_path_length =
        kernel_state_->memory()->TranslateVirtual<uint32_t*>(
            args->server_path_length_ptr);

    *server_path_length =
        xe::byte_swap<uint32_t>(uint32_t(endpoint_API.size()));
  }

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unk58024(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  struct data {
    X_ARGUEMENT_ENTRY xuid;
    X_ARGUEMENT_ENTRY ukn2;  // 125
    X_ARGUEMENT_ENTRY ukn3;  // 0
  };

  data* entry = memory->TranslateVirtual<data*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(
      memory->TranslateVirtual(entry->xuid.object_ptr));
  uint32_t ukn2 = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn2.object_ptr));
  uint32_t ukn3_ptr = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn3.object_ptr));

  return X_E_SUCCESS;
}

X_HRESULT XLiveBaseApp::Unk5801C(uint32_t buffer_length) {
  if (!buffer_length) {
    return X_E_INVALIDARG;
  }

  Memory* memory = kernel_state_->memory();

  struct data {
    X_ARGUEMENT_ENTRY xuid;
    X_ARGUEMENT_ENTRY ukn2;
    X_ARGUEMENT_ENTRY ukn3;
  };

  data* entry = memory->TranslateVirtual<data*>(buffer_length);

  uint64_t xuid = xe::load_and_swap<uint64_t>(
      memory->TranslateVirtual(entry->xuid.object_ptr));
  uint32_t ukn2 = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn2.object_ptr));
  uint32_t ukn3_ptr = xe::load_and_swap<uint32_t>(
      memory->TranslateVirtual(entry->ukn3.object_ptr));

  return X_E_SUCCESS;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
