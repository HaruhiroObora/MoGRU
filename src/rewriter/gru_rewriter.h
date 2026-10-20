// Copyright 2010-2021, Google Inc.
// All rights reserved.

#ifndef MOZC_REWRITER_GRU_REWRITER_H_
#define MOZC_REWRITER_GRU_REWRITER_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "converter/segments.h"
#include "request/conversion_request.h"
#include "absl/flags/declare.h"
#include "rewriter/rewriter_interface.h"

ABSL_DECLARE_FLAG(bool, enable_gru_rewriter);
ABSL_DECLARE_FLAG(double, gru_score_weight);
ABSL_DECLARE_FLAG(std::string, gru_model_path);
ABSL_DECLARE_FLAG(std::string, gru_chardata_path);

namespace gruscorer {
class GruModel;
}
namespace charindex {
class CharIndex;
}

namespace mozc {

class GruRewriter : public RewriterInterface {
 public:
  GruRewriter();
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

  // Explicitly initialize or reload models.
  bool Reload() override;

 private:
  bool EnsureInitialized() const;

  mutable absl::Mutex mutex_;
  mutable std::unique_ptr<gruscorer::GruModel> model_ ABSL_GUARDED_BY(mutex_);
  mutable std::unique_ptr<charindex::CharIndex> indexer_ ABSL_GUARDED_BY(mutex_);
  mutable std::string loaded_model_path_ ABSL_GUARDED_BY(mutex_);
  mutable std::string loaded_chardata_path_ ABSL_GUARDED_BY(mutex_);
  mutable bool is_initialized_ ABSL_GUARDED_BY(mutex_) = false;
};

}  // namespace mozc

#endif  // MOZC_REWRITER_GRU_REWRITER_H_

