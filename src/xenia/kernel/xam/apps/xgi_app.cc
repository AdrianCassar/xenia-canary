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
#include <winsock2.h>                    // NOLINT(build/include_order)
#include <WS2tcpip.h>                    // NOLINT(build/include_order)
#endif
#include <src/xenia/kernel/kernel_state.cc>
#include <random>
#include <src/xenia/kernel/xam/xam_net.h>
#define RAPIDJSON_HAS_STDSTRING 1
#include <third_party/rapidjson/include/rapidjson/document.h>
#include <third_party/rapidjson/include/rapidjson/prettywriter.h>
#include <third_party/libcurl/include/curl/curl.h>

using namespace rapidjson;

DECLARE_bool(logging);

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

/* 
* Most of the structs below were found in the Source SDK, provided as stubs.
* Specifically, they can be found in the Source 2007 SDK and the Alien Swarm Source SDK.
* Both are available on Steam for free.
* A GitHub mirror of the Alien Swarm SDK can be found here:
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
  xe::be<uint64_t> qwMachineID;
  xe::be<uint32_t> bTrustworthiness;
  xe::be<uint32_t> bNumUsers;
  xe::be<uint32_t> rgUsers;
};

XgiApp::XgiApp(KernelState* kernel_state) : App(kernel_state, 0xFB) {}

// http://mb.mirage.org/bugzilla/xliveless/main.c

struct XNKID {
  uint8_t ab[8];
};

struct XNKEY {
  uint8_t ab[16];
};

struct XNADDR {
  in_addr ina;
  in_addr inaOnline;
  xe::be<uint16_t> wPortOnline;
  uint8_t abEnet[6];
  uint8_t abOnline[20];
};

struct XSESSION_INFO {
  XNKID sessionID;
  XNADDR hostAddress;
  XNKEY keyExchangeKey;
};

struct XUSER_DATA {
  uint8_t type;

  union {
    xe::be<uint32_t> dword_data;      // XUSER_DATA_TYPE_INT32
    xe::be<uint64_t> qword_data;  // XUSER_DATA_TYPE_INT64
    xe::be<double> double_data;  // XUSER_DATA_TYPE_DOUBLE
    struct             // XUSER_DATA_TYPE_UNICODE
    {
      xe::be<uint32_t> string_length;
      xe::be<uint32_t> string_ptr;
    } string;
    xe::be<float> float_data;
    struct
    {
      xe::be<uint32_t> data_length;
      xe::be<uint32_t> data_ptr;
    } binary;
    FILETIME filetime_data;
  };
};

struct XUSER_PROPERTY {
  xe::be<uint32_t> property_id;
  XUSER_DATA value;
};

struct XUSER_CONTEXT {
  xe::be<uint32_t> context_id;
  xe::be<uint32_t> value;
};

struct XSESSION_SEARCHRESULT {
  XSESSION_INFO info;
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

struct XSESSION_LOCAL_DETAILS {
  xe::be<uint32_t> dwUserIndexHost;
  xe::be<uint32_t> dwGameType;
  xe::be<uint32_t> dwGameMode;
  xe::be<uint32_t> dwFlags;
  xe::be<uint32_t> dwMaxPublicSlots;
  xe::be<uint32_t> dwMaxPrivateSlots;
  xe::be<uint32_t> dwAvailablePublicSlots;
  xe::be<uint32_t> dwAvailablePrivateSlots;
  xe::be<uint32_t> dwActualMemberCount;
  xe::be<uint32_t> dwReturnedMemberCount;
  xe::be<uint32_t> eState;
  xe::be<uint64_t> qwNonce;
  XSESSION_INFO sessionInfo;
  XNKID xnkidArbitration;
  xe::be<uint32_t> pSessionMembers;
};

struct XSESSION_MEMBER {
  xe::be<uint64_t> xuidOnline;
  xe::be<uint32_t> dwUserIndex;
  xe::be<uint32_t> dwFlags;
};

// TODO: Remove - Codie
std::size_t callback(const char* in, std::size_t size, std::size_t num,
                     char* out) {
  std::string data(in, (std::size_t)size * num);
  *((std::stringstream*)out) << data;
  return size * num;
}

// TODO: Move - Codie
bool StringToHex(const std::string& inStr, unsigned char* outStr) {
  size_t len = inStr.length();
  for (size_t i = 0; i < len; i += 2) {
    sscanf(inStr.c_str() + i, "%2hhx", outStr);
    ++outStr;
  }
  return true;
}

xe::be<uint64_t> XNKIDtoUint64(XNKID* sessionID) {
  int i;
  xe::be<uint64_t> sessionId64 = 0;
  for (i = 7; i >= 0; --i) {
    sessionId64 = sessionId64 << 8;
    sessionId64 |= (uint64_t)sessionID->ab[7 - i];
  }

  return sessionId64;
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

xe::be<uint64_t> UCharArrayToUint64(unsigned char* data) {
  int i;
  xe::be<uint64_t> out = 0;
  for (i = 7; i >= 0; --i) {
    out = out << 8;
    out |= (uint64_t)data[7-i];
  }

  return out;
}

void Uint64toXNKID(xe::be<uint64_t> sessionID, XNKID* xnkid) {
  for (int i = 0; i < 8; i++) {
    xnkid->ab[i] = ((sessionID >> (8 * i)) & 0XFF);
  }
}

// TODO: Move - Codie
std::map<xe::be<uint32_t>, xe::be<uint64_t>> sessionHandleMap{};

struct XSESSION_VIEW_PROPERTIES {
  xe::be<uint32_t> leaderboard_id;
  xe::be<uint32_t> properties_count;
  xe::be<uint32_t> properties_guest_address;
};


struct XUSER_STATS_READ_RESULTS {
  xe::be<uint32_t> dwNumViews;
  xe::be<uint32_t> pViews;
};


struct XUSER_STATS_VIEW {
  xe::be<uint32_t> dwViewId;
  xe::be<uint32_t> dwTotalViewRows;
  xe::be<uint32_t> dwNumRows;
  xe::be<uint32_t> pRows;
};

struct XUSER_STATS_ROW {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> dwRank;
  xe::be<uint64_t> i64Rating;
  CHAR szGamertag[16];
  xe::be<uint32_t> dwNumColumns;
  xe::be<uint32_t> pColumns;
};

struct XUSER_STATS_COLUMN {
  xe::be<uint16_t> wColumnId;
  XUSER_DATA Value;
};

struct XUSER_STATS_SPEC {
  xe::be<uint32_t> dwViewId;
  xe::be<uint32_t> dwNumColumnIds;
  xe::be<uint16_t> rgwColumnIds[0x40];
};




X_HRESULT XgiApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);

  if (cvars::logging) {
    XELOGI("DispatchMessageSync: {:X}", message);
  }

  switch (message) {
    case 0x000B0018: {
      struct message_data {
        xe::be<uint32_t> hSession;
        xe::be<uint32_t> dwFlags;
        xe::be<uint32_t> dwMaxPublicSlots;
        xe::be<uint16_t> dwMaxPrivateSlots;
      }* data = reinterpret_cast<message_data*>(buffer);

      XELOGI(
          "XSessionModify({:08X} {:08X} {:08X} {:08X})",
          data->hSession, data->dwFlags, data->dwMaxPublicSlots, data->dwMaxPrivateSlots);

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

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0') << sessionHandleMap[data->hSession];

      d.AddMember("flags", data->dwFlags, allocator);
      d.AddMember("publicSlotsCount", data->dwMaxPublicSlots, allocator);
      d.AddMember("privateSlotsCount", data->dwMaxPrivateSlots, allocator);

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

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream url;
      url << GetApiAddress() << "/title/"
          << titleId.str() << "/sessions/"
          << sessionIdStr.str() << "/modify";

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


      return X_E_SUCCESS;
    }
    case 0x000B001C: {
      XELOGI("XSessionSearchEx");

      int i = 0;
      int j = 0;

      struct message_data {
        xe::be<uint32_t> proc_index;
        xe::be<uint32_t> user_index;
        xe::be<uint32_t> num_results;
        xe::be<uint16_t> num_props;
        xe::be<uint16_t> num_ctx;
        xe::be<uint32_t> props_ptr;
        xe::be<uint32_t> ctx_ptr;
        xe::be<uint32_t> cbResultsBuffer;
        xe::be<uint32_t> pSearchResults;
        xe::be<uint32_t> num_users;
      }* data = reinterpret_cast<message_data*>(buffer);

      auto* pSearchContexts =
          memory_->TranslateVirtual<XUSER_CONTEXT*>(data->ctx_ptr);

      uint32_t results_ptr = data->pSearchResults + sizeof(XSESSION_SEARCHRESULT_HEADER);
      auto* result = memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(results_ptr);

            auto resultsHeader =
          memory_->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
              data->pSearchResults);

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

      d.AddMember("searchIndex", data->proc_index, allocator);
      d.AddMember("resultsCount", data->num_results, allocator);

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

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream url;
      url << GetApiAddress() << "/title/" << titleId.str()
          << "/sessions/search";

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

        const Value& sessionsJsonArray = d.GetArray();

        unsigned int i = 0;
        for (Value::ConstValueIterator sessionJsonObjectPtr =
                 sessionsJsonArray.Begin();
             sessionJsonObjectPtr != sessionsJsonArray.End();
             ++sessionJsonObjectPtr) {
          uint32_t result_guest_address = data->pSearchResults +
                                          sizeof(XSESSION_SEARCHRESULT_HEADER) +
                                          (sizeof(XSESSION_SEARCHRESULT) * i);
          auto* resultHostPtr =
              memory_->TranslateVirtual<XSESSION_SEARCHRESULT*>(
                  result_guest_address);


          // if (i > 1) break;
          if (data->num_results <= i) break;
            result[i].contexts_count = (uint32_t)data->num_ctx;
            result[i].properties_count = 3;
            result[i].contexts_ptr = data->ctx_ptr;
            result[i].properties_ptr = data->props_ptr;
          result[i].filled_priv_slots =
              (*sessionJsonObjectPtr)["filledPrivateSlotsCount"].GetInt();
          result[i].filled_public_slots =
              (*sessionJsonObjectPtr)["filledPublicSlotsCount"].GetInt();
          result[i].open_priv_slots =
              (*sessionJsonObjectPtr)["openPrivateSlotsCount"].GetInt();
          result[i].open_public_slots =
              (*sessionJsonObjectPtr)["openPublicSlotsCount"].GetInt();
          StringToHex((*sessionJsonObjectPtr)["id"].GetString(),
                        (unsigned char*)&result[i].info.sessionID.ab);

            resultHostPtr[i].info.hostAddress.wPortOnline =
                (*sessionJsonObjectPtr)["port"].GetInt();

            for (int j = 0; j < 16; j++) {
              result[i].info.keyExchangeKey.ab[j] = j;
            }

            StringToHex((*sessionJsonObjectPtr)["macAddress"].GetString(), result[i].info.hostAddress.abEnet);
            StringToHex((*sessionJsonObjectPtr)["macAddress"].GetString(), result[i].info.hostAddress.abOnline);

            inet_pton(AF_INET,
            (*sessionJsonObjectPtr)["hostAddress"].GetString(),
                      &resultHostPtr[i].info.hostAddress.ina.S_un.S_addr);
            inet_pton(AF_INET,
            (*sessionJsonObjectPtr)["hostAddress"].GetString(),
                      &resultHostPtr[i].info.hostAddress.inaOnline.S_un.S_addr);

          i += 1;
        }

        resultsHeader->search_results_count = i;
        resultsHeader->search_results_ptr =
            data->pSearchResults + sizeof(XSESSION_SEARCHRESULT_HEADER);
      }
#pragma endregion
      return X_E_SUCCESS;
    }
    case 0xB001D: {
      struct message_data {
        xe::be<uint32_t> unk_handle;
        xe::be<uint32_t> details_buffer_size;
        xe::be<uint32_t> details_buffer;
        xe::be<uint32_t> unk4;
        xe::be<uint32_t> unk5;
        xe::be<uint32_t> unk6;
      }* data = reinterpret_cast<message_data*>(buffer);

      XELOGI("XSessionGetDetails({:08X});", buffer_length);

      auto details = memory_->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->details_buffer);

            #pragma region Curl
            /*
                TODO:
                    - Refactor the CURL out to a separate class.
                    - Use the overlapped task to do this asyncronously.
            */
      
            std::stringstream sessionIdStr;
            sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                         << std::setfill('0') <<
                         sessionHandleMap[data->unk_handle];
      
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
      
            std::stringstream titleId;
            titleId << std::hex << std::noshowbase << std::setw(8)
                    << std::setfill('0') << kernel_state()->title_id();
      
            std::stringstream url;
            url << GetApiAddress() << "/title/"
                << titleId.str() <<
            "/sessions/"
                << sessionIdStr.str() << "/details";
      
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
      
                    memcpy(&details->sessionInfo.sessionID,
                    sessionIdStr.str().c_str(), 8);
      
      
              details->sessionInfo.hostAddress.inaOnline.S_un.S_addr =
                  inet_addr(d["hostAddress"].GetString());
      
              details->sessionInfo.hostAddress.ina.S_un.S_addr =
                  details->sessionInfo.hostAddress.inaOnline.S_un.S_addr;
      
              auto myMac = new unsigned char[12];
              StringToHex(d["macAddress"].GetString(), myMac);
      
              memcpy(&details->sessionInfo.hostAddress.abEnet, myMac, 6);
      
              details->sessionInfo.hostAddress.wPortOnline =
              d["port"].GetInt();
      
              details->dwUserIndexHost = 0;
              details->dwGameMode = 0;
              details->dwGameType = 0;
              details->eState = 0;
      
              details->dwFlags = d["flags"].GetInt();
              details->dwMaxPublicSlots = d["publicSlotsCount"].GetInt();
              details->dwMaxPrivateSlots = d["privateSlotsCount"].GetInt();
              details->dwAvailablePrivateSlots =
              d["openPublicSlotsCount"].GetInt();
              details->dwAvailablePublicSlots =
              d["openPrivateSlotsCount"].GetInt();
              details->dwActualMemberCount =
              d["filledPublicSlotsCount"].GetInt() +
                                             d["filledPrivateSlotsCount"].GetInt();
              details->dwReturnedMemberCount = d["players"].GetArray().Size();
      
      
              details->qwNonce = 0xAAAAAAAAAAAAAAAA;
      
              for (int i = 0; i < 16; i++) {
                details->sessionInfo.keyExchangeKey.ab[i] = i;
              }
      
              for (int i = 0; i < 20; i++) {
                details->sessionInfo.hostAddress.abOnline[i] = i;
              }
      
              uint32_t members_ptr =
                  memory_->SystemHeapAlloc(sizeof(XSESSION_MEMBER) *
                  details->dwReturnedMemberCount);
      
              auto members =
              memory_->TranslateVirtual<XSESSION_MEMBER*>(members_ptr);
      
              details->pSessionMembers = members_ptr;
      
              unsigned int i = 0;
              for (const auto& player : d["players"].GetArray()) {
                members[i].dwUserIndex = 0xFE;
                StringToHex(player["xuid"].GetString(), (unsigned char*)&members[i].xuidOnline); i += 1;
              }
      
      
            } else {
              return 1;
            }
      #pragma endregion

      return X_E_SUCCESS;
    }
    case 0xB001E: {
        // I think there's more in here.
      struct message_data {
        xe::be<uint32_t> session_handle;
        xe::be<uint32_t> session_info;
      }* data = reinterpret_cast<message_data*>(buffer);

      XELOGI("XSessionMigrateHost({:08X});", buffer_length);

      auto sessionInfo = memory_->TranslateVirtual<XSESSION_INFO*>(data->session_info);
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
                       << std::setfill('0')
                       << MacAddresstoUint64(getMacAddress());

      d.AddMember("hostAddress", std::string(str), allocator);
      d.AddMember("macAddress", macAddressString.str(), allocator);
      d.AddMember("port", getPort(), allocator);

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

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0')
                   << sessionHandleMap[data->session_handle];

      std::stringstream url;
      url << GetApiAddress() << "/title/"
          << titleId.str()
          << "/sessions/" << sessionIdStr.str() << "/migrate";

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

      if (httpCode == 200) {
        rapidjson::Document d;
        d.Parse(out.str());

        sessionInfo->hostAddress.inaOnline.S_un.S_addr =
            inet_addr(d["hostAddress"].GetString());

        sessionInfo->hostAddress.ina.S_un.S_addr =
            sessionInfo->hostAddress.inaOnline.S_un.S_addr;

        auto myMac = new unsigned char[6];
        StringToHex(d["macAddress"].GetString(), myMac);

        memcpy(&sessionInfo->hostAddress.abEnet, myMac, 6);

        sessionInfo->hostAddress.wPortOnline = getPort();
      }
#pragma endregion

      return X_E_SUCCESS;
    }
    case 0x000B0021: {
      struct message_data {
        xe::be<uint32_t> titleId;
        xe::be<uint32_t> xuids_count;
        xe::be<uint32_t> xuids_guest_address;
        xe::be<uint32_t> specs_count;
        xe::be<uint32_t> specs_guest_address;
        xe::be<uint32_t> results_size;
        xe::be<uint32_t> results_guest_address;
      }* data = reinterpret_cast<message_data*>(buffer);

      if (!data->results_guest_address) return 1;

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

      rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);
      auto xuids = memory_->TranslateVirtual<xe::be<uint64_t>*>(data->xuids_guest_address);

      for (unsigned int playerIndex = 0; playerIndex < data->xuids_count; playerIndex++) {
        std::stringstream xuidSS;
        xuidSS << std::hex << std::noshowbase << std::setw(16)
               << std::setfill('0') << xuids[playerIndex];
        rapidjson::Value value;
        value.SetString(xuidSS.str().c_str(), 16, allocator);
        xuidsJsonArray.PushBack(value, allocator);
      }

      d.AddMember("players", xuidsJsonArray, allocator);

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      d.AddMember("titleId", titleId.str(), allocator);

      rapidjson::Value leaderboardQueryJsonArray(rapidjson::kArrayType);
      auto queries = memory_->TranslateVirtual<XUSER_STATS_SPEC*>(data->specs_guest_address);

      for (unsigned int queryIndex = 0; queryIndex < data->specs_count; queryIndex++) {
        rapidjson::Value queryObject(rapidjson::kObjectType);
        queryObject.AddMember("id", queries[queryIndex].dwViewId, allocator);
        rapidjson::Value statIdsArray(rapidjson::kArrayType);
        for (uint32_t statIdIndex = 0; statIdIndex < queries[queryIndex].dwNumColumnIds; statIdIndex++) {
          statIdsArray.PushBack(queries[queryIndex].rgwColumnIds[statIdIndex], allocator);
        }
        queryObject.AddMember("statisticIds", statIdsArray, allocator);
        leaderboardQueryJsonArray.PushBack(queryObject, allocator);

      }

      d.AddMember("queries", leaderboardQueryJsonArray, allocator);

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
      url << GetApiAddress() << "/leaderboards/find";
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
        rapidjson::Document leaderboards;
        leaderboards.Parse(out.str());
        const Value& leaderboardsArray = leaderboards.GetArray();

        auto leaderboards_guest_address = memory_->SystemHeapAlloc(
            sizeof(XUSER_STATS_VIEW) * leaderboardsArray.Size());
        auto leaderboard = memory_->TranslateVirtual<XUSER_STATS_VIEW*>(
            leaderboards_guest_address);
        auto resultsHeader =
            memory_->TranslateVirtual<XUSER_STATS_READ_RESULTS*>(
                data->results_guest_address);
        resultsHeader->dwNumViews = leaderboardsArray.Size();
        resultsHeader->pViews = leaderboards_guest_address;

        uint32_t leaderboardIndex = 0;
        for (Value::ConstValueIterator leaderboardObjectPtr = leaderboardsArray.Begin(); leaderboardObjectPtr != leaderboardsArray.End(); ++leaderboardObjectPtr) {
          leaderboard[leaderboardIndex].dwViewId = (*leaderboardObjectPtr)["id"].GetInt();
          auto playersArray = (*leaderboardObjectPtr)["players"].GetArray();
          leaderboard[leaderboardIndex].dwNumRows = playersArray.Size();
          leaderboard[leaderboardIndex].dwTotalViewRows = playersArray.Size();
          auto players_guest_address = memory_->SystemHeapAlloc(sizeof(XUSER_STATS_ROW) * playersArray.Size());
          auto player = memory_->TranslateVirtual<XUSER_STATS_ROW*>(players_guest_address);
          leaderboard[leaderboardIndex].pRows = players_guest_address;

          uint32_t playerIndex = 0;
          for (Value::ConstValueIterator playerObjectPtr = playersArray.Begin(); playerObjectPtr != playersArray.End(); ++playerObjectPtr) {
            player[playerIndex].dwRank = 1;
            player[playerIndex].i64Rating = 1;
            auto gamertag = (*playerObjectPtr)["gamertag"].GetString();
            auto gamertagLength = (*playerObjectPtr)["gamertag"].GetStringLength();
            memcpy(player[playerIndex].szGamertag, gamertag, gamertagLength);
            unsigned char xuid[8];
            StringToHex((*playerObjectPtr)["xuid"].GetString(), xuid);
            player[playerIndex].xuid = UCharArrayToUint64(xuid);

            auto statisticsArray = (*playerObjectPtr)["stats"].GetArray();
            player[playerIndex].dwNumColumns = statisticsArray.Size();
            auto stats_guest_address = memory_->SystemHeapAlloc(sizeof(XUSER_STATS_COLUMN) * statisticsArray.Size());
            auto stat = memory_->TranslateVirtual<XUSER_STATS_COLUMN*>(stats_guest_address);
            player[playerIndex].pColumns = stats_guest_address;

            uint32_t statIndex = 0;
            for (Value::ConstValueIterator statObjectPtr = statisticsArray.Begin(); statObjectPtr != statisticsArray.End(); ++statObjectPtr) {
              stat[statIndex].wColumnId = (*statObjectPtr)["id"].GetUint();
              stat[statIndex].Value.type = (*statObjectPtr)["type"].GetUint();

              switch (stat[statIndex].Value.type) {
                  case 1:
                    stat[statIndex].Value.dword_data = (*statObjectPtr)["value"].GetUint();
                    break;
                  case 2:
                    stat[statIndex].Value.qword_data = (*statObjectPtr)["value"].GetUint64();
                    break;
                  default:
                    XELOGW("Unimplemented stat type for read, will attempt anyway.", stat[statIndex].Value.type);
                    if ((*statObjectPtr)["value"].IsNumber())
                        stat[statIndex].Value.qword_data = (*statObjectPtr)["value"].GetUint64();
              }

              stat[statIndex].Value.type = (*statObjectPtr)["type"].GetInt();
              statIndex++;
            }

            playerIndex++;
          }

          leaderboardIndex++;
        }
      }
