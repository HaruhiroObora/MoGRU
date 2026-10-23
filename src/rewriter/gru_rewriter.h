// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_REWRITER_GRU_REWRITER_H_
#define MOZC_REWRITER_GRU_REWRITER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/flags/declare.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "converter/segments.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

ABSL_DECLARE_FLAG(bool, enable_gru_rewriter);
ABSL_DECLARE_FLAG(bool, enable_gru_resegmentation);
ABSL_DECLARE_FLAG(int32_t, gru_resegment_cost_threshold);
ABSL_DECLARE_FLAG(double, gru_score_weight);
ABSL_DECLARE_FLAG(double, gru_katakana_multiplier);
ABSL_DECLARE_FLAG(double, gru_hiragana_multiplier);
ABSL_DECLARE_FLAG(double, gru_hiragana_length_power);
ABSL_DECLARE_FLAG(std::string, gru_model_path);
ABSL_DECLARE_FLAG(std::string, gru_chardata_path);
ABSL_DECLARE_FLAG(std::string, gru_config_path);

namespace gruscorer {
class GruModel;
}
namespace charindex {
class CharIndex;
}

namespace mozc {

class ImmutableConverterInterface;

struct GruPenaltyConfig {
  double katakana_multiplier = 1.3;
  double hiragana_multiplier = 1.1;
  double hiragana_length_power = 1.0;
  double emoji_penalty = 15000.0;
  double alphabet_multiplier = 2.0;
  bool enable_resegmentation = true;
  double resegment_threshold = 600.0;
};

class GruRewriter : public RewriterInterface {
 public:
  explicit GruRewriter(const ImmutableConverterInterface* immutable_converter = nullptr);
  ~GruRewriter() override;

  int capability(const ConversionRequest& request) const override {
    return CONVERSION | PREDICTION;
  }

  bool Rewrite(const ConversionRequest& request,
               Segments* segments) const override;

  bool Focus(Segments* segments, size_t segment_index,
             int candidate_index) const override;

  std::optional<ResizeSegmentsRequest> CheckResizeSegmentsRequest(
      const ConversionRequest& request,
      const Segments& segments) const override;

  // Explicitly initialize or reload models and configurations.
  bool Reload() override;

 private:
  struct ResegmentResult {
    std::vector<uint8_t> new_sizes;
    double score_diff = 0.0;
  };

  bool EnsureInitialized() const;
  GruPenaltyConfig GetPenaltyConfig() const;

  std::optional<ResegmentResult> EvaluateResegmentation(
      const Segments& segments,
      size_t start_seg_idx,
      const std::string& prefix_context,
      const GruPenaltyConfig& penalty_config,
      double alpha,
      size_t focused_seg_idx = static_cast<size_t>(-1),
      int focused_cand_idx = 0) const;

  bool RerankSegmentsWithBeamSearch(
      Segments* segments,
      size_t start_seg_idx,
      const std::string& prefix_context,
      const GruPenaltyConfig& penalty_config,
      double alpha,
      bool show_score) const;

  mutable absl::Mutex mutex_;
  mutable std::unique_ptr<gruscorer::GruModel> model_ ABSL_GUARDED_BY(mutex_);
  mutable std::unique_ptr<charindex::CharIndex> indexer_ ABSL_GUARDED_BY(mutex_);
  mutable std::string loaded_model_path_ ABSL_GUARDED_BY(mutex_);
  mutable std::string loaded_chardata_path_ ABSL_GUARDED_BY(mutex_);
  mutable std::string config_file_path_ ABSL_GUARDED_BY(mutex_);
  mutable GruPenaltyConfig penalty_config_ ABSL_GUARDED_BY(mutex_);
  mutable int64_t last_config_mtime_ ABSL_GUARDED_BY(mutex_) = 0;
  mutable absl::Time last_config_check_time_ ABSL_GUARDED_BY(mutex_) = absl::InfinitePast();
  mutable bool is_initialized_ ABSL_GUARDED_BY(mutex_) = false;

  const ImmutableConverterInterface* immutable_converter_ = nullptr;
};

}  // namespace mozc

#endif  // MOZC_REWRITER_GRU_REWRITER_H_
