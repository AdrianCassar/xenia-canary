/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnet_signalling.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/xnet_qos.h"
#include "xenia/kernel/xnet_stream.h"

#include <chrono>
#include <cstring>

// clang-format off
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
#include "third_party/libcurl/include/curl/websockets.h"
// clang-format on

namespace xe {
namespace kernel {

namespace {

std::string BuildWsUrl(const std::string& api_base) {
  std::string base = api_base;
  while (!base.empty() && (base.back() == '/' || base.back() == '\\')) {
    base.pop_back();
  }
  if (base.rfind("https://", 0) == 0) {
    return "wss://" + base.substr(8) + "/ws";
  }
  if (base.rfind("http://", 0) == 0) {
    return "ws://" + base.substr(7) + "/ws";
  }
  if (base.rfind("wss://", 0) == 0 || base.rfind("ws://", 0) == 0) {
    return base + "/ws";
  }
  return "ws://" + base + "/ws";
}

// Minimal JSON string escape.
std::string JsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::string ExtractJsonString(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  auto pos = json.find(needle);
  if (pos == std::string::npos) {
    return {};
  }
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return {};
  }
  pos = json.find('"', pos + 1);
  if (pos == std::string::npos) {
    return {};
  }
  size_t start = pos + 1;
  std::string out;
  for (size_t i = start; i < json.size(); ++i) {
    if (json[i] == '\\' && i + 1 < json.size()) {
      switch (json[i + 1]) {
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        default:
          // Unknown escape — keep the escaped char.
          out += json[i + 1];
          break;
      }
      ++i;
      continue;
    }
    if (json[i] == '"') {
      break;
    }
    out += json[i];
  }
  return out;
}

}  // namespace

XNetSignalling::XNetSignalling() = default;

XNetSignalling::~XNetSignalling() { Stop(); }

void XNetSignalling::SetHandlers(OfferHandler on_offer, AnswerHandler on_answer,
                                 CandidateHandler on_candidate,
                                 GatheringDoneHandler on_gathering_done) {
  on_offer_ = std::move(on_offer);
  on_answer_ = std::move(on_answer);
  on_candidate_ = std::move(on_candidate);
  on_gathering_done_ = std::move(on_gathering_done);
}

void XNetSignalling::Start(const std::string& api_base,
                           const std::string& local_peer_id) {
  Stop();
  api_base_ = api_base;
  local_peer_id_ = local_peer_id;
  running_ = true;
  thread_ = std::thread(&XNetSignalling::ThreadMain, this);
}

void XNetSignalling::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard lock(mutex_);
  DisconnectLocked();
  while (!outbound_.empty()) {
    outbound_.pop();
  }
}

const char* XNetSignalling::PeerTypeString(XNetPeerType type) {
  return type == XNetPeerType::kQos ? "qos" : "title";
}

XNetPeerType XNetSignalling::PeerTypeFromString(const std::string& s) {
  return s == "qos" ? XNetPeerType::kQos : XNetPeerType::kTitle;
}

bool XNetSignalling::SendOffer(const std::string& target_peer_id,
                               XNetPeerType type, const std::string& sdp) {
  std::string json =
      std::string("{\"type\":\"offer\",\"target_peer_id\":\"") +
      JsonEscape(target_peer_id) + "\",\"target_peer_type\":\"" +
      PeerTypeString(type) + "\",\"local_peer_id\":\"" +
      JsonEscape(local_peer_id_) + "\",\"sdp\":\"" + JsonEscape(sdp) + "\"}";
  XELOGI("XNetSignalling queue offer -> {} ({}) sdp_len={}", target_peer_id,
         PeerTypeString(type), sdp.size());
  std::lock_guard lock(mutex_);
  outbound_.push(std::move(json));
  return true;
}

bool XNetSignalling::SendAnswer(const std::string& target_peer_id,
                                XNetPeerType type, const std::string& sdp) {
  std::string json =
      std::string("{\"type\":\"answer\",\"target_peer_id\":\"") +
      JsonEscape(target_peer_id) + "\",\"target_peer_type\":\"" +
      PeerTypeString(type) + "\",\"sdp\":\"" + JsonEscape(sdp) + "\"}";
  XELOGI("XNetSignalling queue answer -> {} ({}) sdp_len={}", target_peer_id,
         PeerTypeString(type), sdp.size());
  std::lock_guard lock(mutex_);
  outbound_.push(std::move(json));
  return true;
}

bool XNetSignalling::SendCandidate(const std::string& target_peer_id,
                                   XNetPeerType type,
                                   const std::string& candidate,
                                   const std::string& mid) {
  std::string json =
      std::string("{\"type\":\"ice_candidate\",\"target_peer_id\":\"") +
      JsonEscape(target_peer_id) + "\",\"target_peer_type\":\"" +
      PeerTypeString(type) + "\",\"candidate\":\"" + JsonEscape(candidate) +
      "\",\"mid\":\"" + JsonEscape(mid) + "\"}";
  XELOGI("XNetSignalling queue candidate -> {} ({}) mid={} cand={}",
         target_peer_id, PeerTypeString(type), mid, candidate);
  std::lock_guard lock(mutex_);
  outbound_.push(std::move(json));
  return true;
}

