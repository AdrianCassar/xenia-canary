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
#include "xenia/base/threading.h"

#ifdef XE_PLATFORM_WIN32
// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#include <winsock2.h>                    // NOLINT(build/include_order)
#endif

#include <random>

// Get title id
#include <src/xenia/kernel/kernel_state.cc>

#include <xenia/kernel/XLiveAPI.h>

using namespace rapidjson;
using namespace xe::string_util;

DECLARE_bool(logging);

DECLARE_bool(upnp);

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

struct XSESSION_REGISTRATION_RESULTS {
  xe::be<uint32_t> registrants_count;
  xe::be<uint32_t> registrants_ptr;
};

struct XSESSION_REGISTRANT {
  xe::be<uint64_t> MachineID;
  xe::be<uint32_t> bTrustworthiness;
  xe::be<uint32_t> bNumUsers;
  xe::be<uint32_t> rgUsers;
};

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

struct XUSER_CONTEXT {
  xe::be<uint32_t> context_id;
  xe::be<uint32_t> value;
};

struct XSESSION_SEARCHRESULT {
  XLiveAPI::XSESSION_INFO info;
  xe::be<uint32_t> open_public_slots;
  xe::be<uint32_t> open_priv_slots;
  xe::be<uint32_t> filled_public_slots;
  xe::be<uint32_t> filled_priv_slots;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> contexts_count;
  xe::be<uint32_t> properties_ptr;
  xe::be<uint32_t> contexts_ptr;
};

struct XSESSION_SEARCHRESULT_HEADER {
  xe::be<uint32_t> search_results_count;
  xe::be<uint32_t> search_results_ptr;
};

enum SessionFlags {
  HOST = 0x01,
  PRESENCE = 0x02,
  STATS = 0x04,
  MATCHMAKING = 0x08,
  ARBITRATION = 0x10,
  PEER_NETWORK = 0x20,
  SOCIAL_MATCHMAKING_ALLOWED = 0x80,
  INVITES_DISABLED = 0x0100,
  JOIN_VIA_PRESENCE_DISABLED = 0x0200,
  JOIN_IN_PROGRESS_DISABLED = 0x0400,
  JOIN_VIA_PRESENCE_FRIENDS_ONLY = 0x0800
};

const uint32_t SINGLEPLAYER_WITH_STATS = PRESENCE | STATS | INVITES_DISABLED |
                                         JOIN_VIA_PRESENCE_DISABLED |
                                         JOIN_IN_PROGRESS_DISABLED;

const uint32_t LIVE_MULTIPLAYER_STANDARD =
    PRESENCE | STATS | MATCHMAKING | PEER_NETWORK;

const uint32_t LIVE_MULTIPLAYER_RANKED =
    LIVE_MULTIPLAYER_STANDARD | ARBITRATION;

const uint32_t SYSTEMLINK = PEER_NETWORK;

const uint32_t GROUP_LOBBY = PRESENCE | PEER_NETWORK;

const uint32_t GROUP_GAME = STATS | MATCHMAKING | PEER_NETWORK;

enum XSESSION_STATE : uint32_t {
  LOBBY,
  REGISTRATION,
  INGAME,
  REPORTING,
  DELETED,
};

struct XSESSION_LOCAL_DETAILS {
  xe::be<uint32_t> UserIndexHost;
  xe::be<uint32_t> GameType;
  xe::be<uint32_t> GameMode;
  xe::be<uint32_t> Flags;
  xe::be<uint32_t> MaxPublicSlots;
  xe::be<uint32_t> MaxPrivateSlots;
  xe::be<uint32_t> AvailablePublicSlots;
  xe::be<uint32_t> AvailablePrivateSlots;
  xe::be<uint32_t> ActualMemberCount;
  xe::be<uint32_t> ReturnedMemberCount;
  // xe::be<uint32_t> eState;
  XSESSION_STATE eState;
  xe::be<uint64_t> Nonce;
  XLiveAPI::XSESSION_INFO sessionInfo;
  XLiveAPI::XNKID xnkidArbitration;
  xe::be<uint32_t> pSessionMembers;
};

struct XSESSION_MEMBER {
  xe::be<uint64_t> xuidOnline;
  xe::be<uint32_t> UserIndex;
  xe::be<uint32_t> Flags;
};

