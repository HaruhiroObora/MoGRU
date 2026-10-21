#include "gru_scorer/gru_model.hh"
#include "gru_scorer/math_ops.hh"
#include "gru_scorer/charindex.hh"

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

void AssertNear(float actual, float expected, float tolerance = 1e-4f, const std::string &msg = "")
{
    float diff = std::fabs(actual - expected);
    if (diff > tolerance)
    {
        std::cerr << "Assertion failed: " << msg
                  << " actual=" << actual << " expected=" << expected
                  << " diff=" << diff << " (tolerance=" << tolerance << ")" << std::endl;
        std::abort();
    }
}

void TestMathOps()
{
    std::cout << "[Test] Running TestMathOps..." << std::endl;

    // 1. DotProduct
    const float a[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    const float b[] = {2.0f, 0.5f, -1.0f, 1.5f, 2.0f};
    // 1*2 + 2*0.5 + 3*(-1) + 4*1.5 + 5*2 = 2 + 1 - 3 + 6 + 10 = 16
    float dot = gruscorer::math::DotProduct(a, b, 5);
    AssertNear(dot, 16.0f, 1e-5f, "DotProduct");

    // 2. Gemv: y = W * x + bias
    // W = [[1, 2], [3, 4], [5, 6]] (3x2)
    // x = [0.5, -1.0]
    // b = [10, 20, 30]
    // y0 = 1*0.5 + 2*(-1.0) + 10 = -1.5 + 10 = 8.5
    // y1 = 3*0.5 + 4*(-1.0) + 20 = -2.5 + 20 = 17.5
    // y2 = 5*0.5 + 6*(-1.0) + 30 = -3.5 + 30 = 26.5
    const float W[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    const float x[] = {0.5f, -1.0f};
    const float bias[] = {10.0f, 20.0f, 30.0f};
    float y[3] = {0.0f};
    gruscorer::math::Gemv(W, x, bias, y, 3, 2);
    AssertNear(y[0], 8.5f, 1e-5f, "Gemv y[0]");
    AssertNear(y[1], 17.5f, 1e-5f, "Gemv y[1]");
    AssertNear(y[2], 26.5f, 1e-5f, "Gemv y[2]");

    // 3. Sigmoid
    AssertNear(gruscorer::math::Sigmoid(0.0f), 0.5f, 1e-5f, "Sigmoid(0)");

    // 4. LogSoftmax
    const float logits[] = {1.0f, 2.0f, 3.0f};
    float log_probs[3] = {0.0f};
    gruscorer::math::LogSoftmax(logits, log_probs, 3);
    // exp(1)+exp(2)+exp(3) = 2.71828 + 7.389056 + 20.085537 = 30.19287
    // log(30.19287) = 3.4076
    // log_probs = [1 - 3.4076 = -2.4076, 2 - 3.4076 = -1.4076, 3 - 3.4076 = -0.4076]
    AssertNear(log_probs[0], -2.4076f, 1e-3f, "LogSoftmax[0]");
    AssertNear(log_probs[1], -1.4076f, 1e-3f, "LogSoftmax[1]");
    AssertNear(log_probs[2], -0.4076f, 1e-3f, "LogSoftmax[2]");

    // Sum of exp(log_probs) must be 1.0
    float sum_p = std::exp(log_probs[0]) + std::exp(log_probs[1]) + std::exp(log_probs[2]);
    AssertNear(sum_p, 1.0f, 1e-5f, "LogSoftmax sum exp");

    // 5. Numerical consistency check: Scalar vs AVX2+FMA
#if defined(__x86_64__) || defined(_M_X64)
    if (gruscorer::math::HasAvx2Fma())
    {
        constexpr size_t kRows = 257;
        constexpr size_t kCols = 135;
        std::vector<float> big_W(kRows * kCols);
        std::vector<float> big_x(kCols);
        std::vector<float> big_b(kRows);
        std::vector<float> y_scalar(kRows);
        std::vector<float> y_avx2(kRows);

        for (size_t i = 0; i < big_W.size(); ++i)
        {
            big_W[i] = std::sin(static_cast<float>(i + 1) * 0.01f);
        }
        for (size_t i = 0; i < big_x.size(); ++i)
        {
            big_x[i] = std::cos(static_cast<float>(i + 1) * 0.02f);
        }
        for (size_t i = 0; i < big_b.size(); ++i)
        {
            big_b[i] = 0.1f * static_cast<float>(i % 10);
        }

        gruscorer::math::GemvScalar(big_W.data(), big_x.data(), big_b.data(), y_scalar.data(), kRows, kCols);
        gruscorer::math::GemvAvx2Fma(big_W.data(), big_x.data(), big_b.data(), y_avx2.data(), kRows, kCols);

        for (size_t r = 0; r < kRows; ++r)
        {
            AssertNear(y_avx2[r], y_scalar[r], 1e-4f, "Gemv AVX2 vs Scalar row " + std::to_string(r));
        }

        float lse_scalar = gruscorer::math::ComputeLogSumExpScalar(y_scalar.data(), kRows);
        float lse_avx2 = gruscorer::math::ComputeLogSumExpAvx2(y_scalar.data(), kRows);
        AssertNear(lse_avx2, lse_scalar, 1e-4f, "LogSumExp AVX2 vs Scalar");
    }
#endif

    std::cout << "[Test] TestMathOps passed." << std::endl;
}

#include <filesystem>

std::string FindDataFile(const std::string &filename)
{
    const std::vector<std::string> candidates = {
        filename,
        "gru_scorer/" + filename,
        "_main/gru_scorer/" + filename,
        "../gru_scorer/" + filename,
    };
    for (const auto &p : candidates)
    {
        if (std::filesystem::exists(p)) return p;
    }
    const char *test_srcdir = std::getenv("TEST_SRCDIR");
    if (test_srcdir)
    {
        std::string p = std::string(test_srcdir) + "/_main/gru_scorer/" + filename;
        if (std::filesystem::exists(p)) return p;
        p = std::string(test_srcdir) + "/gru_scorer/" + filename;
        if (std::filesystem::exists(p)) return p;
    }
    return filename;
}

void TestGruModelInference()
{
    std::cout << "[Test] Running TestGruModelInference..." << std::endl;

    gruscorer::GruModel model;
    std::string path = FindDataFile("dummy_model.bin");
    const bool loaded = model.LoadModel(path);
    (void)loaded;
    assert(loaded && "Failed to load dummy_model.bin");
    assert(model.is_loaded());
    assert(model.vocab_size() == 4096);
    assert(model.hidden_size() == 64);

    const size_t H = model.hidden_size();
    const size_t V = model.vocab_size();

    std::vector<float> h1(H, 0.0f);
    std::vector<float> log_probs1(V, 0.0f);

    // Step 1: token 1 (<bos>), h_prev = nullptr
    model.Step(1, nullptr, h1.data(), log_probs1.data());

    // Verify against Python NumPy reference values
    AssertNear(h1[0], -0.001625417f, 1e-5f, "Step1 h1[0]");
    AssertNear(h1[1], -0.010878378f, 1e-5f, "Step1 h1[1]");
    AssertNear(h1[2], -0.014900248f, 1e-5f, "Step1 h1[2]");
    AssertNear(h1[3], -0.008568134f, 1e-5f, "Step1 h1[3]");
    AssertNear(h1[4],  0.017188603f, 1e-5f, "Step1 h1[4]");

    AssertNear(log_probs1[0], -8.313534f, 1e-4f, "Step1 log_probs[0]");
    AssertNear(log_probs1[1], -8.329125f, 1e-4f, "Step1 log_probs[1]");
    AssertNear(log_probs1[2], -8.335196f, 1e-4f, "Step1 log_probs[2]");
    AssertNear(log_probs1[100], -8.157008f, 1e-4f, "Step1 log_probs[100]");

    // Verify StepWithTargetLogProb matches Step log_probs output exactly
    std::vector<float> h1_fast(H, 0.0f);
    for (uint16_t test_tok : {0, 1, 2, 50, 100, 500, 4000})
    {
        float target_lp = model.StepWithTargetLogProb(1, nullptr, h1_fast.data(), test_tok);
        AssertNear(target_lp, log_probs1[test_tok], 1e-4f, "StepWithTargetLogProb token " + std::to_string(test_tok));
        for (size_t j = 0; j < H; ++j)
        {
            AssertNear(h1_fast[j], h1[j], 1e-5f, "StepWithTargetLogProb h state");
        }
    }

    // Step 2: token 100, h_prev = h1
    std::vector<float> h2(H, 0.0f);
    std::vector<float> log_probs2(V, 0.0f);
    model.Step(100, h1.data(), h2.data(), log_probs2.data());

    AssertNear(h2[0], -0.014443655f, 1e-5f, "Step2 h2[0]");
    AssertNear(h2[1], -0.024245083f, 1e-5f, "Step2 h2[1]");
    AssertNear(h2[2],  0.000322970f, 1e-5f, "Step2 h2[2]");
    AssertNear(h2[3], -0.007451564f, 1e-5f, "Step2 h2[3]");
    AssertNear(h2[4],  0.045681491f, 1e-5f, "Step2 h2[4]");

    AssertNear(log_probs2[200], -8.254036f, 1e-4f, "Step2 log_probs[200]");

    std::cout << "[Test] TestGruModelInference passed! Precision strictly matches NumPy." << std::endl;
}

void TestCharIndexIntegration()
{
    std::cout << "[Test] Running TestCharIndexIntegration..." << std::endl;

    charindex::CharIndex indexer;
    std::string chardata_path = FindDataFile("chardata.bin");
    const bool loaded_char = indexer.LoadFile(chardata_path);
    (void)loaded_char;
    assert(loaded_char && "Failed to load chardata.bin");

    gruscorer::GruModel model;
    std::string model_path = FindDataFile("dummy_model.bin");
    const bool loaded_model = model.LoadModel(model_path);
    (void)loaded_model;
    assert(loaded_model && "Failed to load dummy_model.bin");

    std::string text = "今日は良い天気";
    float score = model.ScoreString(indexer, text);

    std::cout << "  - Text: \"" << text << "\", Log-likelihood Score: " << score << std::endl;
    assert(!std::isnan(score) && !std::isinf(score));
    assert(score < 0.0f);

    assert(model.ScoreString(indexer, "") == 0.0f);

    std::cout << "[Test] TestCharIndexIntegration passed!" << std::endl;
}

void TestGruContextCaching()
{
    std::cout << "[Test] Running TestGruContextCaching..." << std::endl;

    charindex::CharIndex indexer;
    std::string chardata_path = FindDataFile("chardata.bin");
    assert(indexer.LoadFile(chardata_path));

    gruscorer::GruModel model;
    std::string model_path = FindDataFile("dummy_model.bin");
    assert(model.LoadModel(model_path));

    const std::string full_text = "今日は良い天気";
    float score_full = model.ScoreString(indexer, full_text, nullptr, false);

    // 1. Empty context test
    gruscorer::GruContext empty_ctx;
    float score_empty_ctx = model.ScoreStringWithContext(indexer, full_text, empty_ctx, false);
    AssertNear(score_empty_ctx, score_full, 1e-5f, "Empty context scoring == full ScoreString");

    // 2. Empty string with context
    auto ctx_partial = model.ComputeContext(indexer, "今日は");
    AssertNear(model.ScoreStringWithContext(indexer, "", ctx_partial, false), 0.0f, 1e-5f, "Empty string with context");

    // 3. Mathematical consistency of chain rule:
    // log P(AB) = log P(A) + log P(B | A)
    const std::string prefix = "今日は";
    const std::string suffix = "良い天気";
    float score_prefix = model.ScoreString(indexer, prefix, nullptr, false);
    auto ctx = model.ComputeContext(indexer, prefix);
    assert(!ctx.is_empty);
    assert(ctx.h.size() == model.hidden_size());

    float score_suffix = model.ScoreStringWithContext(indexer, suffix, ctx, false);
    AssertNear(score_full, score_prefix + score_suffix, 1e-4f, "Chain rule log P(AB) == log P(A) + log P(B|A)");

    std::cout << "[Test] TestGruContextCaching passed! Chain rule strictly holds." << std::endl;
}

void BenchmarkInference()
{
    std::cout << "[Bench] Running BenchmarkInference..." << std::endl;

    gruscorer::GruModel model;
    std::string model_path = FindDataFile("dummy_model.bin");
    model.LoadModel(model_path);

    const size_t H = model.hidden_size();
    const size_t V = model.vocab_size();

    std::vector<float> h_prev(H, 0.0f);
    std::vector<float> h_next(H, 0.0f);
    std::vector<float> log_probs(V, 0.0f);

    // Warm up
    for (int i = 0; i < 100; ++i) {
        model.Step(1, h_prev.data(), h_next.data(), log_probs.data());
    }

    constexpr int kIterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kIterations; ++i)
    {
        uint16_t token = static_cast<uint16_t>((i % 100) + 1);
        model.Step(token, h_prev.data(), h_next.data(), log_probs.data());
        std::swap(h_prev, h_next);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double us_per_step = (total_ms * 1000.0) / kIterations;

    std::cout << "  - Executed " << kIterations << " steps in " << total_ms << " ms" << std::endl;
    std::cout << "  - Latency per character (Step + LogSoftmax): " << us_per_step << " microseconds" << std::endl;
    std::cout << "  - Throughput: " << static_cast<int>(1000000.0 / us_per_step) << " chars/sec" << std::endl;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Starting GRU Scorer Unit Tests & Benchmarks" << std::endl;
    std::cout << "========================================" << std::endl;

    TestMathOps();
    TestGruModelInference();
    TestCharIndexIntegration();
    TestGruContextCaching();
    BenchmarkInference();

    std::cout << "========================================" << std::endl;
    std::cout << "All Tests & Benchmarks Passed Successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
