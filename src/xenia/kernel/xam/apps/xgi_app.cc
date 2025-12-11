/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xgi_app.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/json/read_user_stats_object_json.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xsession.h"

using namespace rapidjson;
using namespace xe::string_util;

DECLARE_bool(logging);

namespace xe {
namespace kernel {
namespace xam {
namespace apps {
/*
 * Most of the structs below were found in the Source SDK, provided as stubs.
 * Specifically, they can be found in the Source 2007 SDK and the Alien Swarm
 * Source SDK. Both are available on Steam for free. A GitHub mirror of the
 * Alien Swarm SDK can be found here:
 * https://github.com/NicolasDe/AlienSwarm/blob/master/src/common/xbox/xboxstubs.h
 */

struct XGI_XUSER_ACHIEVEMENT {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> achievement_id;
};
static_assert_size(XGI_XUSER_ACHIEVEMENT, 0x8);

struct XGI_XUSER_GET_PROPERTY {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> unused;
  xe::be<uint64_t> xuid;  // If xuid is 0 then user_index is used.
  xe::be<uint32_t>
      property_size_ptr;  // Normally filled with sizeof(XUSER_PROPERTY), with
                          // exception of binary and wstring type.
  xe::be<uint32_t> context_address;
  xe::be<uint32_t> property_address;
};
static_assert_size(XGI_XUSER_GET_PROPERTY, 0x20);

struct XGI_XUSER_SET_CONTEXT {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> unused;
  xe::be<uint64_t> xuid;
  XUSER_CONTEXT context;
};
static_assert_size(XGI_XUSER_SET_CONTEXT, 0x18);

struct XGI_XUSER_SET_PROPERTY {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> unused;
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> property_id;
  xe::be<uint32_t> data_size;
  xe::be<uint32_t> data_address;
};
static_assert_size(XGI_XUSER_SET_PROPERTY, 0x20);

// ANID = Anonymous user id
struct XGI_XUSER_ANID {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> AnId_buffer_size;
  xe::be<uint32_t> AnId_buffer_ptr;  // char*
  xe::be<uint32_t> block;            // 1
};
static_assert_size(XGI_XUSER_ANID, 0x10);

struct XGI_XUSER_STATS_RESET {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> view_id;
};
static_assert_size(XGI_XUSER_STATS_RESET, 0x8);

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

X_HRESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x000B0018: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_MODIFY));

      XGI_SESSION_MODIFY* data = reinterpret_cast<XGI_SESSION_MODIFY*>(buffer);