// use hex_string_to_array() instead
// TODO: Move - Codie
bool StringToHex(const std::string& inStr, unsigned char* outStr) {
  size_t len = inStr.length();
  for (size_t i = 0; i < len; i += 2) {
    sscanf(inStr.c_str() + i, "%2hhx", outStr);
    ++outStr;
  }
  return true;
}

xe::be<uint64_t> XNKIDtoUint64(XLiveAPI::XNKID* sessionID) {
  int i;
  xe::be<uint64_t> sessionId64 = 0;
  for (i = 7; i >= 0; --i) {
    sessionId64 = sessionId64 << 8;
    sessionId64 |= (uint64_t)sessionID->ab[7 - i];
  }

  return sessionId64;
}

void Uint64toXNKID(xe::be<uint64_t> sessionID, XLiveAPI::XNKID* xnkid) {
  for (int i = 0; i < 8; i++) {
    xnkid->ab[i] = ((sessionID >> (8 * i)) & 0XFF);
  }
}

xe::be<uint64_t> UCharArrayToUint64(unsigned char* data) {
  int i;
  xe::be<uint64_t> out = 0;
  for (i = 7; i >= 0; --i) {
    out = out << 8;
    out |= (uint64_t)data[7 - i];
  }

  return out;
}

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
  XLiveAPI::XUSER_DATA Value;
};

struct XUSER_STATS_SPEC {
  xe::be<uint32_t> ViewId;
  xe::be<uint32_t> NumColumnIds;
  xe::be<uint16_t> rgwColumnIds[0x40];
};

