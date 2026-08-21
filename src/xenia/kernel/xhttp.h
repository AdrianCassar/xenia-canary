/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XHTTP_H_
#define XENIA_KERNEL_XHTTP_H_

#include <mutex>
#include <regex>
#include <string>
#include <vector>

#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {

enum XHTTP_QUERY : uint32_t {
  XHTTP_QUERY_CONTENT_LENGTH = 9,
  XHTTP_QUERY_EXPIRES = 13,
  XHTTP_QUERY_RAW_HEADERS_CRLF = 22,
  XHTTP_QUERY_STATUS_CODE = 0xFFFE,
  XHTTP_QUERY_CUSTOM = 0xFFFF,
};

enum XHTTP_QUERY_FLAGS : uint32_t {
  XHTTP_QUERY_FLAG_REQUEST_HEADERS = 0x80000000,
  XHTTP_QUERY_FLAG_SYSTEMTIME = 0x40000000,
  XHTTP_QUERY_FLAG_NUMBER = 0x20000000,
  XHTTP_QUERY_FLAG_FILETIME = 0x10000000,
  XHTTP_QUERY_ATTRIBUTE_MASK = 0x0000FFFF,
};

enum XHTTP_FLAG : uint32_t {
  XHTTP_FLAG_ASYNC = 0x10000000,
};

enum XHTTP_CALLBACK_STATUS : uint32_t {
  XHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE = 0x00020000,
  XHTTP_CALLBACK_STATUS_DATA_AVAILABLE = 0x00040000,
  XHTTP_CALLBACK_STATUS_READ_COMPLETE = 0x00080000,
  XHTTP_CALLBACK_STATUS_WRITE_COMPLETE = 0x00100000,
  XHTTP_CALLBACK_STATUS_REQUEST_ERROR = 0x00200000,
  XHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE = 0x00400000,
};

enum XHTTP_ASYNC_API : uint32_t {
  XHTTP_API_RECEIVE_RESPONSE = 1,
  XHTTP_API_READ_DATA = 3,
  XHTTP_API_WRITE_DATA = 4,
  XHTTP_API_SEND_REQUEST = 5,
};

class XHttp : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Http;

  enum class Kind {
    kSession,
    kConnection,
    kRequest,
  };

  XHttp(KernelState* kernel_state, Kind kind);
  ~XHttp() override = default;

  Kind kind() const { return kind_; }

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

  std::mutex perform_mutex;
  bool performed = false;
  bool succeeded = false;
  uint64_t status_code = 0;
  std::string response_headers;
  std::string response_body;
  size_t read_offset = 0;

  void Perform();
  uint32_t ResolveStatusCallback() const;

 private:
  Kind kind_;
};

// Queued notification, drained on the title's thread by XHttpDoWork.
struct XHttpCompletion {
  uint32_t handle = 0;          // hInternet (request handle)
  uint32_t session_handle = 0;  // Which session owns the req.
  uint32_t context = 0;         // dwContext
  uint32_t callback = 0;        // guest status callback
  uint32_t status = 0;          // XHTTP_CALLBACK_STATUS_*
  uint32_t info_ptr = 0;        // lpvStatusInformation (guest ptr) or 0
  uint32_t info_len = 0;        // dwStatusInformationLength

  // REQUEST_ERROR: pass an XHTTP_ASYNC_RESULT {api, error}.
  bool alloc_error = false;
  uint32_t error_api = 0;
  uint32_t error_code = 0;

  // WRITE_COMPLETE: pass a DWORD holding write_count.
  bool alloc_write_count = false;
  uint32_t write_count = 0;
};

std::vector<std::string> XHttpSplitHeaders(std::string request_headers);
bool XHttpFindHeaderValue(const std::string& raw_headers, const char* name,
                          std::string* out_value);
const std::regex& XHttpUrlRegex();
std::string XHttpUnescapeUrl(const std::string& escaped);
void XHttpDeliverCompletion(XHttpCompletion completion);
void XHttpDeliverReceiveResponse(const object_ref<XHttp>& request,
                                 uint32_t handle, uint32_t context,
                                 uint32_t callback);

uint32_t XHttpDoWork(uint32_t h_session, uint32_t wait_ms);

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XHTTP_H_
