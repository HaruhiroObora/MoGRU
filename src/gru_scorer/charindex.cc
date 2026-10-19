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

    const auto* raw_bytes = reinterpret_cast<const uint8_t*>(buffer_.get());
    char2Index_ = reinterpret_cast<const uint16_t*>(raw_bytes);
    index2Char_ = reinterpret_cast<const Utf8Entry*>(raw_bytes + 65536 * sizeof(uint16_t));

    return true;
}
