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

struct XGI_XUSER_ANID {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> AnId_buffer_size;
  xe::be<uint32_t> AnId_buffer_ptr;  // char*
  xe::be<uint32_t> block;            // 1
};
static_assert_size(XGI_XUSER_ANID, 0x10);

struct XGI_XUSER_READ_STATS {
  xe::be<uint32_t> titleId;
  xe::be<uint32_t> xuids_count;
  xe::be<uint32_t> xuids_ptr;
  xe::be<uint32_t> specs_count;
  xe::be<uint32_t> specs_ptr;
  xe::be<uint32_t> results_size;
  xe::be<uint32_t> results_ptr;
};
static_assert_size(XGI_XUSER_READ_STATS, 0x1C);

struct XGI_XUSER_STATS_RESET {
  xe::be<uint32_t> user_index;
  xe::be<uint32_t> view_id;
};
static_assert_size(XGI_XUSER_STATS_RESET, 0x8);

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

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

      return XSession::GetSessions(memory_, data, num_users);
    }
    case 0x000B001C: {
      assert_true(!buffer_length ||
                  buffer_length == sizeof(XGI_SESSION_SEARCH_EX));
      XELOGI("XSessionSearchEx");

      XGI_SESSION_SEARCH_EX* data =
          reinterpret_cast<XGI_SESSION_SEARCH_EX*>(buffer);

      return XSession::GetSessions(memory_, &data->session_search,
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
        return 1;
      }

#pragma region Curl
      Document doc;
      doc.SetObject();

      Value xuidsJsonArray(kArrayType);
      auto xuids =
          memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuids_ptr);

      for (uint32_t player_index = 0; player_index < data->xuids_count;
           player_index++) {
        const xe::be<uint64_t> xuid = xuids[player_index];

        assert_true(IsValidXUID(xuid));

        if (xuid) {
          std::string xuid_str = string_util::to_hex_string(xuid);

          Value value;
          value.SetString(xuid_str.c_str(), 16, doc.GetAllocator());
          xuidsJsonArray.PushBack(value, doc.GetAllocator());
        }
      }

      if (xuidsJsonArray.Empty()) {
        return X_E_SUCCESS;
      }

      doc.AddMember("players", xuidsJsonArray, doc.GetAllocator());

      std::string title_id = fmt::format("{:08x}", kernel_state()->title_id());
      doc.AddMember("titleId", title_id, doc.GetAllocator());

      Value leaderboardQueryJsonArray(kArrayType);
      auto queries =
          memory_->TranslateVirtual<X_USER_STATS_SPEC*>(data->specs_ptr);

      for (unsigned int queryIndex = 0; queryIndex < data->specs_count;
           queryIndex++) {
        Value queryObject(kObjectType);
        queryObject.AddMember("id", queries[queryIndex].view_id,
                              doc.GetAllocator());

        assert_false(queries[queryIndex].num_column_ids >
                     kXUserMaxStatsAttributes);

        const uint32_t num_column_ids = std::min<uint32_t>(
            queries[queryIndex].num_column_ids, kXUserMaxStatsAttributes);

        Value statIdsArray(kArrayType);
        for (uint32_t stat_id_index = 0; stat_id_index < num_column_ids;
             stat_id_index++) {
          statIdsArray.PushBack(queries[queryIndex].column_Ids[stat_id_index],
                                doc.GetAllocator());
        }
        queryObject.AddMember("statisticIds", statIdsArray, doc.GetAllocator());
        leaderboardQueryJsonArray.PushBack(queryObject, doc.GetAllocator());
      }

      doc.AddMember("queries", leaderboardQueryJsonArray, doc.GetAllocator());

      rapidjson::StringBuffer buffer;
      PrettyWriter<rapidjson::StringBuffer> writer(buffer);
      doc.Accept(writer);

      std::unique_ptr<HTTPResponseObjectJSON> chunk =
          XLiveAPI::LeaderboardsFind((uint8_t*)buffer.GetString());

      if (chunk->RawResponse().response == nullptr ||
          chunk->StatusCode() != HTTP_STATUS_CODE::HTTP_CREATED) {
        return X_ERROR_FUNCTION_FAILED;
      }

      Document leaderboards;
      leaderboards.Parse(chunk->RawResponse().response);
      const Value& leaderboardsArray = leaderboards.GetArray();

      // Fixed FM4 and RDR GOTY from crashing.
      if (leaderboardsArray.Empty()) {
        return X_ERROR_IO_PENDING;
      }

      auto leaderboards_guest_address = memory_->SystemHeapAlloc(
          sizeof(X_USER_STATS_VIEW) * leaderboardsArray.Size());
      auto leaderboard = memory_->TranslateVirtual<X_USER_STATS_VIEW*>(
          leaderboards_guest_address);
      auto resultsHeader =
          memory_->TranslateVirtual<X_USER_STATS_READ_RESULTS*>(
              data->results_ptr);
      resultsHeader->NumViews = leaderboardsArray.Size();
      resultsHeader->Views_ptr = leaderboards_guest_address;

      uint32_t leaderboardIndex = 0;
      for (Value::ConstValueIterator leaderboardObjectPtr =
               leaderboardsArray.Begin();
           leaderboardObjectPtr != leaderboardsArray.End();
           ++leaderboardObjectPtr) {
        leaderboard[leaderboardIndex].ViewId =
            (*leaderboardObjectPtr)["id"].GetUint();
        auto playersArray = (*leaderboardObjectPtr)["players"].GetArray();
        leaderboard[leaderboardIndex].NumRows = playersArray.Size();
        leaderboard[leaderboardIndex].TotalViewRows = playersArray.Size();
        auto players_guest_address = memory_->SystemHeapAlloc(
            sizeof(X_USER_STATS_ROW) * playersArray.Size());
        auto player =
            memory_->TranslateVirtual<X_USER_STATS_ROW*>(players_guest_address);
        leaderboard[leaderboardIndex].pRows = players_guest_address;

        uint32_t playerIndex = 0;
        for (Value::ConstValueIterator playerObjectPtr = playersArray.Begin();
             playerObjectPtr != playersArray.End(); ++playerObjectPtr) {
          player[playerIndex].Rank = 1;
          player[playerIndex].i64Rating = 1;
          auto gamertag = (*playerObjectPtr)["gamertag"].GetString();
          auto gamertagLength =
              (*playerObjectPtr)["gamertag"].GetStringLength();
          memcpy(player[playerIndex].szGamertag, gamertag, gamertagLength);

          std::vector<uint8_t> xuid;
          string_util::hex_string_to_array(
              xuid, (*playerObjectPtr)["xuid"].GetString());
          memcpy(&player[playerIndex].xuid, xuid.data(), 8);

          auto statisticsArray = (*playerObjectPtr)["stats"].GetArray();
          player[playerIndex].NumColumns = statisticsArray.Size();
          auto stats_guest_address = memory_->SystemHeapAlloc(
              sizeof(X_USER_STATS_COLUMN) * statisticsArray.Size());
          auto stat = memory_->TranslateVirtual<X_USER_STATS_COLUMN*>(
              stats_guest_address);
          player[playerIndex].pColumns = stats_guest_address;

          uint32_t statIndex = 0;
          for (Value::ConstValueIterator statObjectPtr =
                   statisticsArray.Begin();
               statObjectPtr != statisticsArray.End(); ++statObjectPtr) {
            stat[statIndex].ColumnId = (*statObjectPtr)["id"].GetUint();
            stat[statIndex].Value.type = static_cast<X_USER_DATA_TYPE>(
                (*statObjectPtr)["type"].GetUint());

            X_USER_DATA_TYPE stat_type = stat[statIndex].Value.type;

            switch (stat_type) {
              case X_USER_DATA_TYPE::CONTEXT: {
                XELOGW("Statistic type: CONTEXT");
              } break;
              case X_USER_DATA_TYPE::INT32: {
                XELOGW("Statistic type: INT32");
              } break;
              case X_USER_DATA_TYPE::INT64: {
                XELOGW("Statistic type: INT64");
              } break;
              case X_USER_DATA_TYPE::DOUBLE: {
                XELOGW("Statistic type: DOUBLE");
              } break;
              case X_USER_DATA_TYPE::WSTRING: {
                XELOGW("Statistic type: WSTRING");
              } break;
              case X_USER_DATA_TYPE::FLOAT: {
                XELOGW("Statistic type: FLOAT");
              } break;
              case X_USER_DATA_TYPE::BINARY: {
                XELOGW("Statistic type: BINARY");
              } break;
              case X_USER_DATA_TYPE::DATETIME: {
                XELOGW("Statistic type: DATETIME");
              } break;
              case X_USER_DATA_TYPE::UNSET: {
                XELOGW("Statistic type: UNSET");
              } break;
              default:
                XELOGW("Unsupported statistic type.",
                       static_cast<uint32_t>(stat_type));
                break;
            }

            switch (stat_type) {
              case X_USER_DATA_TYPE::INT32:
                stat[statIndex].Value.data.u32 =
                    (*statObjectPtr)["value"].GetUint();
                break;
              case X_USER_DATA_TYPE::INT64:
                stat[statIndex].Value.data.s64 =
                    (*statObjectPtr)["value"].GetUint64();
                break;
              default:
                XELOGW("Unimplemented stat type for read, will attempt anyway.",
                       static_cast<uint32_t>(stat[statIndex].Value.type));
                if ((*statObjectPtr)["value"].IsNumber()) {
                  stat[statIndex].Value.data.s64 =
                      (*statObjectPtr)["value"].GetUint64();
                }
            }

            stat[statIndex].Value.type = static_cast<X_USER_DATA_TYPE>(
                (*statObjectPtr)["type"].GetUint());

            statIndex++;
          }

          playerIndex++;
        }

        leaderboardIndex++;
      }
