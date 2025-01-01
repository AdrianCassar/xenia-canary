/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "src/xenia/kernel/hinternet.h"

#include <cstring>

#include <third_party/libcurl/include/curl/curl.h>
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {

HINTERNET::HINTERNET(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

HINTERNET* HINTERNET::CreateSessionHandle(std::string user_agent) {
  user_agent_ = user_agent;
  return this;
}

HINTERNET* HINTERNET::CreateConnectionHandle(std::string server_name,
                                             uint32_t port) {
  if (port == 0) {
    port = 80;
  }
  if (server_name.empty()) {
    server_name = "http://127.0.0.1/";
  }
  size_t pos = 0;
  int count = 0;

  // Loop to find the 3rd '/' occurrence
  while (count < 3 && pos != std::string::npos) {
    pos = server_name.find('/', pos + 1);
    count++;
  }
  std::string hostname = fmt::format(
      "{}:{}",
      (pos != std::string::npos) ? server_name.substr(0, pos) : server_name,
      port);

  this->server_name_ = server_name;
  this->port_ = port;
  this->url_ = hostname;

  return this;
}
HINTERNET* HINTERNET::CreateRequestHandle(std::string path,
                                          std::string method) {
  this->path_ = path;
  this->url_ = this->url_.append(path);
  this->method_ = method;
  return this;
}
void HINTERNET::SendRequest(std::string Header, std::string buffer) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    XELOGI("HINTERNET: failed to init curl!");
    this->setLastError(1);
    return;
  }
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, Header.c_str());
  curl_easy_setopt(curl, CURLOPT_URL, this->url().c_str());
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, this->user_agent().c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (this->method() == "GET") {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else if (this->method() == "POST") {
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buffer.size());
  } else if (this->method() == "PUT") {
    curl_easy_setopt(curl, CURLOPT_PUT, 5L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)buffer.size());

    // The buffer should be sent via hexadecimal
    // but because we use sockets it 'should not' be a problem.
    std::string resultBody;
    curl_easy_setopt(curl, CURLOPT_READDATA, &resultBody);
    curl_easy_setopt(
        curl, CURLOPT_READFUNCTION,
        static_cast<size_t(__stdcall*)(char*, size_t, size_t, void*)>(
            [](char* ptr, size_t size, size_t nmemb, void* resultBody) {
              *(static_cast<std::string*>(resultBody)) +=
                  std::string{ptr, size * nmemb};
              return size * nmemb;
            }));
  } else if (this->method() == "DELETE") {
    // The 3rd party server should handle the rest.
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (result == CURLE_OK || result == CURLE_GOT_NOTHING) {
    this->setLastError(0);
    return;
  }
  this->setLastError(12029);
  return;
}
void HINTERNET::Connect() {
  if (this->getLastError() != 0) {
    return;
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    XELOGI("HINTERNET: failed to init curl!");
    this->setLastError(1);
    return;
  }
  size_t pos = 0;
  int count = 0;

  // Loop to find the 3rd '/' occurrence
  while (count < 3 && pos != std::string::npos) {
    pos = this->url().find('/', pos + 1);
    count++;
  }

  std::string hostname =
      (pos != std::string::npos) ? this->url().substr(0, pos) : this->url();

  curl_easy_setopt(curl, CURLOPT_URL, hostname.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, this->user_agent().c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 3L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  auto run = [this, curl]() -> void {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (result == CURLE_OK || result == CURLE_GOT_NOTHING) {
      this->setLastError(0);
      return;
    }
    this->setLastError(12029);
    return;
  };
  std::thread thread(run);
  thread.detach();
  return;
}

}  // namespace kernel
}  // namespace xe