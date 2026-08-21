/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

// clang-format off
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

#include "xenia/base/logging.h"
#include "xenia/base/utf8.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/net_utils.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xhttp.h"
#include "xenia/kernel/xnet.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_bool(logging);

namespace xe {
namespace kernel {

XHttp::XHttp(KernelState* kernel_state, Kind kind)
    : XObject(kernel_state, kObjectType), kind_(kind) {}

namespace {

// https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
size_t CurlWriteCallback(void* data, size_t size, size_t nmemb, void* clientp) {
  const size_t realsize = size * nmemb;
  auto* mem = static_cast<response_data*>(clientp);

  char* ptr =
      static_cast<char*>(realloc(mem->response, mem->size + realsize + 1));
  if (!ptr) {
    return 0;
  }

  mem->response = ptr;
  std::memcpy(&mem->response[mem->size], data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

// The backend maps hostnames via title/<id>/hosts; anything unlisted is left
// alone.
std::string ResolveRedirectHost(const std::string& host) {
  const std::string redirect =
      kernel_state()->GetXboxLiveAPI()->GetHostRedirect(host);

  return redirect.empty() ? host : redirect;
}

// void callback(hInternet, dwContext, dwInternetStatus, lpvStatusInformation,
//               dwStatusInformationLength)
// Guest threads only.
void InvokeGuestCallback(uint32_t guest_callback, uint32_t handle,
                         uint32_t context, uint32_t status, uint32_t info_ptr,
                         uint32_t info_len) {
  if (!guest_callback) {
    return;
  }

  auto* thread = XThread::GetCurrentThread();
  if (!thread) {
    return;
  }

  uint64_t args[] = {handle, context, status, info_ptr, info_len};
  kernel_state()->processor()->Execute(thread->thread_state(), guest_callback,
                                       args, xe::countof(args));
}

// Throwaway guest thread, so `work` can block on the network without holding
// up the caller.
void RunXHttpWorker(std::function<void()> work) {
  auto thread = object_ref<XThread>(new XHostThread(
      kernel_state(), 128 * 1024, 0,
      [work = std::move(work)]() -> int {
        work();
        return 0;
      },
      kernel_state()->GetSystemProcess()));
  thread->set_name("XHTTP Async Worker");
  thread->Create();
}

std::mutex g_xhttp_pump_mutex;
std::condition_variable g_xhttp_pump_cv;
std::vector<XHttpCompletion> g_xhttp_pump_queue;

uint32_t ResolveSessionHandle(const object_ref<XHttp>& obj) {
  if (!obj) {
    return 0;
  }
  switch (obj->kind()) {
    case XHttp::Kind::kSession:
      return obj->handle();
    case XHttp::Kind::kConnection:
      return obj->session_handle;
    case XHttp::Kind::kRequest: {
      const auto connection =
          kernel_state()->object_table()->LookupObject<XHttp>(
              obj->connection_handle);
      return connection ? connection->session_handle : 0;
    }
  }
  return 0;
}

void ExecuteCompletion(const XHttpCompletion& c) {
  if (c.alloc_error) {
    const uint32_t result_ptr =
        kernel_state()->memory()->SystemHeapAlloc(2 * sizeof(uint32_t));
    if (result_ptr) {
      auto* async_result =
          kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
              result_ptr);
      async_result[0] = c.error_api;
      async_result[1] = c.error_code;
    }
    InvokeGuestCallback(c.callback, c.handle, c.context, c.status, result_ptr,
                        2 * sizeof(uint32_t));
    if (result_ptr) {
      kernel_state()->memory()->SystemHeapFree(result_ptr);
    }
    return;
  }

  if (c.alloc_write_count) {
    const uint32_t count_ptr =
        kernel_state()->memory()->SystemHeapAlloc(sizeof(uint32_t));
    if (count_ptr) {
      *kernel_state()->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          count_ptr) = c.write_count;
    }
    InvokeGuestCallback(c.callback, c.handle, c.context, c.status, count_ptr,
                        sizeof(uint32_t));
    if (count_ptr) {
      kernel_state()->memory()->SystemHeapFree(count_ptr);
    }
    return;
  }

  InvokeGuestCallback(c.callback, c.handle, c.context, c.status, c.info_ptr,
                      c.info_len);
}

}  // namespace

void XHttp::Perform() {
  std::lock_guard<std::mutex> perform_lock(perform_mutex);
  if (this->performed) {
    return;
  }

  const auto connection = kernel_state()->object_table()->LookupObject<XHttp>(
      this->connection_handle);
  const std::string host = connection ? connection->host : std::string();
  const uint16_t host_port = connection ? connection->port : 0;

  std::string path = this->path;
  if (path.empty() || path.front() != '/') {
    path = "/" + path;
  }

  // Only the hostname is rewritten; the port the title asked for is kept.
  const std::string target = ResolveRedirectHost(host);
  const std::string url =
      host_port ? fmt::format("http://{}:{}{}", target, host_port, path)
                : fmt::format("http://{}{}", target, path);

  CURL* curl_handle = curl_easy_init();
  if (!curl_handle) {
    XELOGE("XHttp: Cannot initialize CURL");
    XThread::SetLastError(XHTTP_ERROR_INTERNAL_ERROR);
    return;
  }

  response_data body_chunk = {};
  response_data header_chunk = {};

  const std::string verb = this->verb.empty() ? "GET" : this->verb;

  curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, verb.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "xenia");

