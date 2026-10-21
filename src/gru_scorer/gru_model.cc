#include "gru_scorer/gru_model.hh"
#include "gru_scorer/math_ops.hh"
#include "gru_scorer/charindex.hh"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace gruscorer
{

void GruModel::AlignedDeleter::operator()(float *ptr) const noexcept
{
#if defined(_MSC_VER)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

GruModel::GruModel() = default;

GruModel::~GruModel() = default;

GruModel::GruModel(GruModel &&) noexcept = default;

GruModel &GruModel::operator=(GruModel &&) noexcept = default;

bool GruModel::LoadModel(const std::string &path)
{
    is_loaded_ = false;

    std::ifstream fin(path, std::ios::binary);
    if (!fin.is_open())
    {
        return false;
    }

    ModelHeader header;
    fin.read(reinterpret_cast<char *>(&header), sizeof(ModelHeader));
    if (fin.gcount() != sizeof(ModelHeader))
    {
        return false;
    }

    if (std::memcmp(header.magic, kMagic, 4) != 0)
    {
        return false;
    }

    if (header.version != kCurrentVersion || header.data_type != kDataTypeFp32)
    {
        return false;
    }

    vocab_size_ = header.vocab_size;
    hidden_size_ = header.hidden_size;

    const size_t H = hidden_size_;
    const size_t V = vocab_size_;

    const size_t size_emb = V * H;
    const size_t size_ih  = 3 * H * H;
    const size_t size_hh  = 3 * H * H;
    const size_t size_bih = 3 * H;
    const size_t size_bhh = 3 * H;
    const size_t size_out = V * H;
    const size_t size_bout = V;

    const size_t total_elements = size_emb + size_ih + size_hh + size_bih + size_bhh + size_out + size_bout;
    const size_t total_bytes = total_elements * sizeof(float);

    float *raw_buf = nullptr;
#if defined(_MSC_VER)
    raw_buf = static_cast<float *>(_aligned_malloc(total_bytes, 64));
#else
    if (posix_memalign(reinterpret_cast<void **>(&raw_buf), 64, total_bytes) != 0)
    {
        return false;
    }
#endif

    if (raw_buf == nullptr)
    {
        return false;
    }

    weights_buffer_.reset(raw_buf);

    fin.read(reinterpret_cast<char *>(raw_buf), total_bytes);
    if (static_cast<size_t>(fin.gcount()) != total_bytes)
    {
        weights_buffer_.reset();
        return false;
    }

    // Map weight pointers
    float *ptr = raw_buf;
    w_emb_ = ptr; ptr += size_emb;
    w_ih_  = ptr; ptr += size_ih;
    w_hh_  = ptr; ptr += size_hh;
    b_ih_  = ptr; ptr += size_bih;
    b_hh_  = ptr; ptr += size_bhh;
    w_out_ = ptr; ptr += size_out;
    b_out_ = ptr; ptr += size_bout;

    is_loaded_ = true;
    return true;
}

void GruModel::ForwardGruCell(uint16_t token_id,
                              const float *h_prev,
                              float *h_next) const
{
    if (token_id >= vocab_size_) [[unlikely]]
    {
        token_id = kUnkTokenId;
    }

    const size_t H = hidden_size_;
    const size_t H3 = 3 * H;

    constexpr size_t kMaxStackH3 = 768;
    alignas(64) float gi_stack[kMaxStackH3];
    alignas(64) float gh_stack[kMaxStackH3];

    std::vector<float> gi_heap;
    std::vector<float> gh_heap;
    float *gi = gi_stack;
    float *gh = gh_stack;

    if (H3 > kMaxStackH3) [[unlikely]]
    {
        gi_heap.resize(H3);
        gh_heap.resize(H3);
        gi = gi_heap.data();
        gh = gh_heap.data();
    }

    // 1. Lookup embedding for token_id
    const float *e_t = &w_emb_[static_cast<size_t>(token_id) * H];

    // 2. gi = W_ih * e_t + b_ih
    math::Gemv(w_ih_, e_t, b_ih_, gi, H3, H);

    // 3. gh = W_hh * h_prev + b_hh
    if (h_prev != nullptr)
    {
        math::Gemv(w_hh_, h_prev, b_hh_, gh, H3, H);
    }
    else
    {
        std::memcpy(gh, b_hh_, H3 * sizeof(float));
    }

    // 4. Update GRU gates and compute h_next
    alignas(64) float zero_h[256] = {0.0f};
    const float *prev_h_vec = h_prev;
    if (prev_h_vec == nullptr)
    {
        if (H <= 256)
        {
            prev_h_vec = zero_h;
        }
        else
        {
            gi_heap.assign(H, 0.0f);
            prev_h_vec = gi_heap.data();
        }
    }

    math::GruCellUpdate(gi, gh, prev_h_vec, h_next, H);
}

void GruModel::Step(uint16_t token_id,
                    const float *h_prev,
                    float *h_next,
                    float *log_probs_out) const
{
    if (!is_loaded_)
    {
        return;
    }

    ForwardGruCell(token_id, h_prev, h_next);

    // Output layer: log_probs = LogSoftmax(W_out * h_next + b_out)
    if (log_probs_out != nullptr)
    {
        math::Gemv(w_out_, h_next, b_out_, log_probs_out, vocab_size_, hidden_size_);
        math::LogSoftmax(log_probs_out, log_probs_out, vocab_size_);
    }
}

float GruModel::StepWithTargetLogProb(uint16_t token_id,
                                      const float *h_prev,
                                      float *h_next,
                                      uint16_t target_token) const
{
    if (!is_loaded_)
    {
        return 0.0f;
    }

    ForwardGruCell(token_id, h_prev, h_next);

    if (target_token >= vocab_size_) [[unlikely]]
    {
        target_token = kUnkTokenId;
    }

    constexpr size_t kMaxStackVocab = 4096;
    alignas(64) float logits_stack[kMaxStackVocab];
    std::vector<float> logits_heap;
    float *logits = logits_stack;

    if (vocab_size_ > kMaxStackVocab) [[unlikely]]
    {
        logits_heap.resize(vocab_size_);
        logits = logits_heap.data();
    }

    // Compute all logits for LogSumExp denominator
    math::Gemv(w_out_, h_next, b_out_, logits, vocab_size_, hidden_size_);

    const float log_denom = math::ComputeLogSumExp(logits, vocab_size_);
    return logits[target_token] - log_denom;
}

GruContext GruModel::ComputeContext(const charindex::CharIndex &indexer,
                                    std::string_view prefix) const
{
    GruContext ctx;
    if (!is_loaded_ || prefix.empty())
    {
        return ctx;
    }

    std::vector<uint16_t> token_ids;
    token_ids.reserve(prefix.size());

    std::string_view remaining = prefix;
    while (!remaining.empty())
    {
        size_t bytes_consumed = 0;
        char32_t cp = indexer.DecodeUtf8Char(remaining, bytes_consumed);
        if (bytes_consumed == 0)
        {
            break;
        }
        token_ids.push_back(indexer.Char2Index(cp));
        remaining.remove_prefix(bytes_consumed);
    }

    if (token_ids.empty())
    {
        return ctx;
    }

    const size_t H = hidden_size_;
    std::vector<float> h_buf1(H, 0.0f);
    std::vector<float> h_buf2(H, 0.0f);
    float *h_curr = h_buf1.data();
    float *h_next = h_buf2.data();

    uint16_t prev_token = kBosTokenId;
    for (size_t t = 0; t < token_ids.size(); ++t)
    {
        ForwardGruCell(prev_token, (t == 0) ? nullptr : h_curr, h_next);

        uint16_t target_token = token_ids[t];
        if (target_token >= vocab_size_) [[unlikely]]
        {
            target_token = kUnkTokenId;
        }
        prev_token = target_token;
        std::swap(h_curr, h_next);
    }

    ctx.h.assign(h_curr, h_curr + H);
    ctx.last_token = prev_token;
    ctx.is_empty = false;
    return ctx;
}

float GruModel::ScoreStringWithContext(const charindex::CharIndex &indexer,
                                       std::string_view utf8_str,
                                       const GruContext &context,
                                       bool normalize) const
{
    if (!is_loaded_ || utf8_str.empty())
    {
        return 0.0f;
    }

    if (context.is_empty || context.h.empty())
    {
        return ScoreString(indexer, utf8_str, nullptr, normalize);
    }

    std::vector<uint16_t> token_ids;
    token_ids.reserve(utf8_str.size());

    std::string_view remaining = utf8_str;
    while (!remaining.empty())
    {
        size_t bytes_consumed = 0;
        char32_t cp = indexer.DecodeUtf8Char(remaining, bytes_consumed);
        if (bytes_consumed == 0)
        {
            break;
        }
        token_ids.push_back(indexer.Char2Index(cp));
        remaining.remove_prefix(bytes_consumed);
    }

    if (token_ids.empty())
    {
        return 0.0f;
    }

    const size_t H = hidden_size_;
    std::vector<float> h_buf1(context.h);
    std::vector<float> h_buf2(H, 0.0f);
    float *h_curr = h_buf1.data();
    float *h_next = h_buf2.data();

    float total_log_prob = 0.0f;
    uint16_t prev_token = context.last_token;

    for (size_t t = 0; t < token_ids.size(); ++t)
    {
        uint16_t target_token = token_ids[t];
        if (target_token >= vocab_size_) [[unlikely]]
        {
            target_token = kUnkTokenId;
        }

        // Fast zero-store step: directly computes log P(target_token | history)
        float token_log_prob = StepWithTargetLogProb(prev_token, h_curr, h_next, target_token);

        if (target_token == kUnkTokenId)
        {
            token_log_prob = std::min(token_log_prob, -6.0f) - 6.0f;
        }

        total_log_prob += token_log_prob;
        prev_token = target_token;
        std::swap(h_curr, h_next);
    }

    if (normalize)
    {
        return total_log_prob / static_cast<float>(token_ids.size());
    }

    return total_log_prob;
}

float GruModel::ScoreString(const charindex::CharIndex &indexer,
                            std::string_view utf8_str,
                            const float *initial_h,
                            bool normalize) const
{
    if (!is_loaded_ || utf8_str.empty())
    {
        return 0.0f;
    }

    // Parse UTF-8 string into token IDs
    std::vector<uint16_t> token_ids;
    token_ids.reserve(utf8_str.size());

    std::string_view remaining = utf8_str;
    while (!remaining.empty())
    {
        size_t bytes_consumed = 0;
        char32_t cp = indexer.DecodeUtf8Char(remaining, bytes_consumed);
        if (bytes_consumed == 0)
        {
            break;
        }
        token_ids.push_back(indexer.Char2Index(cp));
        remaining.remove_prefix(bytes_consumed);
    }

    if (token_ids.empty())
    {
        return 0.0f;
    }

    const size_t H = hidden_size_;

    // Ping-pong buffers for hidden states
    std::vector<float> h_buf1(H, 0.0f);
    std::vector<float> h_buf2(H, 0.0f);
    float *h_curr = h_buf1.data();
    float *h_next = h_buf2.data();

    if (initial_h != nullptr)
    {
        std::memcpy(h_curr, initial_h, H * sizeof(float));
    }

    float total_log_prob = 0.0f;
    uint16_t prev_token = kBosTokenId;

    for (size_t t = 0; t < token_ids.size(); ++t)
    {
        uint16_t target_token = token_ids[t];
        if (target_token >= vocab_size_) [[unlikely]]
        {
            target_token = kUnkTokenId;
        }

        // Fast zero-store step: directly computes log P(target_token | history)
        float token_log_prob = StepWithTargetLogProb(
            prev_token,
            (t == 0 && initial_h == nullptr) ? nullptr : h_curr,
            h_next,
            target_token);

        if (target_token == kUnkTokenId)
        {
            token_log_prob = std::min(token_log_prob, -6.0f) - 6.0f;
        }

        total_log_prob += token_log_prob;
        prev_token = target_token;
        std::swap(h_curr, h_next);
    }

    if (normalize)
    {
        return total_log_prob / static_cast<float>(token_ids.size());
    }

    return total_log_prob;
}

} // namespace gruscorer
