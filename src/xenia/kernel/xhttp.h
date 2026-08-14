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
#include <string>
#include <vector>

#include "xenia/kernel/xnet.h"
#include "xenia/kernel/xobject.h"

namespace xe {
namespace kernel {

// XHTTP session / connection / request handle. WinHTTP-style hierarchy:
// session (XHttpOpen) owns connections (XHttpConnect) owns requests
// (XHttpOpenRequest).
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

  // Filled by Perform(). perform_mutex serializes the first perform so other
  // threads don't read a partial response.
  std::mutex perform_mutex;
  bool performed = false;
  bool succeeded = false;
  uint64_t status_code = 0;
  std::string response_headers;
  std::string response_body;
  size_t read_offset = 0;

  // NetDll_XHttp* implementation surface.
  static bool Startup();
  static void Shutdown();
  static uint32_t Open(const std::string& user_agent, uint32_t flags);
  static bool CloseHandle(uint32_t handle);
  static uint32_t Connect(uint32_t session_handle, const std::string& host,
                          uint16_t port, uint32_t flags);
  static uint32_t OpenRequest(uint32_t connect_handle, const std::string& verb,
                              const std::string& path, uint32_t flags);
  static uint32_t SetStatusCallback(uint32_t handle,
                                    uint32_t callback_guest_address);
  static bool SendRequest(uint32_t hrequest, const char* headers,
                          uint32_t headers_length, const void* optional,
                          uint32_t optional_length, uint32_t total_length,
                          uint32_t context);
  static bool WriteData(uint32_t hrequest, const void* buffer,
                        uint32_t bytes_to_write, uint32_t* bytes_written_out);
  static bool ReceiveResponse(uint32_t hrequest);
  static bool QueryHeaders(uint32_t hrequest, uint32_t info_level,
                           const char* name, uint8_t* buffer,
                           xe::be<uint32_t>* buffer_length_ptr,
                           xe::be<uint32_t>* index_ptr);
  static bool ReadData(uint32_t hrequest, void* buffer,
                       uint32_t buffer_guest_address, uint32_t bytes_to_read,
                       uint32_t* bytes_read_out);
  static bool CrackUrl(const std::string& url, uint32_t url_guest_address,
                       uint32_t url_length, uint32_t flags,
                       XHTTP_URL_COMPONENTS* components);
  static bool CrackUrlW(const std::u16string& url, uint32_t url_guest_address,
                        uint32_t url_length, uint32_t flags,
                        XHTTP_URL_COMPONENTS* components);
  static uint32_t DoWork();
  static bool SetOption(uint32_t handle, uint32_t option, const void* buffer,
                        uint32_t buffer_length);
  static bool QueryOption(uint32_t handle, uint32_t option, void* buffer,
                          uint32_t* buffer_length);

  void Perform();
  uint32_t ResolveStatusCallback() const;

 private:
  Kind kind_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XHTTP_H_
