#include <fstream>
#include <memory>
#include <string>
#include "gru_scorer/charindex.hh"

using namespace charindex;

bool CharIndex::LoadFile(const std::string path)
{
    constexpr size_t expectedSize = 65536 * sizeof(uint16_t) + 4096 * sizeof(Utf8Entry);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    if (file.tellg() != expectedSize) return false;

    file.seekg(0, std::ios::beg);

    constexpr size_t num_uint32 = expectedSize / sizeof(uint32_t);
    buffer_ = std::make_unique_for_overwrite<uint32_t[]>(num_uint32);

    if (!file.read(reinterpret_cast<char*>(buffer_.get()), expectedSize))
    {
        buffer_.reset();
        return false;
    }

    auto* raw_bytes = reinterpret_cast<uint8_t*>(buffer_.get());
    auto* mutable_char2index = reinterpret_cast<uint16_t*>(raw_bytes);

    // Normalize Japanese punctuation marks:
    // Map fullwidth comma '，' (U+FF0C) and halfwidth comma '､' (U+FF64) to '、' (U+3001)
    // Map fullwidth full stop '．' (U+FF0E) and halfwidth full stop '｡' (U+FF61) to '。' (U+3002)
    mutable_char2index[0xFF0C] = mutable_char2index[0x3001];
    mutable_char2index[0xFF64] = mutable_char2index[0x3001];
    mutable_char2index[0xFF0E] = mutable_char2index[0x3002];
    mutable_char2index[0xFF61] = mutable_char2index[0x3002];

    char2Index_ = mutable_char2index;
    index2Char_ = reinterpret_cast<const Utf8Entry*>(raw_bytes + 65536 * sizeof(uint16_t));

    return true;
}
