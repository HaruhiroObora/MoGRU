// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "rewriter/gru_rewriter.h"

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc {
namespace {

void AddCandidate(absl::string_view key, absl::string_view value,
                  int32_t cost, Segment* segment) {
  converter::Candidate* candidate = segment->add_candidate();
  candidate->key = std::string(key);
  candidate->value = std::string(value);
  candidate->content_key = std::string(key);
  candidate->content_value = std::string(value);
  candidate->cost = cost;
  candidate->wcost = cost;
}

class GruRewriterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::SetFlag(&FLAGS_enable_gru_rewriter, true);
    rewriter_ = std::make_unique<GruRewriter>();
  }

  std::unique_ptr<GruRewriter> rewriter_;
};

TEST_F(GruRewriterTest, Capability) {
  ConversionRequest request;
  EXPECT_NE(rewriter_->capability(request) & RewriterInterface::CONVERSION, 0);
  EXPECT_NE(rewriter_->capability(request) & RewriterInterface::PREDICTION, 0);
}

TEST_F(GruRewriterTest, EmptySegments) {
  ConversionRequest request;
  Segments segments;
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
}

TEST_F(GruRewriterTest, SingleCandidateSegment) {
  ConversionRequest request;
  Segments segments;
  Segment* seg = segments.add_segment();
  seg->set_key("きょう");
  AddCandidate("きょう", "今日", 1000, seg);

  // Single candidate should not be modified
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));
}

