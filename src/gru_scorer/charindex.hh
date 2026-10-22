#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>

namespace charindex
{
    struct alignas(4) Utf8Entry
    {
        uint8_t len;
        char bytes[3];
    };

    class CharIndex
    {
    public:
        CharIndex() = default;
        ~CharIndex() = default;
        bool LoadFile(const std::string path);
        inline char32_t DecodeUtf8Char(const std::string_view sv, size_t &bytes_consumed) const;
        inline uint16_t Char2Index(const char32_t c) const;
        inline std::string_view Index2String(const uint16_t idx) const;

    private:
        std::unique_ptr<uint32_t[]> buffer_;

        const uint16_t *char2Index_ = nullptr;
        const Utf8Entry *index2Char_ = nullptr;
    };

    // implementation of inline functions

    inline char32_t CharIndex::DecodeUtf8Char(const std::string_view sv, size_t &bytes_consumed) const
    {
        if (sv.empty())
        {
            bytes_consumed = 0;
            return 0;
        }

        const uint8_t c0 = static_cast<uint8_t>(sv[0]);

        if (c0 < 0x80)
        { // 1バイト
            bytes_consumed = 1;
            return c0;
        }
        if ((c0 & 0xE0) == 0xC0 && sv.size() >= 2)
        { // 2バイト
            bytes_consumed = 2;
            return ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(sv[1]) & 0x3F);
        }
        if ((c0 & 0xF0) == 0xE0 && sv.size() >= 3)
        { // 3バイト (日本語の大半)
            bytes_consumed = 3;
            return ((c0 & 0x0F) << 12) |
                   ((static_cast<uint8_t>(sv[1]) & 0x3F) << 6) |
                   (static_cast<uint8_t>(sv[2]) & 0x3F);
        }
        if ((c0 & 0xF8) == 0xF0 && sv.size() >= 4)
        { // 4バイト (絵文字、補助漢字等)
            bytes_consumed = 4;
            return ((c0 & 0x07) << 18) |
                   ((static_cast<uint8_t>(sv[1]) & 0x3F) << 12) |
                   ((static_cast<uint8_t>(sv[2]) & 0x3F) << 6) |
                   (static_cast<uint8_t>(sv[3]) & 0x3F);
        }

        bytes_consumed = 1;
        return 0xFFFD; // 不正なバイト列
    }

    inline bool IsEmojiCodePoint(const char32_t c)
    {
        return (c >= 0x1F000 && c <= 0x1FAFF) ||
               (c >= 0x2600 && c <= 0x27BF) ||
               (c >= 0x2B50 && c <= 0x2B55) ||
               (c >= 0xFE00 && c <= 0xFE0F);
    }

    inline uint16_t CharIndex::Char2Index(const char32_t c) const
    {
        if (c > 65535) [[unlikely]] return 0;
        if (!char2Index_) [[unlikely]] return 0;
        if (c == 0xFF0C || c == 0xFF64) [[unlikely]] return char2Index_[0x3001]; // '，' / '､' -> '、'
        if (c == 0xFF0E || c == 0xFF61) [[unlikely]] return char2Index_[0x3002]; // '．' / '｡' -> '。'
        return char2Index_[c];
    }

    inline std::string_view CharIndex::Index2String(const uint16_t idx) const
    {
        if (idx > 4095)
        {
            const auto& e = index2Char_[0];
            return {e.bytes, e.len};
        }
        const auto& e = index2Char_[idx];
        return {e.bytes, e.len};
    }
}