#pragma endregion
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

        if (xgi_context->context.context_id == X_CONTEXT_PRESENCE) {
          auto presence = user->GetPresenceString();
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
        Property property(
            xgi_property->property_id,
            Property::get_valid_data_size(xgi_property->property_id,
                                          xgi_property->data_size),
            memory_->TranslateVirtual<uint8_t*>(xgi_property->data_address));

        kernel_state_->xam_state()->user_tracker()->AddProperty(user->xuid(),
                                                                &property);
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

      return session->DeleteSession(data);
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

      return XSession::GetWeightedSessions(memory_, data, num_users);
    }
    case 0x000B0026: {
      assert_true(!buffer_length || buffer_length == sizeof(XGI_STATS_WRITE));

      XGI_STATS_WRITE* data = reinterpret_cast<XGI_STATS_WRITE*>(buffer);

      XELOGI("XSessionFlushStats({:08X}, {:016X}, {:08X}, {:08X})",
             data->obj_ptr.get(), data->xuid.get(), data->num_views.get(),
             data->views_ptr.get());

      return X_E_SUCCESS;
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
      XELOGD("XGIUnkB0036({:08X}, {:08X}), unimplemented", buffer_ptr,
             buffer_length);
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
        AnIdBuffer[i] = i % 10;
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
      XELOGD("XGIUnkB0071({:08X}, {:08X}), unimplemented", buffer_ptr,
             buffer_length);
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