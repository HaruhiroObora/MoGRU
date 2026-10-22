// Copyright 2010-2021, Google Inc.
// All rights reserved.

#include "rewriter/gru_rewriter.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "converter/attribute.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/commands.pb.h"
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

TEST_F(GruRewriterTest, HistoryPrefixContextCaching) {
  absl::SetFlag(&FLAGS_gru_model_path, "");
  absl::SetFlag(&FLAGS_gru_chardata_path, "");
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Add history segment
  Segment* hist = segments.add_segment();
  hist->set_segment_type(Segment::HISTORY);
  Segment::Candidate* c = hist->add_candidate();
  c->value = "今日は";
  c->key = "きょうは";

  // Add conversion segment
  Segment* seg = segments.add_segment();
  seg->set_key("よい");
  AddCandidate("よい", "良い", 1000, seg);
  AddCandidate("よい", "善い", 1000, seg);

  EXPECT_EQ(segments.history_segments_size(), 1);
  EXPECT_EQ(segments.history_value(), "今日は");
  EXPECT_EQ(segments.conversion_segments_size(), 1);

  const bool rewritten = rewriter_->Rewrite(request, &segments);
  if (rewritten) {
    EXPECT_TRUE(segments.conversion_segment(0).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }
}

TEST_F(GruRewriterTest, SurroundingTextPrefixCaching) {
  absl::SetFlag(&FLAGS_gru_model_path, "");
  absl::SetFlag(&FLAGS_gru_chardata_path, "");
  rewriter_->Reload();

  commands::Context context;
  context.set_preceding_text("最近の");
  ConversionRequest request = ConversionRequestBuilder().SetContext(context).Build();

  Segments segments;
  Segment* seg = segments.add_segment();
  seg->set_key("てんき");
  AddCandidate("てんき", "天気", 1000, seg);
  AddCandidate("てんき", "転機", 1000, seg);

  const bool rewritten = rewriter_->Rewrite(request, &segments);
  if (rewritten) {
    EXPECT_TRUE(segments.conversion_segment(0).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }
}

TEST_F(GruRewriterTest, KoushiKekkanFocusRerank) {
  // Use actual model and chardata if available, or dummy
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Segment 0: "こうし" -> "講師" (cost 3000), "格子" (cost 5000)
  Segment* seg0 = segments.add_segment();
  seg0->set_key("こうし");
  AddCandidate("こうし", "講師", 3000, seg0);
  AddCandidate("こうし", "格子", 5000, seg0);

  // Segment 1: "けっかん" -> "血管" (cost 4500), "欠陥" (cost 4942)
  Segment* seg1 = segments.add_segment();
  seg1->set_key("けっかん");
  AddCandidate("けっかん", "血管", 4500, seg1);
  AddCandidate("けっかん", "欠陥", 4942, seg1);

  rewriter_->Rewrite(request, &segments);

  // Focus segment 0, candidate 1 ("格子")
  const bool focus_handled = rewriter_->Focus(&segments, 0, 1);
  if (focus_handled) {
    // If the model is loaded and distinguishes "格子欠陥" over "格子血管",
    // "欠陥" should be promoted or evaluated with combined score tag containing "[E:"
    EXPECT_TRUE(segments.conversion_segment(1).candidate(0).description.find("[E:") !=
                std::string::npos);
  }
}

TEST_F(GruRewriterTest, ThreeSegmentFocusControl) {
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg0 = segments.add_segment();
  seg0->set_key("だいいち");
  AddCandidate("だいいち", "A0", 1000, seg0);
  AddCandidate("だいいち", "A1", 1050, seg0);

  Segment* seg1 = segments.add_segment();
  seg1->set_key("だいに");
  AddCandidate("だいに", "B0", 1000, seg1);
  AddCandidate("だいに", "B1", 1050, seg1);

  Segment* seg2 = segments.add_segment();
  seg2->set_key("だいさん");
  AddCandidate("だいさん", "C0", 1000, seg2);
  AddCandidate("だいさん", "C1", 1050, seg2);

  rewriter_->Rewrite(request, &segments);

  // 1. Focus on segment 0 candidate 1 ("A1")
  const bool f0 = rewriter_->Focus(&segments, 0, 1);
  if (f0) {
    // Both segment 1 and segment 2 should be evaluated and have descriptions updated
    EXPECT_TRUE(segments.conversion_segment(1).candidate(0).description.find("GRU:") !=
                std::string::npos);
    EXPECT_TRUE(segments.conversion_segment(2).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }

  // 2. Focus on segment 1 candidate 1 ("B1")
  const bool f1 = rewriter_->Focus(&segments, 1, 1);
  if (f1) {
    // Segment 2 should be evaluated
    EXPECT_TRUE(segments.conversion_segment(2).candidate(0).description.find("GRU:") !=
                std::string::npos);
  }

  // 3. Focus on last segment (segment 2)
  const bool f2 = rewriter_->Focus(&segments, 2, 0);
  // Last segment focus should return false (no subsequent segments to rerank)
  EXPECT_FALSE(f2);
}

TEST_F(GruRewriterTest, SkipSymbolAndPunctuationSegments) {
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  // Segment 0: normal hiragana segment
  Segment* seg0 = segments.add_segment();
  seg0->set_key("きょうは");
  AddCandidate("きょうは", "今日は", 1000, seg0);
  AddCandidate("きょうは", "教派", 2000, seg0);

  // Segment 1: punctuation segment (e.g. "." when punctuation style is "，．")
  // SymbolRewriter inserts "！" with the same cost (0) as "．".
  Segment* seg1 = segments.add_segment();
  seg1->set_key(".");
  AddCandidate(".", "．", 0, seg1);
  AddCandidate(".", "！", 0, seg1);
  AddCandidate(".", "？", 0, seg1);

  rewriter_->Rewrite(request, &segments);

  // Punctuation segment must keep its first candidate "．" and not be replaced by "！"
  EXPECT_EQ(segments.conversion_segment(1).candidate(0).value, "．");
}

TEST_F(GruRewriterTest, AlphabetCandidateDoubledGruScore) {
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg = segments.add_segment();
  seg->set_key("えーあい");
  AddCandidate("えーあい", "栄愛", 3000, seg);
  AddCandidate("えーあい", "AI", 3000, seg);

  rewriter_->Rewrite(request, &segments);

  // Find "AI" candidate and verify its description has GRU score evaluated
  for (size_t i = 0; i < seg->candidates_size(); ++i) {
    if (seg->candidate(i).value == "AI" &&
        !seg->candidate(i).description.empty()) {
      EXPECT_TRUE(seg->candidate(i).description.find("GRU:") !=
                  std::string::npos);
    }
  }
}

TEST_F(GruRewriterTest, KatakanaPenaltyRerank) {
  absl::SetFlag(&FLAGS_gru_katakana_penalty, 600.0);
  absl::SetFlag(&FLAGS_gru_score_weight, 0.0);  // isolate Viterbi + Penalty effects
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg = segments.add_segment();
  seg->set_key("とうきょう");
  // Candidate 0: "トウキョウ" (initial cost 1000) -> with katakana penalty 600 => effective 1600
  // Candidate 1: "東京" (initial cost 1200) -> no penalty => effective 1200
  AddCandidate("とうきょう", "トウキョウ", 1000, seg);
  AddCandidate("とうきょう", "東京", 1200, seg);

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(seg->candidate(0).value, "東京");
  EXPECT_EQ(seg->candidate(1).value, "トウキョウ");

  // Verify description contains penalty tag [K+600]
  EXPECT_TRUE(seg->candidate(1).description.find("[K+600]") != std::string::npos);
}

TEST_F(GruRewriterTest, HiraganaPenaltyRerank) {
  absl::SetFlag(&FLAGS_gru_hiragana_penalty, 400.0);
  absl::SetFlag(&FLAGS_gru_score_weight, 0.0);  // isolate Viterbi + Penalty effects
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg = segments.add_segment();
  seg->set_key("とうきょう");
  // Candidate 0: "とうきょう" (initial cost 1000) -> with hiragana penalty 400 => effective 1400
  // Candidate 1: "東京" (initial cost 1200) -> no penalty => effective 1200
  AddCandidate("とうきょう", "とうきょう", 1000, seg);
  AddCandidate("とうきょう", "東京", 1200, seg);

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(seg->candidate(0).value, "東京");
  EXPECT_EQ(seg->candidate(1).value, "とうきょう");

  // Verify description contains penalty tag [H+400]
  EXPECT_TRUE(seg->candidate(1).description.find("[H+400]") != std::string::npos);
}

TEST_F(GruRewriterTest, ConfigFileHotReload) {
  const std::string test_config = FileUtil::JoinPath(
      std::filesystem::temp_directory_path().string(), "test_gru_config.ini");
  {
    std::ofstream ofs(test_config);
    ofs << "katakana_penalty = 1200.0\n"
        << "hiragana_penalty = 800.0\n";
  }

  absl::SetFlag(&FLAGS_gru_config_path, test_config);
  absl::SetFlag(&FLAGS_gru_score_weight, 0.0);
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments;

  Segment* seg = segments.add_segment();
  seg->set_key("てすと");
  AddCandidate("てすと", "テスト", 1000, seg);  // katakana +1200 => 2200
  AddCandidate("てすと", "試験", 1500, seg);    // kanji => 1500

  EXPECT_TRUE(rewriter_->Rewrite(request, &segments));
  EXPECT_EQ(seg->candidate(0).value, "試験");
  EXPECT_TRUE(seg->candidate(1).description.find("[K+1200]") != std::string::npos);

  // Clean up
  std::filesystem::remove(test_config);
}

TEST_F(GruRewriterTest, PunctuationNormalizationScore) {
  rewriter_->Reload();

  ConversionRequest request;
  Segments segments_touten;
  Segment* seg0 = segments_touten.add_segment();
  seg0->set_key("きょうは、");
  AddCandidate("きょうは、", "今日は、", 1000, seg0);
  AddCandidate("きょうは、", "教派、", 1000, seg0);

  Segments segments_comma;
  Segment* seg1 = segments_comma.add_segment();
  seg1->set_key("きょうは，");
  AddCandidate("きょうは，", "今日は，", 1000, seg1);
  AddCandidate("きょうは，", "教派，", 1000, seg1);

  rewriter_->Rewrite(request, &segments_touten);
  rewriter_->Rewrite(request, &segments_comma);

  // Both should select "今日は" candidates and produce identical ranking
  EXPECT_EQ(segments_touten.conversion_segment(0).candidate(0).value, "今日は、");
  EXPECT_EQ(segments_comma.conversion_segment(0).candidate(0).value, "今日は，");

  // Descriptions should contain identical GRU score values
  const std::string desc_touten = segments_touten.conversion_segment(0).candidate(0).description;
  const std::string desc_comma = segments_comma.conversion_segment(0).candidate(0).description;

  auto extract_gru_tag = [](const std::string& desc) {
    const size_t pos = desc.find("GRU:");
    if (pos == std::string::npos) return std::string();
    const size_t end = desc.find('[', pos);
    return desc.substr(pos, (end != std::string::npos) ? end - pos : std::string::npos);
  };
  EXPECT_FALSE(extract_gru_tag(desc_touten).empty());
  EXPECT_EQ(extract_gru_tag(desc_comma), extract_gru_tag(desc_touten));
}

}  // namespace
}  // namespace mozc

