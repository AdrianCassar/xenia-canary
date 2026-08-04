/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_SIGNALLING_H_
#define XENIA_KERNEL_XNET_SIGNALLING_H_

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace xe {
namespace kernel {

enum class XNetPeerType : uint8_t {
  kTitle = 0,
  kQos = 1,
};

struct XNetSignallingOffer {
  std::string from_peer_id;
  XNetPeerType from_peer_type;
  std::string sdp;
};

struct XNetSignallingAnswer {
  std::string from_peer_id;
  XNetPeerType from_peer_type;
  std::string sdp;
};

struct XNetSignallingCandidate {
  std::string from_peer_id;
  XNetPeerType from_peer_type;
  std::string candidate;
  std::string mid;
};

struct XNetSignallingGatheringDone {
  std::string from_peer_id;
  XNetPeerType from_peer_type;
};

class XNetSignalling {
 public:
  using OfferHandler = std::function<void(const XNetSignallingOffer&)>;
  using AnswerHandler = std::function<void(const XNetSignallingAnswer&)>;
  using CandidateHandler = std::function<void(const XNetSignallingCandidate&)>;
  using GatheringDoneHandler =
      std::function<void(const XNetSignallingGatheringDone&)>;

  XNetSignalling();
  ~XNetSignalling();

  void Start(const std::string& api_base, const std::string& local_peer_id);
  void Stop();

  bool IsConnected() const { return connected_.load(); }
  const std::string& local_peer_id() const { return local_peer_id_; }

  void SetHandlers(OfferHandler on_offer, AnswerHandler on_answer,
                   CandidateHandler on_candidate,
                   GatheringDoneHandler on_gathering_done);

  bool SendOffer(const std::string& target_peer_id, XNetPeerType type,
                 const std::string& sdp);
  bool SendAnswer(const std::string& target_peer_id, XNetPeerType type,
                  const std::string& sdp);
  bool SendCandidate(const std::string& target_peer_id, XNetPeerType type,
                     const std::string& candidate, const std::string& mid);
  bool SendGatheringComplete(const std::string& target_peer_id,
                             XNetPeerType type);

 private:
  void ThreadMain();
  bool ConnectLocked();
  void DisconnectLocked();
  bool SendRawLocked(const std::string& json);
  void HandleMessage(const std::string& json);
  static const char* PeerTypeString(XNetPeerType type);
  static XNetPeerType PeerTypeFromString(const std::string& s);

  std::string api_base_;
  std::string local_peer_id_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::thread thread_;

  mutable std::mutex mutex_;
  void* curl_ = nullptr;  // CURL*
  std::queue<std::string> outbound_;

  OfferHandler on_offer_;
  AnswerHandler on_answer_;
  CandidateHandler on_candidate_;
  GatheringDoneHandler on_gathering_done_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_SIGNALLING_H_
