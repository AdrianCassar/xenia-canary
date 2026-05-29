/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/unmarshaller/xuser_estimate_rank_for_ratings_unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

XUserEstimateRankForRatingUnmarshaller::XUserEstimateRankForRatingUnmarshaller(
    KernelState* kernel_state, uint32_t marshaller_address)
    : Unmarshaller(kernel_state, marshaller_address) {}

X_HRESULT XUserEstimateRankForRatingUnmarshaller::Deserialize() {
  if (!GetXLiveBaseAsyncMessage()->xlive_async_task_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask().GetXLiveAsyncTask()->marshalled_request_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask().GetXLiveAsyncTask()->results_ptr) {
    return X_E_INVALIDARG;
  }

  if (!GetAsyncTask().GetXLiveAsyncTask()->results_size) {
    return X_E_INVALIDARG;
  }

  title_id_ = Read<uint32_t>();
  ratings_count_ = Read<uint32_t>();

  if (ratings_count_ > X_ONLINE_MAX_STATS_ESTIMATE_RATING_COUNT) {
    return X_E_INVALIDARG;
  }

  for (uint32_t i = 0; i < ratings_count_; i++) {
    X_USER_RANK_REQUEST estimate_rank = Read<X_USER_RANK_REQUEST>();

    estimate_ranks_.push_back(estimate_rank);
  }

  if (GetPosition() !=
      GetAsyncTask().GetXLiveAsyncTask()->marshalled_request_size) {
    assert_always(std::format("{} deserialization incomplete", __func__));
  }

  return X_E_SUCCESS;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