#pragma endregion
      return X_E_SUCCESS;
    }
    case 0x000B001A: {
      struct message_data {
        xe::be<uint32_t> session_handle;
        xe::be<uint32_t> flags;
        xe::be<uint32_t> unk1;
        xe::be<uint32_t> unk2;
        xe::be<uint32_t> session_nonce;
        xe::be<uint32_t> results_buffer_length;
        xe::be<uint32_t> results_buffer;
        xe::be<uint32_t> unk3;
      }* data = reinterpret_cast<message_data*>(buffer);

      XELOGI("XSessionArbitrationRegister({:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X});", 
          data->session_handle, data->flags, data->unk1, data->unk2,
          data->session_nonce, data->results_buffer_length,
          data->results_buffer, data->unk3);

      auto results =
          memory_->TranslateVirtual<XSESSION_REGISTRATION_RESULTS*>(
          data->results_buffer);

      // TODO: Remove hardcoded results, populate properly.

                  #pragma region Curl
      /*
          TODO:
              - Refactor the CURL out to a separate class.
              - Use the overlapped task to do this asyncronously.
      */

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0')
                   << sessionHandleMap[data->session_handle];

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

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream url;
      url << GetApiAddress() << "/title/" << titleId.str() << "/sessions/"
          << sessionIdStr.str() << "/arbitration";

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

        auto machinesArray = d["machines"].GetArray();

        uint32_t registrants_ptr = memory_->SystemHeapAlloc(sizeof(XSESSION_REGISTRANT) *
                                     machinesArray.Size());

        uint32_t users_ptr =
            memory_->SystemHeapAlloc(sizeof(uint64_t) * d["totalPlayers"].GetInt());

        auto registrants =
            memory_->TranslateVirtual<XSESSION_REGISTRANT*>(registrants_ptr);

        auto users =
            memory_->TranslateVirtual<xe::be<uint64_t>*>(users_ptr);

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
          registrants[machineIndex].qwMachineID = UCharArrayToUint64(machineId);
          registrants[machineIndex].rgUsers = users_ptr + (8 * resultsPlayerIndex);

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

        UserProfile* user_profile = kernel_state_->user_profile(user_index);
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
        const auto property = title_xdbf.GetContext(property_id);
        const XLanguage title_language = title_xdbf.GetExistingLanguage(
            static_cast<XLanguage>(XLanguage::kEnglish));
        const std::string desc =
            title_xdbf.GetStringTableEntry(title_language, property.string_id);
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
            achievement->user_idx, 0, achievement->achievement_id);
      }
      return X_E_SUCCESS;
    }
    case 0x000B0010: {
        assert_true(!buffer_length || buffer_length == 28);
        // Sequence:
        // - XamSessionCreateHandle
        // - XamSessionRefObjByHandle
        // - [this]
        // - CloseHandle

        uint32_t session_handle = xe::load_and_swap<uint32_t>(buffer + 0x0);
        uint32_t flags = xe::load_and_swap<uint32_t>(buffer + 0x4);
        uint32_t num_slots_public = xe::load_and_swap<uint32_t>(buffer + 0x8);
        uint32_t num_slots_private =
        xe::load_and_swap<uint32_t>(buffer + 0xC); uint32_t user_index =
        xe::load_and_swap<uint32_t>(buffer + 0x10); uint32_t
        session_info_ptr = xe::load_and_swap<uint32_t>(buffer + 0x14);
        uint32_t nonce_ptr = xe::load_and_swap<uint32_t>(buffer + 0x18);
      
        std::random_device rd;
        std::uniform_int_distribution<uint64_t> dist(0,0xFFFFFFFFFFFFFFFFu);
      
        auto* pSessionInfo =
            memory_->TranslateVirtual<XSESSION_INFO*>(session_info_ptr);
      
        for (int i = 0; i < 16; i++) {
            pSessionInfo->keyExchangeKey.ab[i] = i;
        }
      
        // If host
        if (flags & 1) {
      
            Uint64toXNKID(dist(rd), &pSessionInfo->sessionID);
            *memory_->TranslateVirtual<uint64_t*>(nonce_ptr) = dist(rd);
      
      
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
      
            std::stringstream sessionIdStr;
            sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                         << std::setfill('0') 
                << XNKIDtoUint64(&pSessionInfo->sessionID);
      
            std::stringstream macAddressString;
            macAddressString << std::hex << std::noshowbase << std::setw(12)
                             << std::setfill('0') 
                << MacAddresstoUint64(getMacAddress());
      
            d.AddMember("sessionId", sessionIdStr.str(), allocator);
            d.AddMember("flags", flags, allocator);
            d.AddMember("publicSlotsCount", num_slots_public, allocator);
            d.AddMember("privateSlotsCount", num_slots_private, allocator);
            d.AddMember("userIndex", user_index, allocator);
            d.AddMember("hostAddress", std::string(str), allocator);
            d.AddMember("macAddress", macAddressString.str(), allocator); 
            d.AddMember("port", getPort(), allocator);
      
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
      
            std::stringstream titleId;
            titleId << std::hex << std::noshowbase << std::setw(8)
                    << std::setfill('0') << kernel_state()->title_id();
      
            std::stringstream url;
            url << GetApiAddress() << "/title/"
                << titleId.str() << "/sessions";
      
            if (cvars::logging) {
                XELOGI("cURL: {}", url.str());

                curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
                curl_easy_setopt(curl, CURLOPT_STDERR, stderr);
            }

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

            pSessionInfo->hostAddress.inaOnline.S_un.S_addr = getOnlineIp().S_un.S_addr;

            pSessionInfo->hostAddress.ina.S_un.S_addr =
                pSessionInfo->hostAddress.inaOnline.S_un.S_addr;

            memcpy(&pSessionInfo->hostAddress.abEnet, getMacAddress(), 6);
            memcpy(&pSessionInfo->hostAddress.abOnline, getMacAddress(), 6);

            pSessionInfo->hostAddress.wPortOnline = getPort();
      
    #pragma endregion
        } else {
    #pragma region Curl

            /*
                TODO:
                    - Refactor the CURL out to a separate class.
                    - Use the overlapped task to do this asyncronously.
            */
      
            std::stringstream sessionIdStr;
            sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                         << std::setfill('0')
                         << XNKIDtoUint64(&pSessionInfo->sessionID);

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
      
            std::stringstream titleId;
            titleId << std::hex << std::noshowbase << std::setw(8)
                    << std::setfill('0') << kernel_state()->title_id();
      
            std::stringstream url;
            url << GetApiAddress() << "/title/" << titleId.str() << "/sessions/"
                << sessionIdStr.str();
      
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
      
                pSessionInfo->hostAddress.inaOnline.S_un.S_addr =
                    inet_addr(d["hostAddress"].GetString());
      
                pSessionInfo->hostAddress.ina.S_un.S_addr =
                    pSessionInfo->hostAddress.inaOnline.S_un.S_addr;
      
                auto myMac = new unsigned char[6];
                StringToHex(d["macAddress"].GetString(), myMac);
      
                memcpy(&pSessionInfo->hostAddress.abEnet, myMac, 6);
                memcpy(&pSessionInfo->hostAddress.abOnline, myMac, 6);
      
                pSessionInfo->hostAddress.wPortOnline = getPort();

                memcpy(&pSessionInfo->hostAddress.abEnet, myMac, 6);
            }
    #pragma endregion
        }
      
        sessionHandleMap.emplace(session_handle, XNKIDtoUint64(&pSessionInfo->sessionID));
        clearXnaddrCache();
        return X_E_SUCCESS;
    }
    case 0x000B0011: {
      // TODO(PermaNull): reverse buffer contents.
      XELOGD("XGISessionDelete");

      struct message_data {
        xe::be<uint32_t> session_handle;
      }* data = reinterpret_cast<message_data*>(buffer);

      #pragma region Curl
      /*
          TODO:
              - Refactor the CURL out to a separate class.
              - Use the overlapped task to do this asyncronously.
      */

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0')
                   << sessionHandleMap[data->session_handle];

      CURL* curl;
      CURLcode res;

      curl_global_init(CURL_GLOBAL_ALL);
      curl = curl_easy_init();
      if (curl == NULL) {
        return 128;
      }

      struct curl_slist* headers = NULL;
      headers = curl_slist_append(headers, "Content-Type: application/json");
      headers = curl_slist_append(headers, "Accept: application/json");
      headers = curl_slist_append(headers, "charset: utf-8");

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream url;
      url << GetApiAddress() << "/title/"
          << titleId.str() << "/sessions/"
          << sessionIdStr.str();

      curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");

      res = curl_easy_perform(curl);

      curl_easy_cleanup(curl);
      int httpCode(0);
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      curl_global_cleanup();

#pragma endregion

      clearXnaddrCache();
      return X_STATUS_SUCCESS;
    }
    case 0x000B0012: {
      assert_true(buffer_length == 0x14);
      uint32_t session_ptr = xe::load_and_swap<uint32_t>(buffer + 0x0);
      uint32_t array_count = xe::load_and_swap<uint32_t>(buffer + 0x4);
      uint32_t xuid_array = xe::load_and_swap<uint32_t>(buffer + 0x8);
      uint32_t user_index_array = xe::load_and_swap<uint32_t>(buffer + 0xC);
      uint32_t private_slots_array = xe::load_and_swap<uint32_t>(buffer + 0x10);

      // Local uses user indices, remote uses XUIDs
      if (xuid_array == 0) {
        XELOGD("XGISessionJoinLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               session_ptr, array_count, xuid_array, user_index_array,
               private_slots_array);
      } else {
        XELOGD("XGISessionJoinRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               session_ptr, array_count, xuid_array, user_index_array,
               private_slots_array);

        #pragma region Curl
        /*
            TODO:
                - Refactor the CURL out to a separate class.
                - Use the overlapped task to do this asyncronously.
        */
        struct message_data {
          xe::be<uint32_t> hSession;
          xe::be<uint32_t> array_count;
          xe::be<uint32_t> xuid_array;
          xe::be<uint32_t> user_index_array;
          xe::be<uint32_t> private_slots_array;
        }* data = reinterpret_cast<message_data*>(buffer);

        auto xuids = memory_->TranslateVirtual<xe::be<uint64_t>*>(xuid_array);

        std::stringstream sessionIdStr;
        sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                     << std::setfill('0') << sessionHandleMap[data->hSession];

        Document d;
        d.SetObject();

        rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);
        Document::AllocatorType& allocator = d.GetAllocator();

        size_t sz = allocator.Size();

        for (unsigned int i = 0; i < array_count; i++) {
          std::stringstream xuidSS;
          xuidSS << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << xuids[i];
          rapidjson::Value value;
          value.SetString(xuidSS.str().c_str(), 16, allocator);
          xuidsJsonArray.PushBack(value, allocator);
        }

        d.AddMember("xuids", xuidsJsonArray, allocator);

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

        std::stringstream titleId;
        titleId << std::hex << std::noshowbase << std::setw(8)
                << std::setfill('0') << kernel_state()->title_id();

        std::stringstream url;
        url << GetApiAddress() << "/title/" << titleId.str() << "/sessions/"
            << sessionIdStr.str() << "/join";

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
      clearXnaddrCache();
      return X_E_SUCCESS;
    }
    case 0x000B0013: {
      assert_true(buffer_length == 0x14);
      uint32_t session_ptr = xe::load_and_swap<uint32_t>(buffer + 0x0);
      uint32_t array_count = xe::load_and_swap<uint32_t>(buffer + 0x4);
      uint32_t xuid_array = xe::load_and_swap<uint32_t>(buffer + 0x8);
      uint32_t user_index_array = xe::load_and_swap<uint32_t>(buffer + 0xC);
      uint32_t unk010 = xe::load_and_swap<uint32_t>(buffer + 0x10);

      // Local uses user indices, remote uses XUIDs
      if (xuid_array == 0) {
        XELOGD("XGISessionLeaveLocal({:08X}, {}, {:08X}, {:08X}, {:08X})",
               session_ptr, array_count, xuid_array, user_index_array, unk010);
      } else {
        XELOGD("XGISessionLeaveRemote({:08X}, {}, {:08X}, {:08X}, {:08X})",
               session_ptr, array_count, xuid_array, user_index_array, unk010);

                #pragma region Curl
        /*
            TODO:
                - Refactor the CURL out to a separate class.
                - Use the overlapped task to do this asyncronously.
        */
        struct message_data {
          xe::be<uint32_t> hSession;
          xe::be<uint32_t> array_count;
          xe::be<uint32_t> xuid_array;
          xe::be<uint32_t> user_index_array;
          xe::be<uint32_t> private_slots_array;
        }* data = reinterpret_cast<message_data*>(buffer);

        auto xuids = memory_->TranslateVirtual<xe::be<uint64_t>*>(xuid_array);

        std::stringstream sessionIdStr;
        sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                     << std::setfill('0') << sessionHandleMap[data->hSession];

        Document d;
        d.SetObject();

        rapidjson::Value xuidsJsonArray(rapidjson::kArrayType);
        Document::AllocatorType& allocator = d.GetAllocator();

        size_t sz = allocator.Size();

        for (unsigned int i = 0; i < array_count; i++) {
          std::stringstream xuidSS;
          xuidSS << std::hex << std::noshowbase << std::setw(16)
                 << std::setfill('0') << xuids[i];
          rapidjson::Value value;
          value.SetString(xuidSS.str().c_str(), 16, allocator);
          xuidsJsonArray.PushBack(value, allocator);
        }

        d.AddMember("xuids", xuidsJsonArray, allocator);

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

        std::stringstream titleId;
        titleId << std::hex << std::noshowbase << std::setw(8)
                << std::setfill('0') << kernel_state()->title_id();

        std::stringstream url;
        url << GetApiAddress() << "/title/"
            << titleId.str() << "/sessions/"
            << sessionIdStr.str() << "/leave";

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

      clearXnaddrCache();
      return X_E_SUCCESS;
    }
    case 0x000B0014: {
      // Gets 584107FB in game.
      // get high score table?
      XELOGD("XGI_unknown");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0015: {
      // send high scores?
      XELOGD("XGI_unknown");
      return X_STATUS_SUCCESS;
    }
    case 0x000B0025: {
      struct message_data {
        xe::be<uint32_t> hSession;
        xe::be<uint32_t> unk1;
        xe::be<uint64_t> xuid;
        xe::be<uint32_t> number_of_leaderboards;
        xe::be<uint32_t> leaderboards_guest_address;
      }* data = reinterpret_cast<message_data*>(buffer);

      auto leaderboard = memory_->TranslateVirtual<XSESSION_VIEW_PROPERTIES*>(data->leaderboards_guest_address);

      Document rootObject;
      rootObject.SetObject();
      rapidjson::Value leaderboardsObject(rapidjson::kObjectType);
      Document::AllocatorType& allocator = rootObject.GetAllocator();

      std::stringstream xuidSS;
      xuidSS << std::hex << std::noshowbase << std::setw(16)
             << std::setfill('0') << data->xuid;

      for (uint32_t leaderboardIndex = 0; leaderboardIndex < data->number_of_leaderboards; leaderboardIndex++) {
        auto statistics = memory_->TranslateVirtual<XUSER_PROPERTY*>(leaderboard[leaderboardIndex].properties_guest_address);
        rapidjson::Value leaderboardObject(rapidjson::kObjectType);
        rapidjson::Value statsObject(rapidjson::kObjectType);

        for (uint32_t statisticIndex = 0; statisticIndex < leaderboard[leaderboardIndex].properties_count; statisticIndex++) {
          rapidjson::Value statObject(rapidjson::kObjectType);

          statObject.AddMember("type", statistics[statisticIndex].value.type, allocator);

          switch (statistics[statisticIndex].value.type) { 
            case 1:
              statObject.AddMember("value", statistics[statisticIndex].value.dword_data, allocator);
              break;
            case 2:
              statObject.AddMember("value", statistics[statisticIndex].value.qword_data, allocator);
              break;
            default:
              XELOGW("Unimplemented statistic type for write", statistics[statisticIndex].value.type);
              break;
          }

          std::stringstream propertyId;
          propertyId << std::hex << std::noshowbase << std::setw(8)
                  << std::setfill('0')
                  << statistics[statisticIndex].property_id;

          Value statisticIdKey(propertyId.str(), allocator);
          statsObject.AddMember(statisticIdKey, statObject, allocator);
        }

        leaderboardObject.AddMember("stats", statsObject, allocator);
        Value leaderboardIdKey(std::to_string(leaderboard[leaderboardIndex].leaderboard_id).c_str(), allocator);
        leaderboardsObject.AddMember(leaderboardIdKey, leaderboardObject, allocator);
      }

      rootObject.AddMember("leaderboards", leaderboardsObject, allocator);
      rootObject.AddMember("xuid", xuidSS.str(), allocator);

      rapidjson::StringBuffer strbuf;
      PrettyWriter<rapidjson::StringBuffer> writer(strbuf);
      rootObject.Accept(writer);

      CURL* curl;

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

      std::stringstream titleId;
      titleId << std::hex << std::noshowbase << std::setw(8)
              << std::setfill('0') << kernel_state()->title_id();

      std::stringstream sessionIdStr;
      sessionIdStr << std::hex << std::noshowbase << std::setw(16)
                   << std::setfill('0') << sessionHandleMap[data->hSession];

      std::stringstream url;
      url << GetApiAddress() << "/title/"
          << titleId.str() << "/sessions/" << sessionIdStr.str()
          << "/leaderboards";

      curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());

      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "xenia");
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strbuf.GetString());

      curl_easy_perform(curl);

      curl_easy_cleanup(curl);
      int httpCode(0);
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      curl_global_cleanup();

      

      return X_STATUS_SUCCESS;
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
        UserProfile* user_profile = kernel_state_->user_profile(user_index);
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