X_HRESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  switch (message) {
    case 0x000B0018: {
      XLiveAPI::XSessionModify* data =
          reinterpret_cast<XLiveAPI::XSessionModify*>(buffer);

      XELOGI("XSessionModify({:08X} {:08X} {:08X} {:08X})",
             data->session_handle.get(), data->flags.get(),
             data->maxPublicSlots.get(), data->maxPrivateSlots.get());

      if (data->session_handle == NULL) {
        assert_always();
        return X_E_SUCCESS;
      }

      XLiveAPI::SessionModify(XLiveAPI::sessionHandleMap[data->session_handle],
                              data);

      return X_E_SUCCESS;
    }
    case 0x000B0016: {
      XELOGI("XSessionSearch");

      XLiveAPI::XSessionSearch* data =
          reinterpret_cast<XLiveAPI::XSessionSearch*>(buffer);

      auto* pSearchContexts =
          memory_->TranslateVirtual<XUSER_CONTEXT*>(data->ctx_ptr);

      uint32_t results_ptr =
          data->search_results + sizeof(XSESSION_SEARCHRESULT_HEADER);

      auto* result =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(results_ptr);

      auto resultsHeader =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
              data->search_results);

      if (!(data->results_buffer)) {
        assert_false(!(data->results_buffer));

        data->results_buffer =
            sizeof(XSESSION_SEARCHRESULT) * data->num_results;

        // return X_E_SUCCESS;
        return ERROR_INSUFFICIENT_BUFFER;
      }

      // return a list of session for the title
      const std::vector<XLiveAPI::SessionJSON> sessions =
          XLiveAPI::SessionSearch(data);

      uint32_t i = 0;
      for (const auto& session : sessions) {
        uint32_t result_guest_address = data->search_results +
                                        sizeof(XSESSION_SEARCHRESULT_HEADER) +
                                        (sizeof(XSESSION_SEARCHRESULT) * i);

        auto* resultHostPtr = memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(
            result_guest_address);

        // If we have looped through all sessions then exit.
        if (data->num_results <= i) break;

        result[i].contexts_count = (uint32_t)data->num_ctx;
        result[i].properties_count = 3;
        result[i].contexts_ptr = data->ctx_ptr;
        result[i].properties_ptr = data->props_ptr;

        result[i].filled_priv_slots = session.filledPrivateSlotsCount;
        result[i].filled_public_slots = session.filledPublicSlotsCount;
        result[i].open_priv_slots = session.openPrivateSlotsCount;
        result[i].open_public_slots = session.openPublicSlotsCount;

        memcpy(&result[i].info.sessionID, session.sessionid.c_str(), 8);
        memcpy(&result[i].info.hostAddress.abEnet, session.macAddress.c_str(),
               6);
        memcpy(&result[i].info.hostAddress.abOnline, session.macAddress.c_str(),
               6);

        for (int j = 0; j < 16; j++) {
          result[i].info.keyExchangeKey.ab[j] = j;
        }

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.ina.s_addr);

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.inaOnline.s_addr);

        resultHostPtr[i].info.hostAddress.wPortOnline = session.port;

        i += 1;

        resultsHeader->search_results_count = i;
        resultsHeader->search_results_ptr =
            data->search_results + sizeof(XSESSION_SEARCHRESULT_HEADER);
      }

      return X_E_SUCCESS;
    }
    case 0x000B001C: {
      XELOGI("XSessionSearchEx");

      XLiveAPI::XSessionSearchEx* data =
          reinterpret_cast<XLiveAPI::XSessionSearchEx*>(buffer);

      auto* pSearchContexts =
          memory_->TranslateVirtual<XUSER_CONTEXT*>(data->ctx_ptr);

      uint32_t results_ptr =
          data->search_results + sizeof(XSESSION_SEARCHRESULT_HEADER);

      auto* result =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(results_ptr);

      auto resultsHeader =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
              data->search_results);

      if (!(data->results_buffer)) {
        assert_false(!(data->results_buffer));

        data->results_buffer =
            sizeof(XSESSION_SEARCHRESULT) * data->num_results;

        // return X_E_SUCCESS;
        return ERROR_INSUFFICIENT_BUFFER;
      }

      // return a list of session for the title
      const std::vector<XLiveAPI::SessionJSON> sessions =
          XLiveAPI::SessionSearchEx(data);

      uint32_t i = 0;
      for (const auto& session : sessions) {
        uint32_t result_guest_address = data->search_results +
                                        sizeof(XSESSION_SEARCHRESULT_HEADER) +
                                        (sizeof(XSESSION_SEARCHRESULT) * i);

        auto* resultHostPtr = memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(
            result_guest_address);

        // If we have looped through all sessions then exit.
        if (data->num_results <= i) break;

        result[i].contexts_count = (uint32_t)data->num_ctx;
        result[i].properties_count = 3;
        result[i].contexts_ptr = data->ctx_ptr;
        result[i].properties_ptr = data->props_ptr;

        result[i].filled_priv_slots = session.filledPrivateSlotsCount;
        result[i].filled_public_slots = session.filledPublicSlotsCount;
        result[i].open_priv_slots = session.openPrivateSlotsCount;
        result[i].open_public_slots = session.openPublicSlotsCount;

        memcpy(&result[i].info.sessionID, session.sessionid.c_str(), 8);
        memcpy(&result[i].info.hostAddress.abEnet, session.macAddress.c_str(),
               6);
        memcpy(&result[i].info.hostAddress.abOnline, session.macAddress.c_str(),
               6);

        for (int j = 0; j < 16; j++) {
          result[i].info.keyExchangeKey.ab[j] = j;
        }

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.ina.s_addr);

        inet_pton(AF_INET, session.hostAddress.c_str(),
                  &resultHostPtr[i].info.hostAddress.inaOnline.s_addr);

        resultHostPtr[i].info.hostAddress.wPortOnline = session.port;

        i += 1;

        resultsHeader->search_results_count = i;
        resultsHeader->search_results_ptr =
            data->search_results + sizeof(XSESSION_SEARCHRESULT_HEADER);
      }

      return X_E_SUCCESS;
    }
    case 0x000B001D: {
      XLiveAPI::XSessionDetails* data =
          reinterpret_cast<XLiveAPI::XSessionDetails*>(buffer);

      XELOGI("XSessionGetDetails({:08X});", buffer_length);

      auto details = memory_->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->details_buffer);

      XLiveAPI::SessionJSON session = XLiveAPI::SessionDetails(
          XLiveAPI::sessionHandleMap[data->session_handle]);

      if (session.hostAddress.empty()) {
        return 1;
      }

      memcpy(&details->sessionInfo.sessionID, session.sessionid.c_str(), 8);

      details->sessionInfo.hostAddress.inaOnline.s_addr =
          inet_addr(session.hostAddress.c_str());
      details->sessionInfo.hostAddress.ina.s_addr =
          details->sessionInfo.hostAddress.inaOnline.s_addr;

      memcpy(&details->sessionInfo.hostAddress.abEnet,
             session.hostAddress.c_str(), 6);
      details->sessionInfo.hostAddress.wPortOnline = session.port;

      details->UserIndexHost = 0;
      details->GameMode = 0;
      details->GameType = 0;
      // details->eState = session.state;

      details->Flags = session.flags;
      details->MaxPublicSlots = session.publicSlotsCount;
      details->MaxPrivateSlots = session.privateSlotsCount;
      details->AvailablePrivateSlots = session.openPublicSlotsCount;
      details->AvailablePublicSlots = session.openPrivateSlotsCount;
      details->ActualMemberCount =
          session.filledPublicSlotsCount + session.filledPrivateSlotsCount;
      details->ReturnedMemberCount = (uint32_t)session.players.size();

      details->Nonce = 0xAAAAAAAAAAAAAAAA;

      for (int i = 0; i < 16; i++) {
        details->sessionInfo.keyExchangeKey.ab[i] = i;
      }

      for (int i = 0; i < 20; i++) {
        details->sessionInfo.hostAddress.abOnline[i] = i;
      }

      uint32_t members_ptr = memory_->SystemHeapAlloc(
          sizeof(XSESSION_MEMBER) * details->ReturnedMemberCount);

      auto members = memory_->TranslateVirtual<XSESSION_MEMBER*>(members_ptr);
      details->pSessionMembers = members_ptr;

      unsigned int i = 0;

      for (const auto& player : session.players) {
        members[i].UserIndex = 0xFE;

        int size = sizeof(player.xuid);
        memcpy(&members[i].xuidOnline, player.xuid.c_str(), size);

        i += 1;
      }

      return X_E_SUCCESS;
    }
    case 0x000B001E: {
      XLiveAPI::XSessionMigate* data =
          reinterpret_cast<XLiveAPI::XSessionMigate*>(buffer);

      XELOGI("XSessionMigrateHost({:08X});", buffer_length);

      if (data->session_info_ptr == NULL) {
        return X_E_SUCCESS;
      }

      auto sessionInfo = memory_->TranslateVirtual<XLiveAPI::XSESSION_INFO*>(
          data->session_info_ptr);

      XLiveAPI::SessionJSON result = XLiveAPI::XSessionMigration(
          XLiveAPI::sessionHandleMap[data->session_handle]);

      // FIX ME:
      if (result.hostAddress == "") {
        return X_E_SUCCESS;
      }

      for (int i = 0; i < 16; i++) {
        sessionInfo->keyExchangeKey.ab[i] = i;
      }

      sessionInfo->hostAddress.inaOnline.s_addr =
          XLiveAPI::OnlineIP().sin_addr.s_addr;

      sessionInfo->hostAddress.ina.s_addr =
          sessionInfo->hostAddress.inaOnline.s_addr;

      memcpy(&sessionInfo->hostAddress.abEnet, XLiveAPI::mac_address, 6);
      memcpy(&sessionInfo->hostAddress.abOnline, XLiveAPI::mac_address, 6);

      sessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();

      if (&sessionInfo->sessionID) {
        XLiveAPI::sessionHandleMap.emplace(
            data->session_handle, XNKIDtoUint64(&sessionInfo->sessionID));
      }

      return X_E_SUCCESS;
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

      XLiveAPI::memory chunk = XLiveAPI::LeaderboardsFind(buffer.GetString());

      if (chunk.response == nullptr) {
        return X_E_SUCCESS;
      }

      Document leaderboards;
      leaderboards.Parse(chunk.response);
      const Value& leaderboardsArray = leaderboards.GetArray();

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
            (*leaderboardObjectPtr)["id"].GetInt();
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
          unsigned char xuid[8];
          StringToHex((*playerObjectPtr)["xuid"].GetString(), xuid);
          player[playerIndex].xuid = UCharArrayToUint64(xuid);

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
            stat[statIndex].Value.type = (*statObjectPtr)["type"].GetUint();

            switch (stat[statIndex].Value.type) {
              case 1:
                stat[statIndex].Value.dword_data =
                    (*statObjectPtr)["value"].GetUint();
                break;
              case 2:
                stat[statIndex].Value.qword_data =
                    (*statObjectPtr)["value"].GetUint64();
                break;
              default:
                XELOGW("Unimplemented stat type for read, will attempt anyway.",
                       stat[statIndex].Value.type);
                if ((*statObjectPtr)["value"].IsNumber())
                  stat[statIndex].Value.qword_data =
                      (*statObjectPtr)["value"].GetUint64();
            }

            stat[statIndex].Value.type = (*statObjectPtr)["type"].GetInt();
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
      XLiveAPI::XSessionArbitrationData* data =
          reinterpret_cast<XLiveAPI::XSessionArbitrationData*>(buffer);

      XELOGI(
          "XSessionArbitrationRegister({:08X}, {:08X}, {:08X}, {:08X}, {:08X}, "
          "{:08X}, {:08X}, {:08X});",
          data->session_handle.get(), data->flags.get(), data->unk1.get(),
          data->unk2.get(), data->session_nonce.get(),
          data->results_buffer_size.get(), data->results.get(),
          data->pXOverlapped.get());

      auto results = memory_->TranslateVirtual<XSESSION_REGISTRATION_RESULTS*>(
          data->results);

      // TODO: Remove hardcoded results, populate properly.

      char* result = XLiveAPI::XSessionArbitration(
          XLiveAPI::sessionHandleMap[data->session_handle]);

#pragma region Curl
      rapidjson::Document doc;
      doc.Parse(result);

      auto machinesArray = doc["machines"].GetArray();

      uint32_t registrants_ptr = memory_->SystemHeapAlloc(
          sizeof(XSESSION_REGISTRANT) * machinesArray.Size());

      uint32_t users_ptr = memory_->SystemHeapAlloc(
          sizeof(uint64_t) * doc["totalPlayers"].GetInt());

      auto registrants =
          memory_->TranslateVirtual<XSESSION_REGISTRANT*>(registrants_ptr);

      auto users = memory_->TranslateVirtual<xe::be<uint64_t>*>(users_ptr);

      results->registrants_ptr = registrants_ptr;
      results->registrants_count = machinesArray.Size();

      unsigned int machineIndex = 0;
      unsigned int machinePlayerIndex = 0;
      unsigned int resultsPlayerIndex = 0;

      for (const auto& machine : machinesArray) {
        auto playersArray = machine["players"].GetArray();
        registrants[machineIndex].bNumUsers = playersArray.Size();
        registrants[machineIndex].bTrustworthiness = 1;
        unsigned char machineId[8];
        StringToHex(machine["id"].GetString(), machineId);
        registrants[machineIndex].MachineID = UCharArrayToUint64(machineId);
        registrants[machineIndex].rgUsers =
            users_ptr + (8 * resultsPlayerIndex);

        machinePlayerIndex = 0;
        for (const auto& player : playersArray) {
          unsigned char xuid[8];
          StringToHex(player["xuid"].GetString(), xuid);

          users[resultsPlayerIndex] = UCharArrayToUint64(xuid);

          machinePlayerIndex += 1;
          resultsPlayerIndex += 1;
        }

        machineIndex += 1;
      }
#pragma endregion

      return X_E_SUCCESS;
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

      XLiveAPI::XSesion* data = reinterpret_cast<XLiveAPI::XSesion*>(buffer);

      std::random_device rd;
      std::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFFu);

      auto* pSessionInfo = memory_->TranslateVirtual<XLiveAPI::XSESSION_INFO*>(
          data->session_info_ptr);

      for (int i = 0; i < 16; i++) {
        pSessionInfo->keyExchangeKey.ab[i] = i;
      }

      switch (data->flags) {
        case SINGLEPLAYER_WITH_STATS:
          XELOGI("XSessionCreate SINGLEPLAYER_WITH_STATS");
          XELOGI("Session is advertised");
          break;
        case LIVE_MULTIPLAYER_STANDARD:
          XELOGI("XSessionCreate LIVE_MULTIPLAYER_STANDARD");
          XELOGI("Session is advertised");
          break;
        case LIVE_MULTIPLAYER_RANKED:
          XELOGI("XSessionCreate LIVE_MULTIPLAYER_RANKED");
          XELOGI("Session is advertised");
          break;
        case SYSTEMLINK:
          XELOGI("XSessionCreate SYSTEMLINK");
          break;
        case GROUP_LOBBY:
          XELOGI("XSessionCreate GROUP_LOBBY");
          XELOGI("Session is advertised");
          break;
        case GROUP_GAME:
          XELOGI("XSessionCreate GROUP_GAME");
          XELOGI("Session is advertised");
          break;
        default:
          break;
      }

      if (data->flags & HOST) {
        XELOGI("HOST Set");
      }

      if (data->flags & PRESENCE) {
        XELOGI("PRESENCE Set");
        XELOGI("Session is advertised");
      }

      if (data->flags & STATS) {
        XELOGI("STATS Set");
      }

      if (data->flags & MATCHMAKING) {
        XELOGI("MATCHMAKING Set");
        XELOGI("Session is advertised");
      }

      if (data->flags & ARBITRATION) {
        XELOGI("ARBITRATION Set");
      }

      if (data->flags & PEER_NETWORK) {
        XELOGI("PEER_NETWORK Set");
      }

      if (data->flags & SOCIAL_MATCHMAKING_ALLOWED) {
        XELOGI("SOCIAL_MATCHMAKING_ALLOWED Set");
      }

      if (data->flags & INVITES_DISABLED) {
        XELOGI("INVITES_DISABLED Set");
      }

      if (data->flags & JOIN_VIA_PRESENCE_DISABLED) {
        XELOGI("JOIN_VIA_PRESENCE_DISABLED Set");
      }

      if (data->flags & JOIN_IN_PROGRESS_DISABLED) {
        XELOGI("JOIN_IN_PROGRESS_DISABLED Set");
      }

      if (data->flags & JOIN_VIA_PRESENCE_FRIENDS_ONLY) {
        XELOGI("JOIN_VIA_PRESENCE_FRIENDS_ONLY Set");
      }

      if (data->flags & HOST || data->flags & STATS) {
        if (!cvars::upnp) {
          XELOGI("Hosting while UPnP is disabled!");
        }

        Uint64toXNKID(dist(rd), &pSessionInfo->sessionID);
        *memory_->TranslateVirtual<uint64_t*>(data->nonce_ptr) = dist(rd);

        XLiveAPI::XSessionCreate(XNKIDtoUint64(&pSessionInfo->sessionID), data);

        pSessionInfo->hostAddress.inaOnline.s_addr =
            XLiveAPI::OnlineIP().sin_addr.s_addr;

        pSessionInfo->hostAddress.ina.s_addr =
            pSessionInfo->hostAddress.inaOnline.s_addr;

        memcpy(&pSessionInfo->hostAddress.abEnet, XLiveAPI::mac_address, 6);
        memcpy(&pSessionInfo->hostAddress.abOnline, XLiveAPI::mac_address, 6);

        pSessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();
      } else {
        // Check if session id is valid
        auto sessionId = XNKIDtoUint64(&pSessionInfo->sessionID);

        if (sessionId == NULL) {
          assert_always();
          return X_E_SUCCESS;
        }

        auto session = XLiveAPI::XSessionGet(sessionId);

        pSessionInfo->hostAddress.inaOnline.s_addr =
            inet_addr(session.hostAddress.c_str());

        pSessionInfo->hostAddress.ina.s_addr =
            pSessionInfo->hostAddress.inaOnline.s_addr;

        memcpy(&pSessionInfo->hostAddress.abEnet, session.macAddress.c_str(),
               6);
        memcpy(&pSessionInfo->hostAddress.abOnline, session.macAddress.c_str(),
               6);

        pSessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();
      }

      // Check if session id is valid
      auto sessionId = XNKIDtoUint64(&pSessionInfo->sessionID);

      if (sessionId != NULL) {
        XLiveAPI::sessionHandleMap.emplace(
            data->session_handle, XNKIDtoUint64(&pSessionInfo->sessionID));
      }

      XLiveAPI::clearXnaddrCache();
      return X_E_SUCCESS;
    }
    case 0x000B0011: {
      XELOGI("XGISessionDelete");

      struct SessionDelete {
        xe::be<uint32_t> session_handle;
      }* session = reinterpret_cast<SessionDelete*>(buffer);

      XLiveAPI::DeleteSession(
          XLiveAPI::sessionHandleMap[session->session_handle]);

      return X_STATUS_SUCCESS;
    }
    case 0x000B0012: {
      assert_true(buffer_length == 0x14);

      struct SessionJoin {
        xe::be<uint32_t> session_ptr;
        xe::be<uint32_t> array_count;
        xe::be<uint32_t> xuid_array;
        xe::be<uint32_t> user_index_array;
        xe::be<uint32_t> private_slots_array;
      }* data = reinterpret_cast<SessionJoin*>(buffer);

      // Local uses user indices, remote uses XUIDs
      if (data->xuid_array == 0) {
        XELOGI("XGISessionJoinLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr.get(), data->array_count.get(),
               data->xuid_array.get(), data->user_index_array.get(),
               data->private_slots_array.get());
      } else {
        XELOGI("XGISessionJoinRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr.get(), data->array_count.get(),
               data->xuid_array.get(), data->user_index_array.get(),
               data->private_slots_array.get());

        struct XSessionJoinRemote {
          xe::be<uint32_t> session_ptr;
          xe::be<uint32_t> array_count;
          xe::be<uint32_t> xuid_array;
          xe::be<uint32_t> private_slots_array;
          xe::be<uint32_t> overlapped;
        }* data = reinterpret_cast<XSessionJoinRemote*>(buffer);

        auto xuids =
            memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuid_array);

        Document doc;
        doc.SetObject();

        Value xuidsJsonArray(kArrayType);

        for (unsigned int i = 0; i < data->array_count; i++) {
          std::string xuid = to_hex_string(xuids[i]);

          Value value;
          value.SetString(xuid.c_str(), 16, doc.GetAllocator());
          xuidsJsonArray.PushBack(value, doc.GetAllocator());
        }

        doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

        rapidjson::StringBuffer buffer;
        PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        XLiveAPI::SessionJoinRemote(
            XLiveAPI::sessionHandleMap[data->session_ptr], buffer.GetString());
      }

      XLiveAPI::clearXnaddrCache();

      return X_E_SUCCESS;
    }
    case 0x000B0013: {
      assert_true(buffer_length == 0x14);

      struct XSessionLeaveRemote {
        xe::be<uint32_t> session_ptr;
        xe::be<uint32_t> array_count;
        xe::be<uint32_t> xuid_array;
        xe::be<uint32_t> user_index_array;
        xe::be<uint32_t> private_slots_array;
      }* data = reinterpret_cast<XSessionLeaveRemote*>(buffer);

      // Local uses user indices, remote uses XUIDs
      if (data->xuid_array == 0) {
        XELOGI("XGISessionLeaveLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr.get(), data->array_count.get(),
               data->xuid_array.get(), data->user_index_array.get(),
               data->private_slots_array.get());
      } else {
        XELOGI("XGISessionLeaveRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               data->session_ptr.get(), data->array_count.get(),
               data->xuid_array.get(), data->user_index_array.get(),
               data->private_slots_array.get());

        auto xuids =
            memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuid_array);

        Document doc;
        doc.SetObject();

        Value xuidsJsonArray(kArrayType);

        for (unsigned int i = 0; i < data->array_count; i++) {
          std::string xuid = to_hex_string(xuids[i]);

          Value value;
          value.SetString(xuid.c_str(), 16, doc.GetAllocator());
          xuidsJsonArray.PushBack(value, doc.GetAllocator());
        }

        doc.AddMember("xuids", xuidsJsonArray, doc.GetAllocator());

        rapidjson::StringBuffer buffer;
        PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        XLiveAPI::SessionLeaveRemote(
            XLiveAPI::sessionHandleMap[data->session_ptr], buffer.GetString());
      }

      XLiveAPI::clearXnaddrCache();

      return X_E_SUCCESS;
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

      XLiveAPI::XSessionWriteStats* data =
          reinterpret_cast<XLiveAPI::XSessionWriteStats*>(buffer);

      if (XLiveAPI::sessionHandleMap[data->session_handle] == 0) {
        assert_always();
        return X_STATUS_SUCCESS;
      }

      auto leaderboard =
          memory_->TranslateVirtual<XLiveAPI::XSessionViewProperties*>(
              data->leaderboards_guest_address);

      XLiveAPI::SessionWriteStats(
          XLiveAPI::sessionHandleMap[data->session_handle], data, leaderboard);

      return X_STATUS_SUCCESS;
    }
    case 0x000B001B: {
      XELOGI("XSessionSearchByID unimplemented");
      return X_E_SUCCESS;
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