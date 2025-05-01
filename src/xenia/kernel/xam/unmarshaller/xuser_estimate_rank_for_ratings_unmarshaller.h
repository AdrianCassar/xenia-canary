/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_ESTIMATE_RANK_FOR_RATINGS_UNMARSHALLER_H_
#define XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_ESTIMATE_RANK_FOR_RATINGS_UNMARSHALLER_H_

#include "xenia/kernel/xam/unmarshaller/unmarshaller.h"

namespace xe {
namespace kernel {
namespace xam {

class XUserEstimateRankForRatingUnmarshaller : public Unmarshaller {
 public:
  XUserEstimateRankForRatingUnmarshaller(uint32_t marshaller_buffer);

  ~XUserEstimateRankForRatingUnmarshaller() {};

  virtual X_HRESULT Deserialize();

  const uint32_t TitleId() const { return title_id_; };

  const uint32_t RatingCount() const { return ratings_count_; };

  const std::vector<X_USER_RANK_REQUEST>& StatsEstimateRanks() const {
    return estimate_ranks_;
  };

 private:
  uint32_t title_id_;
  uint32_t ratings_count_;
  std::vector<X_USER_RANK_REQUEST> estimate_ranks_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_UNMARSHALLER_XUSER_ESTIMATE_RANK_FOR_RATINGS_UNMARSHALLER_H_