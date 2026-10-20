// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "rewriter/gru_rewriter.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "base/vlog.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "gru_scorer/charindex.hh"
#include "gru_scorer/gru_model.hh"

ABSL_FLAG(bool, enable_gru_rewriter, false, "Enable GRU neural LM rewriter.");
ABSL_FLAG(bool, show_gru_score, true,
          "Show GRU score in candidate description.");
ABSL_FLAG(double, gru_score_weight, 800.0,
          "Weight for GRU score combining with Viterbi cost.");
ABSL_FLAG(std::string, gru_model_path, "",
          "Path to GRU model file. If empty, uses default path.");
ABSL_FLAG(std::string, gru_chardata_path, "",
          "Path to CharIndex data file. If empty, uses default path.");

namespace mozc {
namespace {

constexpr size_t kMaxCandidateEvaluation = 10;
constexpr size_t kBeamSize = 8;
constexpr size_t kMaxCandidatesPerSegmentForBeam = 5;
constexpr int32_t kMaxCostDiff = 4000;
constexpr double kEmojiPenalty = 15000.0;

bool ContainsEmoji(const charindex::CharIndex& indexer, const std::string& str) {
  std::string_view sv = str;
  while (!sv.empty()) {
    size_t consumed = 0;
    const char32_t cp = indexer.DecodeUtf8Char(sv, consumed);
    if (consumed == 0) break;
    if (charindex::IsEmojiCodePoint(cp)) return true;
    sv.remove_prefix(consumed);
  }
  return false;
}

bool IsCandidateEmoji(const charindex::CharIndex& indexer,
                      const Segment::Candidate& cand) {
  if (absl::StrContains(cand.description, "絵文字")) return true;
  return ContainsEmoji(indexer, cand.value);
}

std::string ResolveFilePath(const std::string& user_specified,
                            const std::string& default_filename) {
  if (!user_specified.empty()) {
    if (std::filesystem::exists(user_specified)) {
      return user_specified;
    }
    const char* test_srcdir = std::getenv("TEST_SRCDIR");
    if (test_srcdir != nullptr) {
      std::string p = FileUtil::JoinPath(test_srcdir, user_specified);
      if (std::filesystem::exists(p)) return p;
      p = FileUtil::JoinPath(test_srcdir, FileUtil::JoinPath("_main", user_specified));
      if (std::filesystem::exists(p)) return p;
    }
    return "";
  }

  // 1. ~/.config/mozc/<filename>
  const std::string user_profile = SystemUtil::GetUserProfileDirectory();
  if (!user_profile.empty()) {
    std::string path = FileUtil::JoinPath(user_profile, default_filename);
    if (std::filesystem::exists(path)) return path;
  }

  // 2. Known local development paths & runfiles
  std::vector<std::string> filenames = {default_filename};
  if (default_filename == "grumodel.bin") {
    filenames.push_back("dummy_model.bin");
  }

  for (const auto& fname : filenames) {
    const std::vector<std::string> dev_paths = {
        FileUtil::JoinPath("models", fname),
        FileUtil::JoinPath("../models", fname),
        FileUtil::JoinPath("gru_scorer", fname),
        FileUtil::JoinPath("../gru_scorer", fname),
        fname,
    };
    for (const auto& p : dev_paths) {
      if (std::filesystem::exists(p)) return p;
    }

    // 3. Bazel runfiles (TEST_SRCDIR)
    const char* test_srcdir = std::getenv("TEST_SRCDIR");
    if (test_srcdir != nullptr) {
      std::string p = FileUtil::JoinPath(
          test_srcdir, FileUtil::JoinPath("_main/gru_scorer", fname));
      if (std::filesystem::exists(p)) return p;
      p = FileUtil::JoinPath(
          test_srcdir, FileUtil::JoinPath("gru_scorer", fname));
      if (std::filesystem::exists(p)) return p;
    }
  }

  return "";
}

struct CandidateScore {
  const Segment::Candidate* cand_ptr = nullptr;
  double combined_cost = 0.0;
  float gru_score = 0.0f;
  bool is_emoji = false;
  bool is_best = false;
};

void UpdateCandidateDescription(Segment::Candidate* cand,
                                float gru_score,
                                bool is_emoji,
                                bool is_promoted) {
  std::string tag = is_emoji ? "GRU:EMOJI" : absl::StrFormat("GRU:%.2f", gru_score);
  if (is_promoted) {
    tag += "*";
  }
  const size_t pos = cand->description.find("GRU:");
  if (pos != std::string::npos) {
    const size_t end_pos = cand->description.find(' ', pos);
    if (end_pos != std::string::npos) {
      cand->description.replace(pos, end_pos - pos, tag);
    } else {
      cand->description.replace(pos, std::string::npos, tag);
    }
  } else {
    if (cand->description.empty()) {
      cand->description = std::move(tag);
    } else {
      cand->description = absl::StrCat(cand->description, " ", tag);
    }
  }
}

// Sorts evaluated candidates of `seg` by combined_cost in ascending order.
// Ensures that the candidate marked with is_best (if any) is placed at index 0.
bool SortSegmentCandidates(Segment* seg,
                           std::vector<CandidateScore> scored,
                           bool show_score) {
  if (scored.empty()) return false;

  const Segment::Candidate* original_top = &seg->candidate(0);

  // Sort by combined_cost ascending.
  // If is_best is true for one candidate, ensure it sorts first.
  std::stable_sort(scored.begin(), scored.end(),
                   [](const CandidateScore& a, const CandidateScore& b) {
                     if (a.is_best != b.is_best) {
                       return a.is_best;
                     }
                     return a.combined_cost < b.combined_cost;
                   });

  // Reorder candidates in seg using move_candidate
  bool modified = false;
  for (size_t target_idx = 0; target_idx < scored.size(); ++target_idx) {
    const Segment::Candidate* target_ptr = scored[target_idx].cand_ptr;
    for (size_t cur_idx = target_idx; cur_idx < seg->candidates_size(); ++cur_idx) {
      if (&seg->candidate(cur_idx) == target_ptr) {
        if (cur_idx != target_idx) {
          seg->move_candidate(cur_idx, target_idx);
          modified = true;
        }
        break;
      }
    }
  }

  // Update descriptions and attribute for the top candidate
  for (size_t i = 0; i < scored.size(); ++i) {
    auto* cand = seg->mutable_candidate(i);
    const bool is_promoted = (i == 0 && cand != original_top);
    if (show_score) {
      UpdateCandidateDescription(cand, scored[i].gru_score, scored[i].is_emoji, is_promoted);
    }
  }

  if (&seg->candidate(0) != original_top) {
    seg->mutable_candidate(0)->attributes |= converter::Attribute::CONTEXT_SENSITIVE;
  }

  return modified;
}

}  // namespace

GruRewriter::GruRewriter() = default;
GruRewriter::~GruRewriter() = default;

bool GruRewriter::EnsureInitialized() const {
  absl::MutexLock l(mutex_);

  const std::string chardata_path =
      ResolveFilePath(absl::GetFlag(FLAGS_gru_chardata_path), "chardata.bin");
  const std::string model_path =
      ResolveFilePath(absl::GetFlag(FLAGS_gru_model_path), "grumodel.bin");

  if (is_initialized_ && model_ && model_->is_loaded() && indexer_ &&
      loaded_chardata_path_ == chardata_path &&
      loaded_model_path_ == model_path) {
    return true;
  }

  is_initialized_ = true;
  loaded_chardata_path_ = chardata_path;
  loaded_model_path_ = model_path;
  model_.reset();
  indexer_.reset();

  if (chardata_path.empty()) {
    MOZC_VLOG(1) << "GruRewriter: chardata.bin not found.";
    return false;
  }

  auto indexer = std::make_unique<charindex::CharIndex>();
  if (!indexer->LoadFile(chardata_path)) {
    LOG(WARNING) << "GruRewriter: Failed to load chardata: " << chardata_path;
    return false;
  }

  if (model_path.empty()) {
    MOZC_VLOG(1) << "GruRewriter: grumodel.bin not found. GruRewriter disabled.";
    return false;
  }

  auto model = std::make_unique<gruscorer::GruModel>();
  if (!model->LoadModel(model_path)) {
    LOG(WARNING) << "GruRewriter: Failed to load model: " << model_path;
    return false;
  }

  model_ = std::move(model);
  indexer_ = std::move(indexer);
  LOG(INFO) << "GruRewriter: Successfully loaded GRU model from " << model_path;
  return true;
}

bool GruRewriter::Reload() {
  {
    absl::MutexLock l(mutex_);
    is_initialized_ = false;
    loaded_chardata_path_.clear();
    loaded_model_path_.clear();
    model_.reset();
    indexer_.reset();
  }
  return EnsureInitialized();
}

bool GruRewriter::Rewrite(const ConversionRequest& request,
                          Segments* segments) const {
  if (!absl::GetFlag(FLAGS_enable_gru_rewriter)) {
    return false;
  }

  if (request.options().skip_slow_rewriters) {
    return false;
  }

  if (segments == nullptr || segments->conversion_segments_size() == 0) {
    return false;
  }

  if (!EnsureInitialized()) {
    return false;
  }

  const double alpha = absl::GetFlag(FLAGS_gru_score_weight);
  const bool show_score = absl::GetFlag(FLAGS_show_gru_score);
  const size_t num_segments = segments->conversion_segments_size();

  // Single-segment case
  if (num_segments == 1) {
    Segment* seg = segments->mutable_conversion_segment(0);
    if (seg->candidates_size() <= 1) {
      if (seg->candidates_size() == 1 && show_score) {
        auto* cand = seg->mutable_candidate(0);
        const bool is_emoji = IsCandidateEmoji(*indexer_, *cand);
        const float s = model_->ScoreString(*indexer_, cand->value, nullptr, true);
        UpdateCandidateDescription(cand, s, is_emoji, false);
      }
      return false;
    }

    const int32_t top_cost = seg->candidate(0).cost;
    const size_t num_eval = std::min(seg->candidates_size(), kMaxCandidateEvaluation);

    std::vector<CandidateScore> scored;
    scored.reserve(num_eval);

    for (size_t i = 0; i < num_eval; ++i) {
      const auto& cand = seg->candidate(i);
      const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
      const float gru_score = model_->ScoreString(*indexer_, cand.value, nullptr, true);

      double combined_cost = static_cast<double>(cand.cost) - (alpha * gru_score);
      if (is_emoji) combined_cost += kEmojiPenalty;
      if (cand.cost > top_cost + kMaxCostDiff) {
        combined_cost += 1e9;
      }

      scored.push_back({&cand, combined_cost, gru_score, is_emoji, false});
    }

    auto best_it = std::min_element(
        scored.begin(), scored.end(),
        [](const CandidateScore& a, const CandidateScore& b) {
          return a.combined_cost < b.combined_cost;
        });
    if (best_it != scored.end()) {
      best_it->is_best = true;
    }

    return SortSegmentCandidates(seg, std::move(scored), show_score);
  }

  // Multi-segment beam search optimization
  struct BeamPath {
    std::vector<size_t> indices;
    std::string text;
    int32_t total_viterbi_cost = 0;
    float gru_score = 0.0f;
    double eval_score = 0.0;
    bool has_emoji = false;
  };

  std::vector<BeamPath> beam;

  // 1. Initialize beam with segment 0 candidates
  {
    const Segment& seg0 = segments->conversion_segment(0);
    const int32_t top_cost = seg0.candidate(0).cost;
    const size_t num_eval = std::min(seg0.candidates_size(), kMaxCandidatesPerSegmentForBeam);

    for (size_t j = 0; j < num_eval; ++j) {
      const auto& cand = seg0.candidate(j);
      if (cand.cost > top_cost + kMaxCostDiff) continue;

      const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
      const float score = model_->ScoreString(*indexer_, cand.value, nullptr, true);
      double eval = static_cast<double>(cand.cost) - (alpha * score);
      if (is_emoji) eval += kEmojiPenalty;

      beam.push_back({{j}, cand.value, cand.cost, score, eval, is_emoji});
    }

    std::sort(beam.begin(), beam.end(),
              [](const BeamPath& a, const BeamPath& b) { return a.eval_score < b.eval_score; });
    if (beam.size() > kBeamSize) beam.resize(kBeamSize);
  }

  if (beam.empty()) return false;

  // 2. Expand beam through subsequent segments 1 .. num_segments - 1
  for (size_t seg_idx = 1; seg_idx < num_segments; ++seg_idx) {
    const Segment& seg = segments->conversion_segment(seg_idx);
    const int32_t top_cost = seg.candidate(0).cost;
    const size_t num_eval = std::min(seg.candidates_size(), kMaxCandidatesPerSegmentForBeam);

    std::vector<BeamPath> next_beam;
    next_beam.reserve(beam.size() * num_eval);

    for (const auto& path : beam) {
      for (size_t j = 0; j < num_eval; ++j) {
        const auto& cand = seg.candidate(j);
        if (cand.cost > top_cost + kMaxCostDiff) continue;

        const bool is_emoji = path.has_emoji || IsCandidateEmoji(*indexer_, cand);
        const std::string new_text = path.text + cand.value;
        const float new_score = model_->ScoreString(*indexer_, new_text, nullptr, true);
        const int32_t new_viterbi = path.total_viterbi_cost + cand.cost;

        double eval = static_cast<double>(new_viterbi) - (alpha * new_score);
        if (is_emoji) eval += kEmojiPenalty;

        std::vector<size_t> new_indices = path.indices;
        new_indices.push_back(j);

        next_beam.push_back({std::move(new_indices), std::move(new_text),
                             new_viterbi, new_score, eval, is_emoji});
      }
    }

    if (next_beam.empty()) break;

    std::sort(next_beam.begin(), next_beam.end(),
              [](const BeamPath& a, const BeamPath& b) { return a.eval_score < b.eval_score; });
    if (next_beam.size() > kBeamSize) next_beam.resize(kBeamSize);
    beam = std::move(next_beam);
  }

  if (beam.empty()) return false;

  const BeamPath& best_path = beam[0];
  bool modified_any = false;

  // 3. Promote best candidate to index 0 and sort secondary candidates for each segment
  for (size_t seg_idx = 0; seg_idx < num_segments; ++seg_idx) {
    Segment* seg = segments->mutable_conversion_segment(seg_idx);
    const size_t best_cand_idx = best_path.indices[seg_idx];
    const int32_t top_cost = seg->candidate(0).cost;
    const size_t num_eval = std::min(seg->candidates_size(), kMaxCandidateEvaluation);

    // Prefix text from preceding segments in best_path
    std::string prefix;
    for (size_t p = 0; p < seg_idx; ++p) {
      prefix += segments->conversion_segment(p).candidate(best_path.indices[p]).value;
    }

    std::vector<CandidateScore> scored;
    scored.reserve(num_eval);

    for (size_t j = 0; j < num_eval; ++j) {
      const auto& cand = seg->candidate(j);
      const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
      const float score = model_->ScoreString(*indexer_, prefix + cand.value, nullptr, true);

      double eval = static_cast<double>(cand.cost) - (alpha * score);
      if (is_emoji) eval += kEmojiPenalty;
      if (cand.cost > top_cost + kMaxCostDiff) {
        eval += 1e9;
      }

      const bool is_best = (j == best_cand_idx);
      scored.push_back({&cand, eval, score, is_emoji, is_best});
    }

    if (SortSegmentCandidates(seg, std::move(scored), show_score)) {
      modified_any = true;
    }
  }

  return modified_any;
}

bool GruRewriter::Focus(Segments* segments, size_t segment_index,
                        int candidate_index) const {
  if (!absl::GetFlag(FLAGS_enable_gru_rewriter)) {
    return false;
  }
  if (segments == nullptr || segments->conversion_segments_size() == 0) {
    return false;
  }
  if (!EnsureInitialized()) {
    return false;
  }

  if (segment_index >= segments->segments_size()) {
    return false;
  }
  const size_t history_size = segments->history_segments_size();
  if (segment_index < history_size) {
    return false;
  }

  const size_t conv_seg_index = segment_index - history_size;
  const size_t num_conv_segments = segments->conversion_segments_size();
  if (conv_seg_index + 1 >= num_conv_segments) {
    // Last conversion segment focused: no subsequent segments to rerank.
    return false;
  }

  const Segment& focused_seg = segments->segment(segment_index);
  std::string focused_value;
  if (candidate_index >= 0) {
    if (candidate_index >= focused_seg.candidates_size()) {
      return false;
    }
    focused_value = focused_seg.candidate(candidate_index).value;
  } else {
    const int meta_idx = -candidate_index - 1;
    if (meta_idx >= focused_seg.meta_candidates_size()) {
      return false;
    }
    focused_value = focused_seg.meta_candidate(meta_idx).value;
  }

  // Construct prefix up to conv_seg_index using candidate(0) for previous segments
  // and focused_value for conv_seg_index.
  std::string prefix;
  for (size_t p = 0; p < conv_seg_index; ++p) {
    prefix += segments->conversion_segment(p).candidate(0).value;
  }
  prefix += focused_value;

  const double alpha = absl::GetFlag(FLAGS_gru_score_weight);
  const bool show_score = absl::GetFlag(FLAGS_show_gru_score);
  const size_t remaining_segments = num_conv_segments - (conv_seg_index + 1);

  bool modified_any = false;

  if (remaining_segments == 1) {
    // Single remaining segment: evaluate and sort candidates directly
    Segment* next_seg = segments->mutable_conversion_segment(conv_seg_index + 1);
    const int32_t top_cost = next_seg->candidate(0).cost;
    const size_t num_eval = std::min(next_seg->candidates_size(), kMaxCandidateEvaluation);

    std::vector<CandidateScore> scored;
    scored.reserve(num_eval);

    for (size_t j = 0; j < num_eval; ++j) {
      const auto& cand = next_seg->candidate(j);
      const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
      const float score = model_->ScoreString(*indexer_, prefix + cand.value, nullptr, true);

      double eval = static_cast<double>(cand.cost) - (alpha * score);
      if (is_emoji) eval += kEmojiPenalty;
      if (cand.cost > top_cost + kMaxCostDiff) {
        eval += 1e9;
      }

      scored.push_back({&cand, eval, score, is_emoji, false});
    }

    auto best_it = std::min_element(
        scored.begin(), scored.end(),
        [](const CandidateScore& a, const CandidateScore& b) {
          return a.combined_cost < b.combined_cost;
        });
    if (best_it != scored.end()) {
      best_it->is_best = true;
    }

    if (SortSegmentCandidates(next_seg, std::move(scored), show_score)) {
      modified_any = true;
    }
  } else {
    // Multiple remaining segments: run beam search over subsequent segments
    struct BeamPath {
      std::vector<size_t> indices;
      std::string text;
      int32_t total_viterbi_cost = 0;
      float gru_score = 0.0f;
      double eval_score = 0.0;
      bool has_emoji = false;
    };

    std::vector<BeamPath> beam;
    const size_t first_sub_idx = conv_seg_index + 1;
    {
      const Segment& seg = segments->conversion_segment(first_sub_idx);
      const int32_t top_cost = seg.candidate(0).cost;
      const size_t num_eval = std::min(seg.candidates_size(), kMaxCandidatesPerSegmentForBeam);

      for (size_t j = 0; j < num_eval; ++j) {
        const auto& cand = seg.candidate(j);
        if (cand.cost > top_cost + kMaxCostDiff) continue;

        const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
        const std::string text = prefix + cand.value;
        const float score = model_->ScoreString(*indexer_, text, nullptr, true);
        double eval = static_cast<double>(cand.cost) - (alpha * score);
        if (is_emoji) eval += kEmojiPenalty;

        beam.push_back({{j}, text, cand.cost, score, eval, is_emoji});
      }

      std::sort(beam.begin(), beam.end(),
                [](const BeamPath& a, const BeamPath& b) { return a.eval_score < b.eval_score; });
      if (beam.size() > kBeamSize) beam.resize(kBeamSize);
    }

    if (!beam.empty()) {
      for (size_t sub_idx = first_sub_idx + 1; sub_idx < num_conv_segments; ++sub_idx) {
        const Segment& seg = segments->conversion_segment(sub_idx);
        const int32_t top_cost = seg.candidate(0).cost;
        const size_t num_eval = std::min(seg.candidates_size(), kMaxCandidatesPerSegmentForBeam);

        std::vector<BeamPath> next_beam;
        next_beam.reserve(beam.size() * num_eval);

        for (const auto& path : beam) {
          for (size_t j = 0; j < num_eval; ++j) {
            const auto& cand = seg.candidate(j);
            if (cand.cost > top_cost + kMaxCostDiff) continue;

            const bool is_emoji = path.has_emoji || IsCandidateEmoji(*indexer_, cand);
            const std::string new_text = path.text + cand.value;
            const float new_score = model_->ScoreString(*indexer_, new_text, nullptr, true);
            const int32_t new_viterbi = path.total_viterbi_cost + cand.cost;

            double eval = static_cast<double>(new_viterbi) - (alpha * new_score);
            if (is_emoji) eval += kEmojiPenalty;

            std::vector<size_t> new_indices = path.indices;
            new_indices.push_back(j);

            next_beam.push_back({std::move(new_indices), std::move(new_text),
                                 new_viterbi, new_score, eval, is_emoji});
          }
        }

        if (next_beam.empty()) break;

        std::sort(next_beam.begin(), next_beam.end(),
                  [](const BeamPath& a, const BeamPath& b) { return a.eval_score < b.eval_score; });
        if (next_beam.size() > kBeamSize) next_beam.resize(kBeamSize);
        beam = std::move(next_beam);
      }
    }

    if (!beam.empty()) {
      const BeamPath& best_path = beam[0];
      for (size_t sub_idx = first_sub_idx; sub_idx < num_conv_segments; ++sub_idx) {
        const size_t rel_idx = sub_idx - first_sub_idx;
        const size_t best_cand_idx = best_path.indices[rel_idx];
        Segment* seg = segments->mutable_conversion_segment(sub_idx);
        const int32_t top_cost = seg->candidate(0).cost;
        const size_t num_eval = std::min(seg->candidates_size(), kMaxCandidateEvaluation);

        std::string sub_prefix = prefix;
        for (size_t p = 0; p < rel_idx; ++p) {
          sub_prefix += segments->conversion_segment(first_sub_idx + p).candidate(best_path.indices[p]).value;
        }

        std::vector<CandidateScore> scored;
        scored.reserve(num_eval);

        for (size_t j = 0; j < num_eval; ++j) {
          const auto& cand = seg->candidate(j);
          const bool is_emoji = IsCandidateEmoji(*indexer_, cand);
          const float score = model_->ScoreString(*indexer_, sub_prefix + cand.value, nullptr, true);

          double eval = static_cast<double>(cand.cost) - (alpha * score);
          if (is_emoji) eval += kEmojiPenalty;
          if (cand.cost > top_cost + kMaxCostDiff) {
            eval += 1e9;
          }

          const bool is_best = (j == best_cand_idx);
          scored.push_back({&cand, eval, score, is_emoji, is_best});
        }

        if (SortSegmentCandidates(seg, std::move(scored), show_score)) {
          modified_any = true;
        }
      }
    }
  }

  return modified_any;
}

std::optional<RewriterInterface::ResizeSegmentsRequest>
GruRewriter::CheckResizeSegmentsRequest(const ConversionRequest& request,
                                        const Segments& segments) const {
  // Reserved for Phase 2: automatic segment boundary adjustment
  return std::nullopt;
}

}  // namespace mozc