      XELOGI("XSessionModify({:08X} {:08X} {:08X} {:08X})", data->obj_ptr.get(),
             data->flags.get(), data->maxPublicSlots.get(),
             data->maxPrivateSlots.get());

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->ModifySession(data);
    }
    case 0x000B0016: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH));
      XELOGI("XSessionSearch");

      XGI_SESSION_SEARCH* data = reinterpret_cast<XGI_SESSION_SEARCH*>(buffer);

      const uint32_t num_users = kernel_state()
                                     ->xam_state()
                                     ->profile_manager()
                                     ->SignedInProfilesCount();

      const auto xlast =
          kernel_state_->emulator()->game_info_database()->GetXLast();

      return XSession::GetSessions(kernel_state_, data, num_users);
    }
    case 0x000B001C: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH_EX));
      XELOGI("XSessionSearchEx");

      XGI_SESSION_SEARCH_EX* data =
          reinterpret_cast<XGI_SESSION_SEARCH_EX*>(buffer);

      return XSession::GetSessions(kernel_state_, &data->session_search,
                                   data->num_users);
    }
    case 0x000B001D: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_DETAILS));
      XELOGI("XSessionGetDetails({:08X});", buffer_length);

      XGI_SESSION_DETAILS* data =
          reinterpret_cast<XGI_SESSION_DETAILS*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->GetSessionDetails(data);
    }
    case 0x000B001E: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_MIGRATE));
      XELOGI("XSessionMigrateHost");

      XGI_SESSION_MIGRATE* data =
          reinterpret_cast<XGI_SESSION_MIGRATE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      XSESSION_INFO* session_info_ptr =
          memory_->TranslateVirtual<XSESSION_INFO*>(data->session_info_ptr);

      if (data->session_info_ptr == NULL) {
        XELOGI("Session Migration Failed");
        return X_E_FAIL;
      }

      return session->MigrateHost(data);
    }
    case 0x000B0021: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_READ_STATS));
      XELOGI("XUserReadStats");

      XGI_XUSER_READ_STATS* data =
          reinterpret_cast<XGI_XUSER_READ_STATS*>(buffer);

      if (!data->results_ptr) {
        return X_E_INVALIDARG;
      }

      // 584107D7 caches results
      X_USER_STATS_READ_RESULTS* results =
          kernel_memory()->TranslateVirtual<X_USER_STATS_READ_RESULTS*>(
              data->results_ptr);

      std::memset(results, 0, sizeof(X_USER_STATS_READ_RESULTS));

      if (data->xuids_count > X_STATS_MAX_USER_COUNT) {
        return X_E_INVALIDARG;
      }

      // 4D5307EA reads 6 leaderboards, 5 standard and 1 skill.
      assert_false(data->specs_count > XUserMaxReadStatsViews + 1);

      if (data->specs_count > XUserMaxReadStatsViews + 1) {
        return X_E_INVALIDARG;
      }

      std::unique_ptr<LeaderboardObjectJSON> leaderboards =
          XLiveAPI::LeaderboardsFind(*data);

      const X_USER_STATS_READ_RESULTS& read_results =
          leaderboards->GetReadStatsResults();

      results->views_ptr = read_results.views_ptr;
      results->num_views = read_results.num_views;

      // Validation

      assert_not_zero(read_results.views_ptr);

      if (!read_results.views_ptr) {
        return X_ONLINE_E_LOGON_NOT_LOGGED_ON;
      }

      assert_false(results->num_views != data->specs_count);

      const X_USER_STATS_SPEC* stats_specs =
          kernel_memory()->TranslateVirtual<X_USER_STATS_SPEC*>(
              data->specs_ptr);

      const X_USER_STATS_VIEW* views_ptr =
          kernel_memory()->TranslateVirtual<X_USER_STATS_VIEW*>(
              results->views_ptr);

      // 545107D4 uses same view id twice?
      for (uint32_t spec_index = 0; spec_index < data->specs_count;
           spec_index++) {
        const X_USER_STATS_SPEC stat_spec_ptr = stats_specs[spec_index];
        const X_USER_STATS_VIEW view_ptr = views_ptr[spec_index];
        const uint32_t view_id = stat_spec_ptr.view_id;

        const auto spa_stats_view =
            kernel_state()->emulator()->game_info_database()->GetStatsView(
                view_id);

        // TrueSkill leaderboards are not defined in SPA?
        if (IsTrueSkillViewID(view_id)) {
          XELOGI("TrueSkill View ID: {:08X}", view_id);
        }
      }

      return X_E_SUCCESS;
    }
    case 0x000B001A: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_ARBITRATION));

      XGI_SESSION_ARBITRATION* data =
          reinterpret_cast<XGI_SESSION_ARBITRATION*>(buffer);

      XELOGI(
          "XSessionArbitrationRegister({:08X}, {:08X}, {:08X}, {:08X}, {:08X})",
          data->obj_ptr.get(), data->flags.get(), data->session_nonce.get(),
          data->results_buffer_size.get(), data->results_ptr.get());

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->RegisterArbitration(data);
    }
    case 0x000B0006: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_SET_CONTEXT));
      const XGI_XUSER_SET_CONTEXT* xgi_context =
          reinterpret_cast<const XGI_XUSER_SET_CONTEXT*>(buffer);

      XELOGD("XGIUserSetContext({:08X}, ID: {:08X}, Value: {:08X})",
             xgi_context->user_index.get(),
             xgi_context->context.context_id.get(),
             xgi_context->context.value.get());

      UserProfile* user = nullptr;
      if (xgi_context->xuid != 0) {
        user = kernel_state_->xam_state()->GetUserProfile(xgi_context->xuid);
      } else {
        user =
            kernel_state_->xam_state()->GetUserProfile(xgi_context->user_index);
      }

      if (user) {
        kernel_state_->xam_state()->user_tracker()->UpdateContext(
            user->xuid(), xgi_context->context.context_id,
            xgi_context->context.value);

        std::u16string context_desc =
            kernel_state()->xam_state()->user_tracker()->GetContextDescription(
                user->xuid(), xgi_context->context.context_id);

        if (!context_desc.empty()) {
          XELOGD("Set {}", xe::to_utf8(context_desc));
        }
      }

      return X_E_SUCCESS;
    }
    case 0x000B0007: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_SET_PROPERTY));
      const XGI_XUSER_SET_PROPERTY* xgi_property =
          reinterpret_cast<const XGI_XUSER_SET_PROPERTY*>(buffer);

      XELOGD("XGIUserSetPropertyEx({:08X}, {:08X}, {}, {:08X})",
             xgi_property->user_index.get(), xgi_property->property_id.get(),
             xgi_property->data_size.get(), xgi_property->data_address.get());

      UserProfile* user = nullptr;
      if (xgi_property->xuid != 0) {
        user = kernel_state_->xam_state()->GetUserProfile(xgi_property->xuid);
      } else {
        user = kernel_state_->xam_state()->GetUserProfile(
            xgi_property->user_index);
      }

      if (user) {
        // 4D5307D5 will provide null pointer for unexpected property from
        // XSessionSearch.
        if (!xgi_property->data_address) {
          XELOGI(
              "XGIUserSetPropertyEx setting property {:08X} without "
              "data_address!",
              xgi_property->property_id.get());
          assert_always();
          return X_E_SUCCESS;
        }

        Property property(
            xgi_property->property_id,
            Property::get_valid_data_size(xgi_property->property_id,
                                          xgi_property->data_size),
            memory_->TranslateVirtual<uint8_t*>(xgi_property->data_address));

        kernel_state_->xam_state()->user_tracker()->AddProperty(user->xuid(),
                                                                &property);

        std::u16string property_desc =
            kernel_state_->xam_state()->user_tracker()->GetPropertyDescription(
                xgi_property->property_id);

        if (!property_desc.empty()) {
          XELOGD("Set {}", xe::to_utf8(property_desc));
        }
      }
      return X_E_SUCCESS;
    }
    case 0x000B0008: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_ACHIEVEMENT));
      uint32_t achievement_count = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t achievements_ptr = xe::load_and_swap<uint32_t>(buffer + 4);
      XELOGD("XGIUserWriteAchievements({:08X}, {:08X})", achievement_count,
             achievements_ptr);

      auto* achievement =
          memory_->TranslateVirtual<XGI_XUSER_ACHIEVEMENT*>(achievements_ptr);
      for (uint32_t i = 0; i < achievement_count; i++, achievement++) {
        kernel_state_->achievement_manager()->EarnAchievement(
            achievement->user_index, kernel_state_->title_id(),
            achievement->achievement_id);
      }
      return X_E_SUCCESS;
    }
    case 0x000B0010: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_CREATE));
      XELOGI("XSessionCreate({:08X}, {:08X})", buffer_ptr, buffer_length);
      // Sequence:
      // - XamSessionCreateHandle
      // - XamSessionRefObjByHandle
      // - [this]
      // - CloseHandle

      XGI_SESSION_CREATE* data = reinterpret_cast<XGI_SESSION_CREATE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      const auto result = session->CreateSession(
          data->user_index, data->num_slots_public, data->num_slots_private,
          data->flags, data->session_info_ptr, data->nonce_ptr);

      XLiveAPI::clearXnaddrCache();
      return result;
    }
    case 0x000B0011: {
      assert_true(!buffer_length || buffer_length == sizeof(XGI_SESSION_STATE));
      XELOGI("XGISessionDelete");

      XGI_SESSION_STATE* data = reinterpret_cast<XGI_SESSION_STATE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      const X_RESULT result = session->DeleteSession(data);
      session->ReleaseHandle();

      return result;
    }
    case 0x000B0012: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_MANAGE));
      XELOGI("XSessionJoin");

      XGI_SESSION_MANAGE* data = reinterpret_cast<XGI_SESSION_MANAGE*>(buffer);
      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      const auto result = session->JoinSession(data);
      XLiveAPI::clearXnaddrCache();
      return result;
    }
    case 0x000B0013: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_MANAGE));
      XELOGI("XSessionLeave");

      const auto data = reinterpret_cast<XGI_SESSION_MANAGE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      const auto result = session->LeaveSession(data);
      XLiveAPI::clearXnaddrCache();

      return result;
    }
    case 0x000B0014: {
      // Gets 584107FB in game.
      // get high score table?
      assert_true(!buffer_length || buffer_length == sizeof(XGI_SESSION_STATE));
      XELOGI("XSessionStart");

      XGI_SESSION_STATE* data = reinterpret_cast<XGI_SESSION_STATE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->StartSession(data);
    }
    case 0x000B0015: {
      // send high scores?
      assert_true(!buffer_length || buffer_length == sizeof(XGI_SESSION_STATE));
      XELOGI("XSessionEnd");

      XGI_SESSION_STATE* data = reinterpret_cast<XGI_SESSION_STATE*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->EndSession(data);
    }
    case 0x000B0025: {
      assert_true(!buffer_length || buffer_length == sizeof(XGI_STATS_WRITE));

      XGI_STATS_WRITE* data = reinterpret_cast<XGI_STATS_WRITE*>(buffer);

      XELOGI("XSessionWriteStats({:08X}, {:016X}, {:08X}, {:08X})",
             data->obj_ptr.get(), data->xuid.get(), data->num_views.get(),
             data->views_ptr.get());

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->WriteStats(data);
    }
    case 0x000B001B: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH_BYID));
      XELOGI("XSessionSearchByID");

      XGI_SESSION_SEARCH_BYID* data =
          reinterpret_cast<XGI_SESSION_SEARCH_BYID*>(buffer);

      return XSession::GetSessionByID(memory_, data);
    }
    case 0x000B0060: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH_BYIDS));
      XELOGI("XSessionSearchByIds");

      XGI_SESSION_SEARCH_BYIDS* data =
          reinterpret_cast<XGI_SESSION_SEARCH_BYIDS*>(buffer);

      const X_RESULT result = XSession::GetSessionByIDs(memory_, data);

      SEARCH_RESULTS* search_results =
          memory_->TranslateVirtual<SEARCH_RESULTS*>(data->search_results_ptr);

      XELOGI("XSessionSearchByIds found {} session(s).",
             search_results->header.search_results_count.get());

      return result;
    }
    case 0x000B0065: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH_WEIGHTED));
      XELOGI("XSessionSearchWeighted");

      XGI_SESSION_SEARCH_WEIGHTED* data =
          reinterpret_cast<XGI_SESSION_SEARCH_WEIGHTED*>(buffer);

      const uint32_t num_users = kernel_state()
                                     ->xam_state()
                                     ->profile_manager()
                                     ->SignedInProfilesCount();

      return XSession::GetWeightedSessions(kernel_state_, data, num_users);
    }
    case 0x000B0026: {
      // 4D5307EA
      assert_true(!buffer_length || buffer_length == sizeof(XGI_STATS_WRITE));

      XGI_STATS_WRITE* data = reinterpret_cast<XGI_STATS_WRITE*>(buffer);

      XELOGI("XSessionFlushStats({:08X}, {:016X}, {:08X}, {:08X})",
             data->obj_ptr.get(), data->xuid.get(), data->num_views.get(),
             data->views_ptr.get());

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->FlushStats();
    }
    case 0x000B001F: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_MODIFYSKILL));
      XELOGI("XSessionModifySkill");

      XGI_SESSION_MODIFYSKILL* data =
          reinterpret_cast<XGI_SESSION_MODIFYSKILL*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->ModifySkill(data);
    }
    case 0x000B0020: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_STATS_RESET));
      // 545107D4
      XELOGI("XUserResetStatsView");

      XGI_XUSER_STATS_RESET* data =
          reinterpret_cast<XGI_XUSER_STATS_RESET*>(buffer);

      return X_E_SUCCESS;
    }
    case 0x000B0019: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_INVITE));
      XELOGI("XSessionGetInvitationData unimplemented");

      XGI_SESSION_INVITE* data = reinterpret_cast<XGI_SESSION_INVITE*>(buffer);

      return X_E_SUCCESS;
    }
    case 0x000B0036: {
      // Called after opening xbox live arcade and clicking on xbox live v5759
      // to 5787 and called after clicking xbox live in the game library from
      // v6683 to v6717
      // Does not get sent a buffer
      XELOGD("XInvalidateGamerTileCache, unimplemented");
      return X_E_FAIL;
    }
    case 0x000B003D: {
      assert_true(!buffer_length || buffer_length == sizeof(XGI_XUSER_ANID));

      // Used in 5451082A, 5553081E
      // XUserGetCachedANID
      XELOGI("XUserGetANID");
      XGI_XUSER_ANID* data = reinterpret_cast<XGI_XUSER_ANID*>(buffer);

      if (!kernel_state()->xam_state()->IsUserSignedIn(data->user_index)) {
        return X_ERROR_NOT_LOGGED_ON;
      }

      uint8_t* AnIdBuffer =
          memory_->TranslateVirtual<uint8_t*>(data->AnId_buffer_ptr);

      // Game calls HexDecodeDigit on AnIdBuffer
      for (uint32_t i = 0; i < data->AnId_buffer_size - 1; i++) {
        AnIdBuffer[i] = i % 16;
      }

      return X_E_SUCCESS;
    }
    case 0x000B0041: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_XUSER_GET_PROPERTY));
      const XGI_XUSER_GET_PROPERTY* xgi_property =
          reinterpret_cast<const XGI_XUSER_GET_PROPERTY*>(buffer);

      UserProfile* user = nullptr;
      if (xgi_property->xuid != 0) {
        user = kernel_state_->xam_state()->GetUserProfile(xgi_property->xuid);
      } else {
        user = kernel_state_->xam_state()->GetUserProfile(
            xgi_property->user_index);
      }

      if (!user) {
        XELOGD(
            "XGIUserGetProperty - Invalid user provided: Index: {:08X} XUID: "
            "{:16X}",
            xgi_property->user_index.get(), xgi_property->xuid.get());
        return X_E_NOTFOUND;
      }

      // Process context
      if (xgi_property->context_address) {
        XUSER_CONTEXT* context = memory_->TranslateVirtual<XUSER_CONTEXT*>(
            xgi_property->context_address);

        XELOGD("XGIUserGetProperty - Context requested: {:08X} XUID: {:16X}",
               context->context_id.get(), user->xuid());

        auto context_value =
            kernel_state_->xam_state()->user_tracker()->GetUserContext(
                user->xuid(), context->context_id);

        if (!context_value) {
          return X_E_INVALIDARG;
        }

        context->value = context_value.value();
        return X_E_SUCCESS;
      }

      if (!xgi_property->property_size_ptr || !xgi_property->property_address) {
        return X_E_INVALIDARG;
      }

      // Process property
      XUSER_PROPERTY* property = memory_->TranslateVirtual<XUSER_PROPERTY*>(
          xgi_property->property_address);

      XELOGD("XGIUserGetProperty - Property requested: {:08X} XUID: {:16X}",
             property->property_id.get(), user->xuid());

      return kernel_state_->xam_state()->user_tracker()->GetProperty(
          user->xuid(),
          memory_->TranslateVirtual<uint32_t*>(xgi_property->property_size_ptr),
          property);
    }
    case 0x000B0071: {
      XELOGD("ContentEnumerate::ResetEnumerator({:08X}, {:08X}), unimplemented",
             buffer_ptr, buffer_length);
      return X_E_SUCCESS;
    }
  }
  XELOGE(
      "Unimplemented XGI message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
