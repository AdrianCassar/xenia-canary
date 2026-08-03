/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

// clang-format off
// We want to include platform.h first to define NOMINMAX to prevent window.h
// from defining the macros.
#include "xenia/base/platform.h"
#include "third_party/libcurl/include/curl/curl.h"
// clang-format on

#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/string_util.h"
#include "xenia/base/utf8.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xnet.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_bool(xhttp);

DECLARE_bool(logging);

namespace xe {
namespace kernel {
namespace xam {

// XHTTP is xam's HTTP client. Requests run through libcurl.
namespace {

// Query info levels: attribute in the low 16 bits, flags above. xam does not
// use WinHTTP's attribute numbering, so these are only the two we have seen
// titles ask for; everything else falls through to a header lookup by name.
constexpr uint32_t XHTTP_QUERY_STATUS_CODE = 0xFFFE;
constexpr uint32_t XHTTP_QUERY_CONTENT_LENGTH = 9;

constexpr uint32_t XHTTP_QUERY_ATTRIBUTE_MASK = 0x0000FFFF;
constexpr uint32_t XHTTP_QUERY_FLAG_NUMBER = 0x20000000;

constexpr uint32_t XHTTP_FLAG_ASYNC = 0x10000000;

// WinHTTP-style status callback notifications (dwInternetStatus).
constexpr uint32_t XHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE = 0x00020000;
constexpr uint32_t XHTTP_CALLBACK_STATUS_DATA_AVAILABLE = 0x00040000;
constexpr uint32_t XHTTP_CALLBACK_STATUS_READ_COMPLETE = 0x00080000;
constexpr uint32_t XHTTP_CALLBACK_STATUS_WRITE_COMPLETE = 0x00100000;
constexpr uint32_t XHTTP_CALLBACK_STATUS_REQUEST_ERROR = 0x00200000;
constexpr uint32_t XHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE = 0x00400000;

// XHTTP_ASYNC_RESULT.dwResult - which async API failed.
constexpr uint32_t XHTTP_API_RECEIVE_RESPONSE = 1;
constexpr uint32_t XHTTP_API_READ_DATA = 3;
constexpr uint32_t XHTTP_API_WRITE_DATA = 4;
constexpr uint32_t XHTTP_API_SEND_REQUEST = 5;

enum class XHttpHandleType { kSession, kConnection, kRequest };

struct XHttpHandle {
  XHttpHandleType type;

  // Inherited session -> connection -> request.
  bool async = false;

  // Session (XHttpOpen).
  std::string user_agent;

  // Connection (XHttpConnect).
  uint32_t session_handle = 0;
  std::string host;
  uint16_t port = 0;

  // Request (XHttpOpenRequest).
  uint32_t connection_handle = 0;
  std::string verb;
  std::string path;
  std::vector<std::string> request_headers;
  std::string request_body;
  uint32_t context = 0;  // dwContext passed to the status callback.

  // Status callback (XHttpSetStatusCallback).
  uint32_t status_callback = 0;

  // Response, valid once performed. perform_mutex holds off a querier on
  // another thread so it can't see a half-populated (status_code == 0) state.
  std::mutex perform_mutex;
  bool performed = false;
  bool succeeded = false;
  uint64_t status_code = 0;
  std::string response_headers;
  std::string response_body;
  size_t read_offset = 0;
};

class XHttpManager {
 public:
  uint32_t Create(XHttpHandleType type) {
    auto handle_obj = std::make_shared<XHttpHandle>();
    handle_obj->type = type;

    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t handle = next_handle_++;
    handles_.emplace(handle, std::move(handle_obj));
    return handle;
  }

  std::shared_ptr<XHttpHandle> Lookup(uint32_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : it->second;
  }

  bool Close(uint32_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return handles_.erase(handle) > 0;
  }