bool XNetSignalling::SendGatheringComplete(const std::string& target_peer_id,
                                           XNetPeerType type) {
  std::string json =
      std::string(
          "{\"type\":\"ice_gathering_complete\",\"target_peer_id\":\"") +
      JsonEscape(target_peer_id) + "\",\"target_peer_type\":\"" +
      PeerTypeString(type) + "\"}";
  XELOGI("XNetSignalling queue gathering_complete -> {} ({})", target_peer_id,
         PeerTypeString(type));
  std::lock_guard lock(mutex_);
  outbound_.push(std::move(json));
  return true;
}

bool XNetSignalling::ConnectLocked() {
  DisconnectLocked();
  CURL* curl = curl_easy_init();
  if (!curl) {
    return false;
  }
  const std::string url = BuildWsUrl(api_base_);
  XELOGI("XNetSignalling connecting {}", url);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    XELOGW("XNet signalling connect failed: {} ({})", curl_easy_strerror(rc),
           url);
    curl_easy_cleanup(curl);
    return false;
  }
  curl_ = curl;
  connected_ = true;
  XELOGI("XNetSignalling connected, registering as {}", local_peer_id_);
  std::string reg = std::string("{\"type\":\"register\",\"peer_id\":\"") +
                    JsonEscape(local_peer_id_) + "\"}";
  return SendRawLocked(reg);
}

void XNetSignalling::DisconnectLocked() {
  connected_ = false;
  if (curl_) {
    curl_easy_cleanup(static_cast<CURL*>(curl_));
    curl_ = nullptr;
  }
}

bool XNetSignalling::SendRawLocked(const std::string& json) {
  if (!curl_ || !connected_) {
    return false;
  }
  size_t sent = 0;
  CURLcode rc =
      curl_ws_send(static_cast<CURL*>(curl_), json.data(), json.size(), &sent,
                   0, CURLWS_TEXT);
  return rc == CURLE_OK;
}

void XNetSignalling::HandleMessage(const std::string& json) {
  const std::string type = ExtractJsonString(json, "type");
  if (type == "registered") {
    XELOGI("XNet signalling registered as {}", local_peer_id_);
    return;
  }
  if (!type.empty()) {
    XELOGI("XNetSignalling recv type={} from={} bytes={}", type,
           ExtractJsonString(json, "from_peer_id"), json.size());
  }
  if (type == "offer" && on_offer_) {
    XNetSignallingOffer msg;
    msg.from_peer_id = ExtractJsonString(json, "from_peer_id");
    msg.from_peer_type =
        PeerTypeFromString(ExtractJsonString(json, "from_peer_type"));
    msg.sdp = ExtractJsonString(json, "sdp");
    on_offer_(msg);
    return;
  }
  if (type == "answer" && on_answer_) {
    XNetSignallingAnswer msg;
    msg.from_peer_id = ExtractJsonString(json, "from_peer_id");
    msg.from_peer_type =
        PeerTypeFromString(ExtractJsonString(json, "from_peer_type"));
    msg.sdp = ExtractJsonString(json, "sdp");
    on_answer_(msg);
    return;
  }
  if (type == "ice_candidate" && on_candidate_) {
    XNetSignallingCandidate msg;
    msg.from_peer_id = ExtractJsonString(json, "from_peer_id");
    msg.from_peer_type =
        PeerTypeFromString(ExtractJsonString(json, "from_peer_type"));
    msg.candidate = ExtractJsonString(json, "candidate");
    msg.mid = ExtractJsonString(json, "mid");
    on_candidate_(msg);
    return;
  }
  if (type == "ice_gathering_complete" && on_gathering_done_) {
    XNetSignallingGatheringDone msg;
    msg.from_peer_id = ExtractJsonString(json, "from_peer_id");
    msg.from_peer_type =
        PeerTypeFromString(ExtractJsonString(json, "from_peer_type"));
    on_gathering_done_(msg);
    return;
  }
}

void XNetSignalling::ThreadMain() {
  using namespace std::chrono_literals;
  while (running_) {
    {
      std::lock_guard lock(mutex_);
      if (!connected_) {
        if (!ConnectLocked()) {
          // retry below
        }
      }
      while (connected_ && !outbound_.empty()) {
        const std::string msg = outbound_.front();
        if (!SendRawLocked(msg)) {
          DisconnectLocked();
          break;
        }
        outbound_.pop();
      }
    }

    if (connected_ && curl_) {
      char buffer[65536];
      size_t recv_len = 0;
      const struct curl_ws_frame* meta = nullptr;
      CURLcode rc =
          curl_ws_recv(static_cast<CURL*>(curl_), buffer, sizeof(buffer),
                       &recv_len, &meta);
      if (rc == CURLE_OK && recv_len > 0) {
        HandleMessage(std::string(buffer, recv_len));
      } else if (rc != CURLE_AGAIN && rc != CURLE_OK) {
        std::lock_guard lock(mutex_);
        DisconnectLocked();
      }
    }

    XNetStream::Instance().Tick();
    XNetQos::Instance().Tick();

    std::this_thread::sleep_for(connected_ ? 5ms : 1000ms);
  }

  std::lock_guard lock(mutex_);
  DisconnectLocked();
}

}  // namespace kernel
}  // namespace xe