  curl_slist* headers = nullptr;
  if (!host.empty()) {
    const std::string host_header =
        host_port ? fmt::format("Host: {}:{}", host, host_port)
                  : fmt::format("Host: {}", host);
    headers = curl_slist_append(headers, host_header.c_str());
  }
  for (const auto& header : this->request_headers) {
    headers = curl_slist_append(headers, header.c_str());
  }
  if (headers) {
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  }

  if (!this->request_body.empty()) {
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS,
                     this->request_body.data());
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(this->request_body.size()));
  }

  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &body_chunk);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, &header_chunk);

  if (cvars::logging) {
    XELOGI("XHttp: {} {} (host: {})", verb, url, host);
  }

  const CURLcode result = curl_easy_perform(curl_handle);
  if (result == CURLE_OK) {
    this->succeeded = true;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &this->status_code);
    XELOGI("XHttp: {} {} -> status {} ({} body bytes)", verb, path,
           this->status_code, body_chunk.response ? body_chunk.size : 0);

    if (body_chunk.response) {
      this->response_body.assign(body_chunk.response, body_chunk.size);
    }
    if (header_chunk.response) {
      this->response_headers.assign(header_chunk.response, header_chunk.size);
    }
  } else {
    XELOGE("XHttp: request failed, CURL error {}",
           static_cast<uint32_t>(result));
    XThread::SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
  }

  if (body_chunk.response) {
    free(body_chunk.response);
  }
  if (header_chunk.response) {
    free(header_chunk.response);
  }
  if (headers) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl_handle);

  this->performed = true;
}

uint32_t XHttp::ResolveStatusCallback() const {
  if (status_callback) {
    return status_callback;
  }

  const auto connection =
      kernel_state()->object_table()->LookupObject<XHttp>(connection_handle);

  if (connection) {
    if (connection->status_callback) {
      return connection->status_callback;
    }

    const auto session = kernel_state()->object_table()->LookupObject<XHttp>(
        connection->session_handle);

    if (session && session->status_callback) {
      return session->status_callback;
    }
  }
  return 0;
}

std::vector<std::string> XHttpSplitHeaders(std::string request_headers) {
  std::regex newlines(R"([\r\n]+)");

  std::sregex_token_iterator it(request_headers.cbegin(),
                                request_headers.cend(), newlines, -1);
  std::sregex_token_iterator end;
  return std::vector<std::string>(it, end);
}