 private:
  std::mutex mutex_;
  // Well above small integers so we don't collide with guest sentinels.
  uint32_t next_handle_ = 0x50000000;
  std::unordered_map<uint32_t, std::shared_ptr<XHttpHandle>> handles_;
};

XHttpManager xhttp_manager;

// https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
size_t XHttpWriteCallback(void* data, size_t size, size_t nmemb,
                          void* clientp) {
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

std::vector<std::string> SplitHeaderLines(const std::string& headers) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < headers.size()) {
    size_t end = headers.find("\r\n", start);
    if (end == std::string::npos) {
      end = headers.size();
    }
    if (end > start) {
      lines.emplace_back(headers.substr(start, end - start));
    }
    start = end + 2;
  }
  return lines;
}

// Case-insensitive, as header names are.
bool FindHeaderValue(const std::string& raw_headers, const std::string& name,
                     std::string* out_value) {
  for (const auto& line : SplitHeaderLines(raw_headers)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    if (xe::utf8::equal_case(line.substr(0, colon).c_str(), name.c_str())) {
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

// scheme://[user[:password]@]host[:port][/path][?query][#fragment], shared by
// the ANSI and wide XHttpCrackUrl. Compiling a regex is slow, so keep it.
const std::regex& UrlRegex() {
  static const std::regex regex(
      R"(^([a-zA-Z]+)://(?:([^:@]+)(?::([^:@]*))?@)?([^/:]+)(?::(\d+))?((/[^?#]*)(\?[^#]*)?(#[^ ]*)?)?$)",
      std::regex_constants::icase);
  return regex;
}

std::string UnescapeUrl(const std::string& escaped) {
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

// The backend maps hostnames via title/<id>/hosts; anything unlisted is left
// alone.
std::string ResolveRedirectHost(const std::string& host) {
  const std::string redirect =
      kernel_state()->GetXboxLiveAPI()->GetHostRedirect(host);

  return redirect.empty() ? host : redirect;
}

// Runs the transaction once; later callers wait here and then return.
void PerformXHttpRequest(const std::shared_ptr<XHttpHandle>& request) {
  std::lock_guard<std::mutex> perform_lock(request->perform_mutex);
  if (request->performed) {
    return;
  }

  const auto connection = xhttp_manager.Lookup(request->connection_handle);
  const std::string host = connection ? connection->host : std::string();
  const uint16_t host_port = connection ? connection->port : 0;

  std::string path = request->path;
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

  const std::string verb = request->verb.empty() ? "GET" : request->verb;

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
  for (const auto& header : request->request_headers) {
    headers = curl_slist_append(headers, header.c_str());
  }
  if (headers) {
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  }

  if (!request->request_body.empty()) {
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS,
                     request->request_body.data());
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(request->request_body.size()));
  }

  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, XHttpWriteCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &body_chunk);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, XHttpWriteCallback);
  curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, &header_chunk);

  if (cvars::logging) {
    XELOGI("XHttp: {} {} (host: {})", verb, url, host);
  }

  const CURLcode result = curl_easy_perform(curl_handle);
  if (result == CURLE_OK) {
    request->succeeded = true;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
                      &request->status_code);
    XELOGI("XHttp: {} {} -> status {} ({} body bytes)", verb, path,
           request->status_code, body_chunk.response ? body_chunk.size : 0);

