#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace charindex
{
    class CharIndex;
}

namespace gruscorer
{

#pragma pack(push, 1)
struct ModelHeader
{
    char magic[4];        // "GRUM"
    uint32_t version;     // 1
    uint32_t vocab_size;  // e.g. 4096
    uint32_t hidden_size; // e.g. 64 or 128
    uint32_t data_type;   // 0: FP32
    uint8_t reserved[44]; // Padding to make header exactly 64 bytes
};
#pragma pack(pop)

static_assert(sizeof(ModelHeader) == 64, "ModelHeader must be exactly 64 bytes");

constexpr char kMagic[4] = {'G', 'R', 'U', 'M'};
constexpr uint32_t kCurrentVersion = 1;
constexpr uint32_t kDataTypeFp32 = 0;
constexpr uint16_t kUnkTokenId = 0;
constexpr uint16_t kBosTokenId = 1;

class GruModel
{
public:
    GruModel();
    ~GruModel();

    // Disable copy, allow move
    GruModel(const GruModel &) = delete;
    GruModel &operator=(const GruModel &) = delete;
    GruModel(GruModel &&) noexcept;
    GruModel &operator=(GruModel &&) noexcept;

    /**
     * @brief Load weights binary from file.
     * @return true on success, false otherwise.
     */
    bool LoadModel(const std::string &path);

    /**
     * @brief Execute single step of GRU inference.
     *
     * Given token_id and h_prev, computes the next hidden state h_next,
     * and optionally the log-probabilities over all vocab_size tokens.
     *
     * @param token_id Input token index (0 to vocab_size - 1)
     * @param h_prev Previous hidden state (array of size hidden_size, or nullptr for zeros)
     * @param h_next Output next hidden state (array of size hidden_size, must not be nullptr)
     * @param log_probs_out Output log probabilities (array of size vocab_size, can be nullptr if not needed)
     */
    void Step(uint16_t token_id,
              const float *h_prev,
              float *h_next,
              float *log_probs_out) const;

    /**
     * @brief Score a UTF-8 string by accumulating log P(c_t | c_{<t}).
     *
     * Starts with <bos> (token 1), iteratively decodes characters with CharIndex,
     * and accumulates the log probability of each next character.
     *
     * @param indexer CharIndex instance to map characters to IDs
     * @param utf8_str Input UTF-8 candidate string
     * @param initial_h Optional initial hidden state (size hidden_size). If nullptr, starts with zeros.
     * @param normalize If true, returns average log probability per character (total / token count).
     * @return Sum of log probabilities (or average if normalize is true).
     */
    float ScoreString(const charindex::CharIndex &indexer,
                      std::string_view utf8_str,
                      const float *initial_h = nullptr,
                      bool normalize = false) const;

    bool is_loaded() const noexcept { return is_loaded_; }
    uint32_t vocab_size() const noexcept { return vocab_size_; }
    uint32_t hidden_size() const noexcept { return hidden_size_; }

private:
    struct AlignedDeleter
    {
        void operator()(float *ptr) const noexcept;
    };

    bool is_loaded_ = false;
    uint32_t vocab_size_ = 0;
    uint32_t hidden_size_ = 0;

    std::unique_ptr<float[], AlignedDeleter> weights_buffer_;

    // Pointers into weights_buffer_
    const float *w_emb_ = nullptr; // [vocab_size, hidden_size]
    const float *w_ih_ = nullptr;  // [3 * hidden_size, hidden_size]
    const float *w_hh_ = nullptr;  // [3 * hidden_size, hidden_size]
    const float *b_ih_ = nullptr;  // [3 * hidden_size]
    const float *b_hh_ = nullptr;  // [3 * hidden_size]
    const float *w_out_ = nullptr; // [vocab_size, hidden_size]
    const float *b_out_ = nullptr; // [vocab_size]
};

} // namespace gruscorer