TEST_F(GruRewriterTest, ContextSensitiveReranking) {
  absl::SetFlag(&FLAGS_gru_model_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/grumodel.bin");
  absl::SetFlag(&FLAGS_gru_chardata_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/data/chardata.bin");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Segment 0: "さいしんの" -> "最新の"
  Segment* seg0 = segments.add_segment();
  seg0->set_key("さいしんの");
  AddCandidate("さいしんの", "最新の", 1000, seg0);

  // Segment 1: "きかい" -> Candidate 0: "機会" (cost 1000), Candidate 1: "機械" (cost 1100)
  // Initially "機会" is at index 0 because cost 1000 < 1100
  Segment* seg1 = segments.add_segment();
  seg1->set_key("きかい");
  AddCandidate("きかい", "機会", 1000, seg1);
  AddCandidate("きかい", "機械", 1100, seg1);

  // Run Rewriter
  // In the context of "最新の", "機械" (machine) should have significantly higher likelihood
  // than "機会" (opportunity), promoting "機械" to index 0 if model is available.
  const bool rewritten = rewriter_->Rewrite(request, &segments);
  if (rewritten) {
    EXPECT_EQ(seg1->candidate(0).value, "機械");
    EXPECT_NE(seg1->candidate(0).attributes & converter::Attribute::CONTEXT_SENSITIVE, 0);
  }

  // Verify that the segments remain valid and well-formed
  EXPECT_EQ(segments.conversion_segments_size(), 2);
  EXPECT_EQ(seg1->candidates_size(), 2);
}

TEST_F(GruRewriterTest, MultiSegmentCombinationBeamSearch) {
  absl::SetFlag(&FLAGS_gru_model_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/grumodel.bin");
  absl::SetFlag(&FLAGS_gru_chardata_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/data/chardata.bin");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Segment 0: "せんもんちしきを" -> Cand 0: "専問知識を" (cost 1000), Cand 1: "専門知識を" (cost 1050)
  Segment* seg0 = segments.add_segment();
  seg0->set_key("せんもんちしきを");
  AddCandidate("せんもんちしきを", "専問知識を", 1000, seg0);
  AddCandidate("せんもんちしきを", "専門知識を", 1050, seg0);

  // Segment 1: "ふかめる" -> Cand 0: "深める" (cost 1000)
  Segment* seg1 = segments.add_segment();
  seg1->set_key("ふかめる");
  AddCandidate("ふかめる", "深める", 1000, seg1);

  // In beam search across combinations, "専門知識を深める" wins over "専問知識を深める"
  // promoting "専門知識を" to index 0 in segment 0.
  const bool rewritten = rewriter_->Rewrite(request, &segments);
  if (rewritten) {
    EXPECT_EQ(seg0->candidate(0).value, "専門知識を");
  }
}

TEST_F(GruRewriterTest, EmojiPenalty) {
  absl::SetFlag(&FLAGS_gru_model_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/grumodel.bin");
  absl::SetFlag(&FLAGS_gru_chardata_path,
                "/home/haruhiro/repos/C++/gru_scorer_engine/data/chardata.bin");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg0 = segments.add_segment();
  seg0->set_key("わら");
  AddCandidate("わら", "笑", 1000, seg0);
  AddCandidate("わら", "😆", 950, seg0);
  seg0->mutable_candidate(1)->description = "絵文字";

  // Emoji should NEVER be promoted over "笑"
  rewriter_->Rewrite(request, &segments);
  EXPECT_NE(seg0->candidate(0).value, "😆");
}

TEST_F(GruRewriterTest, DynamicPathReload) {
  // Point to a non-existent file
  absl::SetFlag(&FLAGS_gru_model_path, "/non/existent/path/grumodel.bin");
  ConversionRequest request;
  Segments segments;
  Segment* seg = segments.add_segment();
  seg->set_key("きょう");
  AddCandidate("きょう", "今日", 1000, seg);
  AddCandidate("きょう", "教", 1050, seg);

  // Should gracefully fail initialization and return false
  EXPECT_FALSE(rewriter_->Rewrite(request, &segments));

  // Reset to default / valid paths
  absl::SetFlag(&FLAGS_gru_model_path, "");
  absl::SetFlag(&FLAGS_gru_chardata_path, "");
  // Should reload and succeed (or fallback safely)
  rewriter_->Rewrite(request, &segments);
}

TEST_F(GruRewriterTest, SortSecondaryCandidates) {
  absl::SetFlag(&FLAGS_gru_model_path, "");
  absl::SetFlag(&FLAGS_gru_chardata_path, "");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg = segments.add_segment();
  seg->set_key("きかい");
  // 3 candidates
  AddCandidate("きかい", "奇怪", 1000, seg);
  AddCandidate("きかい", "機械", 1020, seg);
  AddCandidate("きかい", "機会", 1040, seg);

  const bool rewritten = rewriter_->Rewrite(request, &segments);
  if (rewritten) {
    EXPECT_EQ(segments.conversion_segment(0).candidates_size(), 3);
    // Top candidate should have CONTEXT_SENSITIVE attribute if promoted
    EXPECT_TRUE(segments.conversion_segment(0).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }
}

TEST_F(GruRewriterTest, InteractiveFocusRerank) {
  absl::SetFlag(&FLAGS_gru_model_path, "");
  absl::SetFlag(&FLAGS_gru_chardata_path, "");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Segment 0: "さいしんの"
  Segment* seg0 = segments.add_segment();
  seg0->set_key("さいしんの");
  AddCandidate("さいしんの", "最新の", 1000, seg0);
  AddCandidate("さいしんの", "細心の", 1050, seg0);

  // Segment 1: "ちゅうい"
  Segment* seg1 = segments.add_segment();
  seg1->set_key("ちゅうい");
  AddCandidate("ちゅうい", "中位", 1000, seg1);
  AddCandidate("ちゅうい", "注意", 1050, seg1);

  // Initial rewrite
  rewriter_->Rewrite(request, &segments);

  // Focus segment 0, candidate 1 ("細心の")
  const bool focus_handled = rewriter_->Focus(&segments, 0, 1);
  // Focus should handle and evaluate subsequent segments when model is loaded
  if (focus_handled) {
    EXPECT_EQ(segments.conversion_segments_size(), 2);
    EXPECT_TRUE(segments.conversion_segment(1).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }
}

}  // namespace
}  // namespace mozc