    if (body_chunk.response) {
      request->response_body.assign(body_chunk.response, body_chunk.size);
    }
    if (header_chunk.response) {
      request->response_headers.assign(header_chunk.response,
                                       header_chunk.size);
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

  request->performed = true;
}

// WinHTTP inherits the callback down the handle chain.
uint32_t ResolveStatusCallback(const std::shared_ptr<XHttpHandle>& request) {
  if (request->status_callback) {
    return request->status_callback;
  }
  const auto connection = xhttp_manager.Lookup(request->connection_handle);
  if (connection) {
    if (connection->status_callback) {
      return connection->status_callback;
    }
    const auto session = xhttp_manager.Lookup(connection->session_handle);
    if (session && session->status_callback) {
      return session->status_callback;
    }
  }
  return 0;
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
  kernel_state()->processor()->Execute(
      thread->thread_state(), guest_callback & ~1u, args, xe::countof(args));
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

// Queued notification, drained on the title's thread by XHttpDoWork.
struct XHttpCompletion {
  uint32_t handle = 0;    // hInternet (request handle)
  uint32_t context = 0;   // dwContext
  uint32_t callback = 0;  // guest status callback
  uint32_t status = 0;    // XHTTP_CALLBACK_STATUS_*
  uint32_t info_ptr = 0;  // lpvStatusInformation (guest ptr) or 0
  uint32_t info_len = 0;  // dwStatusInformationLength

  // REQUEST_ERROR: pass an XHTTP_ASYNC_RESULT {api, error}.
  bool alloc_error = false;
  uint32_t error_api = 0;
  uint32_t error_code = 0;

  // WRITE_COMPLETE: pass a DWORD holding write_count.
  bool alloc_write_count = false;
  uint32_t write_count = 0;
};

std::mutex g_xhttp_pump_mutex;
std::vector<XHttpCompletion> g_xhttp_pump_queue;

// The status-information buffer only has to live for the callback.
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

void DeliverCompletion(XHttpCompletion completion) {
  std::lock_guard<std::mutex> lock(g_xhttp_pump_mutex);
  g_xhttp_pump_queue.push_back(std::move(completion));
}

void DeliverReceiveResponse(const std::shared_ptr<XHttpHandle>& request,
                            uint32_t handle, uint32_t context,
                            uint32_t callback) {
  RunXHttpWorker([request, handle, context, callback]() {
    PerformXHttpRequest(request);

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

    DeliverCompletion(std::move(completion));
  });
}

}  // namespace

dword_result_t NetDll_XHttpStartup_entry(dword_t caller, dword_t reserved,
                                         dword_t reserved_ptr) {
  // Console returns 1 even without network access

  if (kernel_state()->emulator()->title_id() == kDashboardID ||
      kernel_state()->emulator()->title_id() == kAvatarEditorID) {
    return 1;
  }

  // We're suppose to set error code if we fail function
  // XThread::SetLastError(XHTTP_ERROR_CONNECTION_ERROR);
  return cvars::xhttp;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpStartup, kNetworking, kStub);

void NetDll_XHttpShutdown_entry(dword_t caller) {}
DECLARE_XAM_EXPORT1(NetDll_XHttpShutdown, kNetworking, kStub);

dword_result_t NetDll_XHttpOpen_entry(dword_t caller, lpstring_t user_agent,
                                      dword_t access_type,
                                      lpstring_t proxy_name,
                                      lpstring_t proxy_bypass, dword_t flags) {
  const uint32_t handle = xhttp_manager.Create(XHttpHandleType::kSession);

  auto session = xhttp_manager.Lookup(handle);
  session->async = (flags & XHTTP_FLAG_ASYNC) != 0;
  if (user_agent) {
    session->user_agent = user_agent.value();
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return handle;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpOpen, kNetworking, kImplemented);

dword_result_t NetDll_XHttpCloseHandle_entry(dword_t caller, dword_t handle) {
  if (!xhttp_manager.Close(handle)) {
    XThread::SetLastError(X_ERROR_INVALID_HANDLE);
    return 0;
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpCloseHandle, kNetworking, kImplemented);

dword_result_t NetDll_XHttpCrackUrl_entry(
    dword_t caller, lpstring_t url_ptr, dword_t url_length, dword_t flags,
    pointer_t<XHTTP_URL_COMPONENTS> url_components_ptr) {
  if (!url_ptr || !url_components_ptr ||
      url_components_ptr->struct_size != sizeof(XHTTP_URL_COMPONENTS)) {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    return false;
  }

  // X_ICU_ESCAPE is unsupported ignore it.

  bool insufficient_buffer =
      url_components_ptr->scheme_ptr && !url_components_ptr->scheme_length ||
      url_components_ptr->host_name_ptr &&
          !url_components_ptr->host_name_length ||
      url_components_ptr->user_name_ptr &&
          !url_components_ptr->user_name_length ||
      url_components_ptr->password_ptr &&
          !url_components_ptr->password_length ||
      url_components_ptr->url_path_ptr &&
          !url_components_ptr->url_path_length ||
      url_components_ptr->extra_info_ptr &&
          !url_components_ptr->extra_info_length;

  std::string url_to_process = url_ptr.value();

  if (url_length) {
    url_to_process = url_ptr.value().substr(0, url_length);
  }

  CURLU* url = curl_url();

  if (url) {
    CURLUcode rc = curl_url_set(url, CURLUPART_URL, url_to_process.c_str(), 0);

    // Assert if URL is bad format
    assert_zero(rc);

    if (rc) {
      url_components_ptr->scheme = -1;
    }

    curl_url_cleanup(url);
  }

  std::smatch matches;

  auto ProcessComponent = [flags, kernel_state = kernel_state()](
                              const uint32_t component_result_ptr,
                              uint32_t& component_ptr,
                              uint32_t& component_length_ptr, uint32_t size) {
    if (component_ptr) {
      // Include null terminator
      const uint32_t min_buffer_size = size + 1;

      if (!component_length_ptr || component_length_ptr < min_buffer_size) {
        component_length_ptr = min_buffer_size;
        return false;
      }

      char* result_dst_ptr =
          kernel_state->memory()->TranslateVirtual<char*>(component_ptr);

      char* result_src_ptr =
          kernel_state->memory()->TranslateVirtual<char*>(component_result_ptr);

      const std::string component_data(result_src_ptr, size);
      const std::string processed_data =
          flags & X_ICU_DECODE ? UnescapeUrl(component_data) : component_data;

      xe::string_util::copy_truncating(result_dst_ptr, processed_data.c_str(),
                                       component_length_ptr);
      component_length_ptr = static_cast<uint32_t>(processed_data.size());
    } else if (component_length_ptr) {
      component_ptr = component_result_ptr;
      component_length_ptr = size;
    }

    return true;
  };

  bool result = true;

  if (std::regex_match(url_to_process, matches, UrlRegex())) {
    for (size_t i = 0; i < matches.size(); ++i) {
      std::ssub_match sub_match = matches[i];

      if (sub_match.matched) {
        const uint32_t result_ptr = url_ptr.guest_address() +
                                    static_cast<uint32_t>(matches.position(i));

        uint32_t length = static_cast<uint32_t>(sub_match.length());

        const X_URL_COMPONENTS current_component =
            static_cast<X_URL_COMPONENTS>(i);

        switch (current_component) {
          case X_URL_COMPONENTS::Full:
          case X_URL_COMPONENTS::Resource: {
            // Skip, these wrap components handled on their own below.
            continue;
          } break;
          case X_URL_COMPONENTS::Protocol: {
            uint32_t scheme_ptr_out = url_components_ptr->scheme_ptr;
            uint32_t scheme_length_out = url_components_ptr->scheme_length;

            const bool component_result = ProcessComponent(
                result_ptr, scheme_ptr_out, scheme_length_out, length);

            url_components_ptr->scheme_length = scheme_length_out;

            if (component_result) {
              if (!url_components_ptr->scheme_ptr) {
                url_components_ptr->scheme_ptr = scheme_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }

            const char* scheme_data_ptr =
                kernel_state()->memory()->TranslateVirtual<char*>(result_ptr);

            std::string schema_data = std::string(scheme_data_ptr, length);

            X_INTERNET_SCHEME scheme_type = {};

            // Set default scheme and port
            if (utf8::equal_case(schema_data.c_str(), "http")) {
              scheme_type = X_INTERNET_SCHEME::HTTP;
              url_components_ptr->port = 80;
            } else if (utf8::equal_case(schema_data.c_str(), "https")) {
              scheme_type = X_INTERNET_SCHEME::HTTPS;
              url_components_ptr->port = 443;
            }

            url_components_ptr->scheme = static_cast<uint32_t>(scheme_type);
          } break;
          case X_URL_COMPONENTS::Username: {
            uint32_t username_ptr_out = url_components_ptr->user_name_ptr;
            uint32_t username_length_out = url_components_ptr->user_name_length;

            const bool component_result = ProcessComponent(
                result_ptr, username_ptr_out, username_length_out, length);

            url_components_ptr->user_name_length = username_length_out;

            if (component_result) {
              if (!url_components_ptr->user_name_ptr) {
                url_components_ptr->user_name_ptr = username_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }
          } break;
          case X_URL_COMPONENTS::Password: {
            uint32_t password_ptr_out = url_components_ptr->password_ptr;
            uint32_t password_length_out = url_components_ptr->password_length;

            const bool component_result = ProcessComponent(
                result_ptr, password_ptr_out, password_length_out, length);

            url_components_ptr->password_length = password_length_out;

            if (component_result) {
              if (!url_components_ptr->password_ptr) {
                url_components_ptr->password_ptr = password_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }
          } break;
          case X_URL_COMPONENTS::Host: {
            uint32_t host_ptr_out = url_components_ptr->host_name_ptr;
            uint32_t host_length_out = url_components_ptr->host_name_length;

            const bool component_result = ProcessComponent(
                result_ptr, host_ptr_out, host_length_out, length);

            url_components_ptr->host_name_length = host_length_out;

            if (component_result) {
              if (!url_components_ptr->host_name_ptr) {
                url_components_ptr->host_name_ptr = host_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }
          } break;
          case X_URL_COMPONENTS::Port: {
            const char* port_str_ptr =
                kernel_memory()->TranslateVirtual<char*>(result_ptr);

            std::string port_str = std::string(port_str_ptr, length);

            const uint16_t port =
                xe::string_util::from_string<uint16_t>(port_str);

            url_components_ptr->port = port;
          } break;
          case X_URL_COMPONENTS::Path: {
            uint32_t path_ptr_out = url_components_ptr->url_path_ptr;
            uint32_t path_length_out = url_components_ptr->url_path_length;

            const bool component_result = ProcessComponent(
                result_ptr, path_ptr_out, path_length_out, length);

            url_components_ptr->url_path_length = path_length_out;

            if (component_result) {
              if (!url_components_ptr->url_path_ptr) {
                url_components_ptr->url_path_ptr = path_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }
          } break;
          case X_URL_COMPONENTS::Query:
          case X_URL_COMPONENTS::Fragment: {
            // Extra info is the query and the fragment together, so let the
            // query cover both and only start at the fragment without one.
            const size_t query = static_cast<size_t>(X_URL_COMPONENTS::Query);
            if (current_component == X_URL_COMPONENTS::Fragment &&
                matches[query].matched) {
              continue;
            }

            const size_t resource =
                static_cast<size_t>(X_URL_COMPONENTS::Resource);
            length = static_cast<uint32_t>(matches.position(resource) +
                                           matches.length(resource) -
                                           matches.position(i));

            uint32_t extra_ptr_out = url_components_ptr->extra_info_ptr;
            uint32_t extra_length_out = url_components_ptr->extra_info_length;

            const bool component_result = ProcessComponent(
                result_ptr, extra_ptr_out, extra_length_out, length);

            url_components_ptr->extra_info_length = extra_length_out;

            if (component_result) {
              if (!url_components_ptr->extra_info_ptr) {
                url_components_ptr->extra_info_ptr = extra_ptr_out;
              }
            } else {
              insufficient_buffer = true;
            }
          } break;
        }
      }
    }
  } else {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    result = false;
  }

  // Return after processing so the component length is set
  if (insufficient_buffer) {
    XThread::SetLastError(X_ERROR_INSUFFICIENT_BUFFER);
    result = false;
  }

  if (result && !insufficient_buffer) {
    if (!url_components_ptr->scheme_ptr) {
      url_components_ptr->scheme_length = 0;
    }
    if (!url_components_ptr->host_name_ptr) {
      url_components_ptr->host_name_length = 0;
    }
    if (!url_components_ptr->user_name_ptr) {
      url_components_ptr->user_name_length = 0;
    }
    if (!url_components_ptr->password_ptr) {
      url_components_ptr->password_length = 0;
    }
    if (!url_components_ptr->url_path_ptr) {
      url_components_ptr->url_path_length = 0;
    }
    if (!url_components_ptr->extra_info_ptr) {
      url_components_ptr->extra_info_length = 0;
    }
  }

  return result;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpCrackUrl, kNetworking, kImplemented);

dword_result_t NetDll_XHttpCrackUrlW_entry(
    dword_t caller, lpu16string_t url_ptr, dword_t url_length, dword_t flags,
    pointer_t<XHTTP_URL_COMPONENTS> url_components_ptr) {
  if (!url_ptr || !url_components_ptr ||
      url_components_ptr->struct_size != sizeof(XHTTP_URL_COMPONENTS)) {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    return false;
  }

  // X_ICU_ESCAPE is unsupported ignore it.

  bool insufficient_buffer =
      url_components_ptr->scheme_ptr && !url_components_ptr->scheme_length ||
      url_components_ptr->host_name_ptr &&
          !url_components_ptr->host_name_length ||
      url_components_ptr->user_name_ptr &&
          !url_components_ptr->user_name_length ||
      url_components_ptr->password_ptr &&
          !url_components_ptr->password_length ||
      url_components_ptr->url_path_ptr &&
          !url_components_ptr->url_path_length ||
      url_components_ptr->extra_info_ptr &&
          !url_components_ptr->extra_info_length;

  std::u16string url_to_process = url_ptr.value();

  if (url_length) {
    url_to_process = url_to_process.substr(0, url_length);
  }

  // URL syntax is ASCII, so match against a byte-per-code-unit copy: offsets
  // into it are also offsets into the UTF-16 original. Anything non-ASCII
  // becomes a filler byte so a truncated code unit can't pose as a delimiter.
  std::string narrow_url(url_to_process.size(), '\0');
  for (size_t i = 0; i < url_to_process.size(); ++i) {
    narrow_url[i] = url_to_process[i] < 0x80
                        ? static_cast<char>(url_to_process[i])
                        : '\x7F';
  }

  std::smatch matches;
  if (!std::regex_match(narrow_url, matches, UrlRegex())) {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    return false;
  }

  auto ProcessComponent = [&](const uint32_t offset, const uint32_t length,
                              xe::be<uint32_t>& component_ptr,
                              xe::be<uint32_t>& component_length_ptr) {
    if (!component_ptr) {
      // No buffer, so hand back a pointer into the caller's own string. That
      // rules out decoding, which needs somewhere to put the shorter result.
      if (component_length_ptr) {
        component_ptr = url_ptr.guest_address() + offset * sizeof(char16_t);
        component_length_ptr = length;
      }
      return;
    }

    std::u16string component_data = url_to_process.substr(offset, length);
    if (flags & X_ICU_DECODE) {
      component_data = xe::to_utf16(UnescapeUrl(xe::to_utf8(component_data)));
    }

    // Include null terminator
    const uint32_t min_buffer_size =
        static_cast<uint32_t>(component_data.size()) + 1;

    if (component_length_ptr < min_buffer_size) {
      component_length_ptr = min_buffer_size;
      insufficient_buffer = true;
      return;
    }

    xe::string_util::copy_and_swap_truncating(
        kernel_memory()->TranslateVirtual<char16_t*>(component_ptr),
        component_data, component_length_ptr);
    component_length_ptr = static_cast<uint32_t>(component_data.size());
  };

  auto ProcessMatch = [&](const X_URL_COMPONENTS component,
                          xe::be<uint32_t>& component_ptr,
                          xe::be<uint32_t>& component_length_ptr) {
    const size_t index = static_cast<size_t>(component);
    if (!matches[index].matched) {
      return;
    }

    ProcessComponent(static_cast<uint32_t>(matches.position(index)),
                     static_cast<uint32_t>(matches.length(index)),
                     component_ptr, component_length_ptr);
  };

  ProcessMatch(X_URL_COMPONENTS::Protocol, url_components_ptr->scheme_ptr,
               url_components_ptr->scheme_length);

  if (matches[static_cast<size_t>(X_URL_COMPONENTS::Protocol)].matched) {
    const std::string scheme_data =
        matches[static_cast<size_t>(X_URL_COMPONENTS::Protocol)].str();

    X_INTERNET_SCHEME scheme_type = {};

    // Set default scheme and port
    if (utf8::equal_case(scheme_data.c_str(), "http")) {
      scheme_type = X_INTERNET_SCHEME::HTTP;
      url_components_ptr->port = 80;
    } else if (utf8::equal_case(scheme_data.c_str(), "https")) {
      scheme_type = X_INTERNET_SCHEME::HTTPS;
      url_components_ptr->port = 443;
    }

    url_components_ptr->scheme = static_cast<uint32_t>(scheme_type);
  }

  ProcessMatch(X_URL_COMPONENTS::Username, url_components_ptr->user_name_ptr,
               url_components_ptr->user_name_length);
  ProcessMatch(X_URL_COMPONENTS::Password, url_components_ptr->password_ptr,
               url_components_ptr->password_length);
  ProcessMatch(X_URL_COMPONENTS::Host, url_components_ptr->host_name_ptr,
               url_components_ptr->host_name_length);
  ProcessMatch(X_URL_COMPONENTS::Path, url_components_ptr->url_path_ptr,
               url_components_ptr->url_path_length);

  // Extra info is the query and the fragment together, so start at the query
  // when present and otherwise at the fragment.
  const size_t query = static_cast<size_t>(X_URL_COMPONENTS::Query);
  const size_t fragment = static_cast<size_t>(X_URL_COMPONENTS::Fragment);
  const size_t resource = static_cast<size_t>(X_URL_COMPONENTS::Resource);
  const size_t extra_start = matches[query].matched ? query : fragment;
  if (matches[extra_start].matched) {
    const uint32_t extra_offset =
        static_cast<uint32_t>(matches.position(extra_start));
    const uint32_t extra_length = static_cast<uint32_t>(
        matches.position(resource) + matches.length(resource) -
        matches.position(extra_start));
    ProcessComponent(extra_offset, extra_length,
                     url_components_ptr->extra_info_ptr,
                     url_components_ptr->extra_info_length);
  }

  // After the scheme, so an explicit port wins over its default.
  const auto& port_match = matches[static_cast<size_t>(X_URL_COMPONENTS::Port)];
  if (port_match.matched) {
    url_components_ptr->port =
        xe::string_util::from_string<uint16_t>(port_match.str());
  }

  // Return after processing so the component length is set
  if (insufficient_buffer) {
    XThread::SetLastError(X_ERROR_INSUFFICIENT_BUFFER);
    return false;
  }

  // Same as the ANSI path: don't leave pointer-return sentinels on absent
  // components.
  if (!url_components_ptr->scheme_ptr) {
    url_components_ptr->scheme_length = 0;
  }
  if (!url_components_ptr->host_name_ptr) {
    url_components_ptr->host_name_length = 0;
  }
  if (!url_components_ptr->user_name_ptr) {
    url_components_ptr->user_name_length = 0;
  }
  if (!url_components_ptr->password_ptr) {
    url_components_ptr->password_length = 0;
  }
  if (!url_components_ptr->url_path_ptr) {
    url_components_ptr->url_path_length = 0;
  }
  if (!url_components_ptr->extra_info_ptr) {
    url_components_ptr->extra_info_length = 0;
  }

  return true;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpCrackUrlW, kNetworking, kImplemented);

dword_result_t NetDll_XHttpDoWork_entry(dword_t caller, dword_t handle,
                                        dword_t unk) {
  std::vector<XHttpCompletion> pending;
  {
    std::lock_guard<std::mutex> lock(g_xhttp_pump_mutex);
    pending.swap(g_xhttp_pump_queue);
  }

  for (const auto& completion : pending) {
    ExecuteCompletion(completion);
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return 0;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpDoWork, kNetworking, kImplemented);

// Timeouts, security flags and the like mean nothing to the local transport,
// but Destiny checks the result, so claim success.
dword_result_t NetDll_XHttpSetOption_entry(dword_t caller, dword_t handle,
                                           dword_t option, lpvoid_t buffer,
                                           dword_t buffer_length) {
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpSetOption, kNetworking, kStub);

dword_result_t NetDll_XHttpQueryOption_entry(dword_t caller, dword_t handle,
                                             dword_t option, lpvoid_t buffer,
                                             lpdword_t buffer_length) {
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpQueryOption, kNetworking, kStub);

dword_result_t NetDll_XHttpOpenRequest_entry(
    dword_t caller, dword_t connect_handle, lpstring_t verb, lpstring_t path,
    lpstring_t version, lpstring_t referrer, lpstring_t reserved,
    dword_t flag) {
  auto connection = xhttp_manager.Lookup(connect_handle);
  if (!connection || connection->type != XHttpHandleType::kConnection) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  const uint32_t handle = xhttp_manager.Create(XHttpHandleType::kRequest);

  auto request = xhttp_manager.Lookup(handle);
  request->async = connection->async;
  request->connection_handle = connect_handle;
  request->verb = verb ? verb.value() : "GET";
  request->path = path ? path.value() : "/";

  XELOGI("XHttp OpenRequest: {} {}", request->verb, request->path);

  XThread::SetLastError(X_ERROR_SUCCESS);
  return handle;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpOpenRequest, kNetworking, kImplemented);

dword_result_t NetDll_XHttpSetStatusCallback_entry(dword_t caller,
                                                   dword_t handle,
                                                   lpdword_t callback_ptr,
                                                   dword_t flags, dword_t unk) {
  auto handle_obj = xhttp_manager.Lookup(handle);
  if (!handle_obj) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return -1;
  }

  // Returns whichever callback was installed before, 0 for none.
  const uint32_t previous_callback = handle_obj->status_callback;
  handle_obj->status_callback =
      callback_ptr ? static_cast<uint32_t>(callback_ptr.guest_address()) : 0;

  return previous_callback;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpSetStatusCallback, kNetworking, kImplemented);

dword_result_t NetDll_XHttpSendRequest_entry(dword_t caller, dword_t hrequest,
                                             lpstring_t headers,
                                             dword_t hlength, lpvoid_t optional,
                                             dword_t optional_length,
                                             dword_t total_length,
                                             dword_t context) {
  auto request = xhttp_manager.Lookup(hrequest);
  if (!request || request->type != XHttpHandleType::kRequest) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  if (headers) {
    std::string request_headers = headers.value();
    if (hlength != static_cast<uint32_t>(-1) &&
        hlength < request_headers.size()) {
      request_headers = request_headers.substr(0, hlength);
    }

    for (auto& header : SplitHeaderLines(request_headers)) {
      request->request_headers.emplace_back(std::move(header));
    }
  }

  // More body may still follow via XHttpWriteData.
  if (optional && optional_length) {
    const char* optional_data = optional.as<const char*>();
    request->request_body.append(optional_data,
                                 static_cast<size_t>(optional_length));
  }

  request->context = context;

  XThread::SetLastError(X_ERROR_SUCCESS);

  if (request->async) {
    XHttpCompletion completion = {};
    completion.handle = hrequest;
    completion.context = context;
    completion.callback = ResolveStatusCallback(request);
    completion.status = XHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE;
    DeliverCompletion(std::move(completion));
  }

  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpSendRequest, kNetworking, kImplemented);

dword_result_t NetDll_XHttpWriteData_entry(dword_t caller, dword_t hrequest,
                                           lpvoid_t buffer,
                                           dword_t bytes_to_write,
                                           lpdword_t bytes_written_ptr) {
  auto request = xhttp_manager.Lookup(hrequest);
  if (!request || request->type != XHttpHandleType::kRequest) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  if (buffer && bytes_to_write) {
    const char* data = buffer.as<const char*>();
    request->request_body.append(data, static_cast<size_t>(bytes_to_write));
  }

  XThread::SetLastError(X_ERROR_SUCCESS);

  if (request->async) {
    XHttpCompletion completion = {};
    completion.handle = hrequest;
    completion.context = request->context;
    completion.callback = ResolveStatusCallback(request);
    completion.status = XHTTP_CALLBACK_STATUS_WRITE_COMPLETE;
    completion.alloc_write_count = true;
    completion.write_count = bytes_to_write.value();
    DeliverCompletion(std::move(completion));

    return 1;
  }

  if (bytes_written_ptr) {
    xe::be<uint32_t>* written = bytes_written_ptr;
    *written = static_cast<uint32_t>(bytes_to_write);
  }

  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpWriteData, kNetworking, kImplemented);

// Where the transaction actually runs.
dword_result_t NetDll_XHttpReceiveResponse_entry(dword_t caller,
                                                 dword_t hrequest,
                                                 dword_t reserved) {
  auto request = xhttp_manager.Lookup(hrequest);
  if (!request || request->type != XHttpHandleType::kRequest) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  XELOGI("XHttp ReceiveResponse: handle={:08X} async={}", hrequest.value(),
         request->async);

  if (request->async) {
    DeliverReceiveResponse(request, hrequest.value(), request->context,
                           ResolveStatusCallback(request));
    XThread::SetLastError(X_ERROR_SUCCESS);
    return 1;
  }

  PerformXHttpRequest(request);

  if (!request->succeeded) {
    return 0;
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpReceiveResponse, kNetworking, kImplemented);

dword_result_t NetDll_XHttpQueryHeaders_entry(dword_t caller, dword_t hrequest,
                                              dword_t info_level,
                                              lpstring_t name, lpvoid_t buffer,
                                              lpdword_t buffer_length_ptr,
                                              lpdword_t index_ptr) {
  auto request = xhttp_manager.Lookup(hrequest);
  if (!request || request->type != XHttpHandleType::kRequest) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  // Titles can query without ever calling XHttpReceiveResponse.
  PerformXHttpRequest(request);

  const uint32_t attribute = info_level & XHTTP_QUERY_ATTRIBUTE_MASK;
  const bool want_number = (info_level & XHTTP_QUERY_FLAG_NUMBER) != 0;

  XELOGI(
      "XHttp QueryHeaders: info_level={:08X} attribute={} number={} "
      "status_code={}",
      static_cast<uint32_t>(info_level), attribute, want_number,
      request->status_code);

  if (!buffer_length_ptr) {
    XThread::SetLastError(X_ERROR_INVALID_PARAMETER);
    return 0;
  }
  xe::be<uint32_t>* length_out = buffer_length_ptr;
  const uint32_t buffer_size = buffer_length_ptr.value();

  if (want_number) {
    uint32_t value = 0;
    switch (attribute) {
      case XHTTP_QUERY_STATUS_CODE:
        value = static_cast<uint32_t>(request->status_code);
        break;
      case XHTTP_QUERY_CONTENT_LENGTH:
        value = static_cast<uint32_t>(request->response_body.size());
        break;
      default:
        XThread::SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
        return 0;
    }

    if (!buffer || buffer_size < sizeof(uint32_t)) {
      *length_out = sizeof(uint32_t);
      XThread::SetLastError(X_ERROR_INSUFFICIENT_BUFFER);
      return 0;
    }

    xe::be<uint32_t>* value_out = buffer.as<xe::be<uint32_t>*>();
    *value_out = value;
    *length_out = sizeof(uint32_t);

    XThread::SetLastError(X_ERROR_SUCCESS);
    return 1;
  }

  std::string result;
  switch (attribute) {
    case XHTTP_QUERY_STATUS_CODE:
      result = std::to_string(request->status_code);
      break;
    case XHTTP_QUERY_CONTENT_LENGTH:
      result = std::to_string(request->response_body.size());
      break;
    default: {
      // Anything else is a header lookup by name.
      if (!name) {
        XThread::SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
        return 0;
      }
      if (!FindHeaderValue(request->response_headers, name.value(), &result)) {
        XThread::SetLastError(XHTTP_ERROR_HEADER_NOT_FOUND);
        return 0;
      }
    } break;
  }

  // Either way WinHTTP reports the length without the null, but the buffer
  // has to be big enough to hold it.
  const uint32_t required = static_cast<uint32_t>(result.size()) + 1;
  if (!buffer || buffer_size < required) {
    *length_out = static_cast<uint32_t>(result.size());
    XThread::SetLastError(X_ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }

  char* buffer_out = buffer.as<char*>();
  std::memcpy(buffer_out, result.data(), result.size());
  buffer_out[result.size()] = '\0';
  *length_out = static_cast<uint32_t>(result.size());

  XThread::SetLastError(X_ERROR_SUCCESS);
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpQueryHeaders, kNetworking, kImplemented);

dword_result_t NetDll_XHttpReadData_entry(dword_t caller, dword_t hrequest,
                                          lpvoid_t buffer,
                                          dword_t bytes_to_read,
                                          lpdword_t bytes_read_ptr) {
  auto request = xhttp_manager.Lookup(hrequest);
  if (!request || request->type != XHttpHandleType::kRequest) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  PerformXHttpRequest(request);

  const size_t remaining = request->response_body.size() - request->read_offset;
  const size_t to_copy =
      std::min<size_t>(remaining, static_cast<size_t>(bytes_to_read));

  if (to_copy && buffer) {
    char* buffer_out = buffer.as<char*>();
    std::memcpy(buffer_out,
                request->response_body.data() + request->read_offset, to_copy);
    request->read_offset += to_copy;
  }

  // The bytes are already in the caller's buffer; READ_COMPLETE just points
  // back at it.
  if (request->async) {
    XHttpCompletion completion = {};
    completion.handle = hrequest;
    completion.context = request->context;
    completion.callback = ResolveStatusCallback(request);
    completion.status = XHTTP_CALLBACK_STATUS_READ_COMPLETE;
    completion.info_ptr = buffer.guest_address();
    completion.info_len = static_cast<uint32_t>(to_copy);
    DeliverCompletion(std::move(completion));

    XThread::SetLastError(X_ERROR_SUCCESS);
    return 1;
  }

  if (bytes_read_ptr) {
    xe::be<uint32_t>* read_out = bytes_read_ptr;
    *read_out = static_cast<uint32_t>(to_copy);
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return 1;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpReadData, kNetworking, kImplemented);

dword_result_t NetDll_XHttpConnect_entry(dword_t caller, dword_t hSession,
                                         lpstring_t host, dword_t port,
                                         dword_t flags) {
  auto session = xhttp_manager.Lookup(hSession);
  if (!session || session->type != XHttpHandleType::kSession) {
    XThread::SetLastError(XHTTP_ERROR_INCORRECT_HANDLE_TYPE);
    return 0;
  }

  const uint32_t handle = xhttp_manager.Create(XHttpHandleType::kConnection);

  auto connection = xhttp_manager.Lookup(handle);
  connection->async = session->async;
  connection->session_handle = hSession;
  connection->host = host ? host.value() : "";
  connection->port = static_cast<uint16_t>(port);

  if (cvars::logging) {
    XELOGI("XHttp Connect: {}:{}", connection->host, connection->port);
  }

  XThread::SetLastError(X_ERROR_SUCCESS);
  return handle;
}
DECLARE_XAM_EXPORT1(NetDll_XHttpConnect, kNetworking, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(NetHttp);
