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

struct X_XUSER_ACHIEVEMENT {
  xe::be<uint32_t> user_idx;
  xe::be<uint32_t> achievement_id;
};

struct XUSER_STATS_READ_RESULTS {
  xe::be<uint32_t> NumViews;
  xe::be<uint32_t> pViews;
};

struct XUSER_STATS_VIEW {
  xe::be<uint32_t> ViewId;
  xe::be<uint32_t> TotalViewRows;
  xe::be<uint32_t> NumRows;
  xe::be<uint32_t> pRows;
};

struct XUSER_STATS_ROW {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> Rank;
  xe::be<uint64_t> i64Rating;
  CHAR szGamertag[16];
  xe::be<uint32_t> NumColumns;
  xe::be<uint32_t> pColumns;
};

struct XUSER_STATS_COLUMN {
  xe::be<uint16_t> ColumnId;
  X_USER_DATA Value;
};

struct XUSER_STATS_SPEC {
  xe::be<uint32_t> ViewId;
  xe::be<uint32_t> NumColumnIds;
  xe::be<uint16_t> rgwColumnIds[0x40];
};

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

X_HRESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x000B0018: {
      XSessionModify* data = reinterpret_cast<XSessionModify*>(buffer);

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
      XELOGI("XSessionSearch");
      XSessionSearch* data = reinterpret_cast<XSessionSearch*>(buffer);

      return XSession::GetSessions(memory_, data);
    }

    case 0x000B001C: {
      XELOGI("XSessionSearchEx");
      XSessionSearch* data = reinterpret_cast<XSessionSearch*>(buffer);

      return XSession::GetSessions(memory_, data);
    }

    case 0x000B001D: {
      XSessionDetails* data = reinterpret_cast<XSessionDetails*>(buffer);

      XELOGI("XSessionGetDetails({:08X});", buffer_length);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->GetSessionDetails(data);
    }
    case 0x000B001E: {
      XSessionMigate* data = reinterpret_cast<XSessionMigate*>(buffer);

      XELOGI("XSessionMigrateHost({:08X});", buffer_length);
      if (data->session_info_ptr == NULL) {
        XELOGI("XSessionMigrateHost Failed!");
        return X_E_SUCCESS;
      }

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->MigrateHost(data);
    }
    case 0x000B0021: {
      struct XLeaderboard {
        xe::be<uint32_t> titleId;
        xe::be<uint32_t> xuids_count;
        xe::be<uint32_t> xuids_guest_address;
        xe::be<uint32_t> specs_count;
        xe::be<uint32_t> specs_guest_address;
        xe::be<uint32_t> results_size;
        xe::be<uint32_t> results_guest_address;
      }* data = reinterpret_cast<XLeaderboard*>(buffer);

      if (!data->results_guest_address) return 1;

#pragma region Curl
      Document doc;
      doc.SetObject();

      Value xuidsJsonArray(kArrayType);
      auto xuids = memory_->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuids_guest_address);

      for (unsigned int playerIndex = 0; playerIndex < data->xuids_count;
           playerIndex++) {
        std::string xuid = to_hex_string(xuids[playerIndex]);

        Value value;
        value.SetString(xuid.c_str(), 16, doc.GetAllocator());
        xuidsJsonArray.PushBack(value, doc.GetAllocator());
      }

      doc.AddMember("players", xuidsJsonArray, doc.GetAllocator());

      std::string title_id = fmt::format("{:08x}", kernel_state()->title_id());
      doc.AddMember("titleId", title_id, doc.GetAllocator());

      Value leaderboardQueryJsonArray(kArrayType);
      auto queries = memory_->TranslateVirtual<XUSER_STATS_SPEC*>(
          data->specs_guest_address);

      for (unsigned int queryIndex = 0; queryIndex < data->specs_count;
           queryIndex++) {
        Value queryObject(kObjectType);
        queryObject.AddMember("id", queries[queryIndex].ViewId,
                              doc.GetAllocator());
        Value statIdsArray(kArrayType);
        for (uint32_t statIdIndex = 0;
             statIdIndex < queries[queryIndex].NumColumnIds; statIdIndex++) {
          statIdsArray.PushBack(queries[queryIndex].rgwColumnIds[statIdIndex],
                                doc.GetAllocator());
        }
        queryObject.AddMember("statisticIds", statIdsArray, doc.GetAllocator());
        leaderboardQueryJsonArray.PushBack(queryObject, doc.GetAllocator());
      }

      doc.AddMember("queries", leaderboardQueryJsonArray, doc.GetAllocator());

      rapidjson::StringBuffer buffer;
      PrettyWriter<rapidjson::StringBuffer> writer(buffer);
      doc.Accept(writer);

      XLiveAPI::memory chunk =
          XLiveAPI::LeaderboardsFind((uint8_t*)buffer.GetString());

      if (chunk.response == nullptr) {
        return X_E_SUCCESS;
      }

      Document leaderboards;
      leaderboards.Parse(chunk.response);
      const Value& leaderboardsArray = leaderboards.GetArray();

      // Fixed FM4 and RDR GOTY from crashing.
      if (leaderboardsArray.Empty()) {
        return X_ERROR_IO_PENDING;
      }

      auto leaderboards_guest_address = memory_->SystemHeapAlloc(
          sizeof(XUSER_STATS_VIEW) * leaderboardsArray.Size());
      auto leaderboard = memory_->TranslateVirtual<XUSER_STATS_VIEW*>(
          leaderboards_guest_address);
      auto resultsHeader = memory_->TranslateVirtual<XUSER_STATS_READ_RESULTS*>(
          data->results_guest_address);
      resultsHeader->NumViews = leaderboardsArray.Size();
      resultsHeader->pViews = leaderboards_guest_address;

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
            sizeof(XUSER_STATS_ROW) * playersArray.Size());
        auto player =
            memory_->TranslateVirtual<XUSER_STATS_ROW*>(players_guest_address);
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
              sizeof(XUSER_STATS_COLUMN) * statisticsArray.Size());
          auto stat = memory_->TranslateVirtual<XUSER_STATS_COLUMN*>(
              stats_guest_address);
          player[playerIndex].pColumns = stats_guest_address;

          uint32_t statIndex = 0;
          for (Value::ConstValueIterator statObjectPtr =
                   statisticsArray.Begin();
               statObjectPtr != statisticsArray.End(); ++statObjectPtr) {
            stat[statIndex].ColumnId = (*statObjectPtr)["id"].GetUint();

            stat[statIndex].Value.type = static_cast<X_USER_DATA_TYPE>(
                (*statObjectPtr)["type"].GetUint());

            switch (stat[statIndex].Value.type) {
              case X_USER_DATA_TYPE::INT32:
                stat[statIndex].Value.s32 = (*statObjectPtr)["value"].GetUint();
                break;
              case X_USER_DATA_TYPE::INT64:
                stat[statIndex].Value.s64 =
                    (*statObjectPtr)["value"].GetUint64();
                break;
              default:
                XELOGW("Unimplemented stat type for read, will attempt anyway.",
                       static_cast<uint32_t>(stat[statIndex].Value.type));
                if ((*statObjectPtr)["value"].IsNumber())
                  stat[statIndex].Value.s64 =
                      (*statObjectPtr)["value"].GetUint64();
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
      XSessionArbitrationData* data =
          reinterpret_cast<XSessionArbitrationData*>(buffer);

      XELOGI(
          "XSessionArbitrationRegister({:08X}, {:08X}, {:08X}, {:08X}, {:08X}, "
          "{:08X}, {:08X}, {:08X});",
          data->obj_ptr.get(), data->flags.get(), data->unk1.get(),
          data->unk2.get(), data->session_nonce.get(),
          data->results_buffer_size.get(), data->results.get(),
          data->pXOverlapped.get());

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->RegisterArbitration(data);
    }
    case 0x000B0006: {
      assert_true(!buffer_length || buffer_length == 24);

      // dword r3 user index
      // dword (unwritten?)
      // qword 0
      // dword r4 context enum
      // dword r5 value
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t context_value = xe::load_and_swap<uint32_t>(buffer + 20);
      XELOGD("XGIUserSetContextEx({:08X}, {:08X}, {:08X})", user_index,
             context_id, context_value);

      const util::XdbfGameData title_xdbf = kernel_state_->title_xdbf();
      if (title_xdbf.is_valid()) {
        const auto context = title_xdbf.GetContext(context_id);
        const XLanguage title_language = title_xdbf.GetExistingLanguage(
            static_cast<XLanguage>(XLanguage::kEnglish));
        const std::string desc =
            title_xdbf.GetStringTableEntry(title_language, context.string_id);
        XELOGD("XGIUserSetContextEx: {} - Set to value: {}", desc,
               context_value);

        UserProfile* user_profile =
            kernel_state_->xam_state()->GetUserProfile(user_index);
        if (user_profile) {
          user_profile->contexts_[context_id] = context_value;
        }
      }
      return X_E_SUCCESS;
    }
    case 0x000B0007: {
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t property_id = xe::load_and_swap<uint32_t>(buffer + 16);
      uint32_t value_size = xe::load_and_swap<uint32_t>(buffer + 20);
      uint32_t value_ptr = xe::load_and_swap<uint32_t>(buffer + 24);
      XELOGD("XGIUserSetPropertyEx({:08X}, {:08X}, {}, {:08X})", user_index,
             property_id, value_size, value_ptr);

      const util::XdbfGameData title_xdbf = kernel_state_->title_xdbf();
      if (title_xdbf.is_valid()) {
        const auto property_xdbf = title_xdbf.GetProperty(property_id);
        const XLanguage title_language = title_xdbf.GetExistingLanguage(
            static_cast<XLanguage>(XLanguage::kEnglish));
        const std::string desc = title_xdbf.GetStringTableEntry(
            title_language, property_xdbf.string_id);

        Property property =
            Property(property_id, value_size,
                     memory_->TranslateVirtual<uint8_t*>(value_ptr));

        auto user = kernel_state_->xam_state()->GetUserProfile(user_index);
        if (user) {
          user->AddProperty(&property);
        }
        XELOGD("XGIUserSetPropertyEx: Setting property: {}", desc);
      }

      return X_E_SUCCESS;
    }
    case 0x000B0008: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t achievement_count = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t achievements_ptr = xe::load_and_swap<uint32_t>(buffer + 4);
      XELOGD("XGIUserWriteAchievements({:08X}, {:08X})", achievement_count,
             achievements_ptr);

      auto* achievement =
          (X_XUSER_ACHIEVEMENT*)memory_->TranslateVirtual(achievements_ptr);
      for (uint32_t i = 0; i < achievement_count; i++, achievement++) {
        kernel_state_->achievement_manager()->EarnAchievement(
            achievement->user_idx, kernel_state_->title_id(),
            achievement->achievement_id);
      }
      return X_E_SUCCESS;
    }
    case 0x000B0010: {
      XELOGI("XSessionCreate");

      assert_true(!buffer_length || buffer_length == 28);
      // Sequence:
      // - XamSessionCreateHandle
      // - XamSessionRefObjByHandle
      // - [this]
      // - CloseHandle

      XSessionData* data = reinterpret_cast<XSessionData*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_ERROR_INVALID_PARAMETER;
      }

      const auto result = session->CreateSession(
          data->user_index, data->num_slots_public, data->num_slots_private,
          data->flags, data->session_info_ptr, data->nonce_ptr);

      XLiveAPI::clearXnaddrCache();
      return result;
    }
    case 0x000B0011: {
      XELOGI("XGISessionDelete");

      struct SessionDelete {
        xe::be<uint32_t> obj_ptr;
      }* data = reinterpret_cast<SessionDelete*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);

      if (!session) {
        return X_ERROR_INVALID_PARAMETER;
      }

      return session->DeleteSession();
    }
    case 0x000B0012: {
      assert_true(buffer_length == 0x14);

      XSessionJoin* data = reinterpret_cast<XSessionJoin*>(buffer);
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
      assert_true(buffer_length == 0x14);

      const auto data = reinterpret_cast<XSessionLeave*>(buffer);

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

      XELOGI("XSessionStart");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0015: {
      // send high scores?

      XELOGI("XSessionEnd");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0025: {
      XELOGI("XSessionWriteStats");

      XSessionWriteStats* data = reinterpret_cast<XSessionWriteStats*>(buffer);

      uint8_t* obj_ptr = memory_->TranslateVirtual<uint8_t*>(data->obj_ptr);

      auto session =
          XObject::GetNativeObject<XSession>(kernel_state(), obj_ptr);
      if (!session) {
        return X_STATUS_INVALID_HANDLE;
      }

      return session->WriteStats(data);
    }
    case 0x000B001B: {
      XSessionSearchID* data = reinterpret_cast<XSessionSearchID*>(buffer);

      return XSession::GetSessionByID(memory_, data);
    }
    case 0x000B0065: {
      XELOGI("XSessionSearchWeighted unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0026: {
      XELOGI("XSessionFlushStats unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B001F: {
      XELOGI("XSessionModifySkill unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0019: {
      XELOGI("XSessionGetInvitationData unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B0036: {
      // Called after opening xbox live arcade and clicking on xbox live v5759
      // to 5787 and called after clicking xbox live in the game library from
      // v6683 to v6717
      XELOGD("XGIUnkB0036, unimplemented");
      return X_E_FAIL;
    }
    case 0x000B0041: {
      assert_true(!buffer_length || buffer_length == 32);
      // 00000000 2789fecc 00000000 00000000 200491e0 00000000 200491f0 20049340
      uint32_t user_index = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t context_ptr = xe::load_and_swap<uint32_t>(buffer + 16);
      auto context =
          context_ptr ? memory_->TranslateVirtual(context_ptr) : nullptr;
      uint32_t context_id =
          context ? xe::load_and_swap<uint32_t>(context + 0) : 0;
      XELOGD("XGIUserGetContext({:08X}, {:08X}{:08X}))", user_index,
             context_ptr, context_id);
      uint32_t value = 0;
      if (context) {
        UserProfile* user_profile =
            kernel_state_->xam_state()->GetUserProfile(user_index);
        if (user_profile) {
          if (user_profile->contexts_.find(context_id) !=
              user_profile->contexts_.cend()) {
            value = user_profile->contexts_[context_id];
          }
        }
        xe::store_and_swap<uint32_t>(context + 4, value);
      }
      return X_E_FAIL;
    }
    case 0x000B0071: {
      XELOGD("XGI 0x000B0071, unimplemented");
      return X_E_SUCCESS;
    }
    case 0x000B003D: {
      // Games used in:
      // - 5451082a (netplay build).
      XELOGD("XGIUnkB003D, unimplemented");
      return X_E_FAIL;
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