bool XHttpFindHeaderValue(const std::string& raw_headers, const char* name,
                          std::string* out_value) {
  if (!name) {
    return false;
  }

  for (const auto& line : XHttpSplitHeaders(raw_headers)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    if (xe::utf8::equal_case(line.substr(0, colon).c_str(), name)) {
      size_t value_start = colon + 1;
      while (value_start < line.size() && line[value_start] == ' ') {
        ++value_start;
      }
      *out_value = line.substr(value_start);
      return true;
    }
  }
  return false;
}

const std::regex& XHttpUrlRegex() {
  static const std::regex regex(
      R"(^([a-zA-Z]+)://(?:([^:@]+)(?::([^:@]*))?@)?([^/:]+)(?::(\d+))?((/[^?#]*)(\?[^#]*)?(#[^ ]*)?)?$)",
      std::regex_constants::icase);
  return regex;
}

std::string XHttpUnescapeUrl(const std::string& escaped) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return "";
  }

  std::string unescaped;
  int length = 0;
  char* output = curl_easy_unescape(curl, escaped.c_str(),
                                    static_cast<int>(escaped.size()), &length);
  if (output) {
    unescaped = std::string(output, length);
    curl_free(output);
  }

  curl_easy_cleanup(curl);

  return unescaped;
}

void XHttpDeliverCompletion(XHttpCompletion completion) {
  if (!completion.session_handle && completion.handle) {
    const auto obj = kernel_state()->object_table()->LookupObject<XHttp>(
        completion.handle);
    completion.session_handle = ResolveSessionHandle(obj);
  }
  {
    std::lock_guard<std::mutex> lock(g_xhttp_pump_mutex);
    g_xhttp_pump_queue.push_back(std::move(completion));
  }
  g_xhttp_pump_cv.notify_all();
}

void XHttpDeliverReceiveResponse(const object_ref<XHttp>& request,
                                 uint32_t handle, uint32_t context,
                                 uint32_t callback) {
  RunXHttpWorker([request, handle, context, callback]() {
    request->Perform();

    XHttpCompletion completion = {};
    completion.handle = handle;
    completion.context = context;
    completion.callback = callback;

    if (request->succeeded) {
      completion.status = XHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE;
    } else {
      completion.status = XHTTP_CALLBACK_STATUS_REQUEST_ERROR;
      completion.alloc_error = true;
      completion.error_api = XHTTP_API_RECEIVE_RESPONSE;
      completion.error_code = XHTTP_ERROR_CONNECTION_ERROR;
    }

    XHttpDeliverCompletion(std::move(completion));
  });
}

uint32_t XHttpDoWork(uint32_t h_session, uint32_t wait_ms) {
  if (h_session) {
    const auto session =
        kernel_state()->object_table()->LookupObject<XHttp>(h_session);
    if (!session || session->kind() != XHttp::Kind::kSession) {
      XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
      return 1;
    }
  }

  auto matches_session = [h_session](const XHttpCompletion& completion) {
    return !h_session || completion.session_handle == h_session ||
           completion.handle == h_session;
  };

  std::vector<XHttpCompletion> pending;
  {
    std::unique_lock<std::mutex> lock(g_xhttp_pump_mutex);

    auto has_matching = [&]() {
      return std::any_of(g_xhttp_pump_queue.begin(), g_xhttp_pump_queue.end(),
                         matches_session);
    };

    if (!has_matching() && wait_ms != 0) {
      if (wait_ms == 0xFFFFFFFFu) {
        g_xhttp_pump_cv.wait(lock, has_matching);
      } else {
        g_xhttp_pump_cv.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                 has_matching);
      }
    }

    for (auto it = g_xhttp_pump_queue.begin();
         it != g_xhttp_pump_queue.end();) {
      if (matches_session(*it)) {
        pending.push_back(std::move(*it));
        it = g_xhttp_pump_queue.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (const auto& completion : pending) {
    ExecuteCompletion(completion);
  }

  return static_cast<uint32_t>(ERROR_SUCCESS);
}

}  // namespace kernel
}  // namespace xe
