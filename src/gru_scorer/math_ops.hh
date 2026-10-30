// Copyright 2026, Haruhiro Obora.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Haruhiro Obora nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(_WIN32) || defined(_MSC_VER)
#include <intrin.h>
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#if defined(__linux__)
#if __has_include(<sys/auxv.h>)
#include <sys/auxv.h>
#endif
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
#endif
#endif

namespace gruscorer::math
{

/**
 * @brief Checks runtime CPU support for AVX2 and FMA extensions.
 */
inline bool HasAvx2Fma() noexcept
{
#if defined(_WIN32) || defined(_MSC_VER)
#if defined(_M_X64) || defined(__x86_64__)
    static const bool supported = []() noexcept -> bool {
        int cpu_info[4] = {0};
        __cpuid(cpu_info, 0);
        const int max_ids = cpu_info[0];
        if (max_ids < 7) return false;

        __cpuid(cpu_info, 1);
        // ECX: bit 12 = FMA, bit 27 = OSXSAVE, bit 28 = AVX
        const bool has_osxsave = (cpu_info[2] & (1 << 27)) != 0;
        const bool has_avx = (cpu_info[2] & (1 << 28)) != 0;
        const bool has_fma = (cpu_info[2] & (1 << 12)) != 0;
        if (!has_osxsave || !has_avx || !has_fma) return false;

        // Verify that OS supports AVX registers saving (XCR0 bits 1 and 2)
        const unsigned __int64 xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) return false;

        __cpuidex(cpu_info, 7, 0);
        // EBX: bit 5 = AVX2
        const bool has_avx2 = (cpu_info[1] & (1 << 5)) != 0;
        return has_avx2;
    }();
    return supported;
#else
    return false;
#endif
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(_M_X64))
    static const bool supported = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    return supported;
#else
    return false;
#endif
}

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief Checks runtime CPU support for ARMv8.2-A FP16 vector arithmetic.
 */
inline bool HasArmFp16() noexcept
{
#if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
    return true;
#elif defined(__APPLE__)
    return true; // All Apple Silicon Macs (M1+) support native FP16
#elif defined(__linux__) && defined(AT_HWCAP) && defined(HWCAP_ASIMDHP)
    static const bool supported = []() noexcept -> bool {
        unsigned long hw = getauxval(AT_HWCAP);
        return (hw & HWCAP_ASIMDHP) != 0;
    }();
    return supported;
#else
    return false;
#endif
}
#endif

/**
 * @brief Computes dot product of two float arrays with loop unrolling for ILP (Scalar fallback).
 */
inline float DotProductScalar(const float *a, const float *b, size_t n) noexcept
{
    float sum0 = 0.0f;
    float sum1 = 0.0f;
    float sum2 = 0.0f;
    float sum3 = 0.0f;

    size_t i = 0;
    for (; i + 4 <= n; i += 4)
    {
        sum0 += a[i + 0] * b[i + 0];
        sum1 += a[i + 1] * b[i + 1];
        sum2 += a[i + 2] * b[i + 2];
        sum3 += a[i + 3] * b[i + 3];
    }

    float total = (sum0 + sum1) + (sum2 + sum3);
    for (; i < n; ++i)
    {
        total += a[i] * b[i];
    }
    return total;
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline float HSum256(__m256 v) noexcept
{
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 s = _mm_add_ps(lo, hi);
    __m128 shuf = _mm_movehl_ps(s, s);
    s = _mm_add_ps(s, shuf);
    shuf = _mm_shuffle_ps(s, s, 1);
    s = _mm_add_ss(s, shuf);
    return _mm_cvtss_f32(s);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline float DotProductAvx2Fma(const float *a, const float *b, size_t n) noexcept
{
    size_t i = 0;
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();

    for (; i + 16 <= n; i += 16)
    {
        acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(&a[i]), _mm256_loadu_ps(&b[i]), acc0);
        acc1 = _mm256_fmadd_ps(_mm256_loadu_ps(&a[i + 8]), _mm256_loadu_ps(&b[i + 8]), acc1);
    }
    if (i + 8 <= n)
    {
        acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(&a[i]), _mm256_loadu_ps(&b[i]), acc0);
        i += 8;
    }

    float total = HSum256(_mm256_add_ps(acc0, acc1));
    for (; i < n; ++i)
    {
        total += a[i] * b[i];
    }
    return total;
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief ARM NEON DotProduct implementation using 16-float unrolling with FMA: dot = a · b
 */
inline float DotProductNeon(const float *a, const float *b, size_t n) noexcept
{
    size_t i = 0;
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    float32x4_t acc2 = vdupq_n_f32(0.0f);
    float32x4_t acc3 = vdupq_n_f32(0.0f);

    for (; i + 16 <= n; i += 16)
    {
        acc0 = vfmaq_f32(acc0, vld1q_f32(&a[i + 0]), vld1q_f32(&b[i + 0]));
        acc1 = vfmaq_f32(acc1, vld1q_f32(&a[i + 4]), vld1q_f32(&b[i + 4]));
        acc2 = vfmaq_f32(acc2, vld1q_f32(&a[i + 8]), vld1q_f32(&b[i + 8]));
        acc3 = vfmaq_f32(acc3, vld1q_f32(&a[i + 12]), vld1q_f32(&b[i + 12]));
    }
    for (; i + 4 <= n; i += 4)
    {
        acc0 = vfmaq_f32(acc0, vld1q_f32(&a[i]), vld1q_f32(&b[i]));
    }

    float32x4_t sum_vec = vaddq_f32(vaddq_f32(acc0, acc1), vaddq_f32(acc2, acc3));
    float total = vaddvq_f32(sum_vec);

    for (; i < n; ++i)
    {
        total += a[i] * b[i];
    }
    return total;
}
#endif

inline float DotProduct(const float *a, const float *b, size_t n) noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && n >= 8)
    {
        return DotProductAvx2Fma(a, b, n);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (n >= 4)
    {
        return DotProductNeon(a, b, n);
    }
#endif
    return DotProductScalar(a, b, n);
}

/**
 * @brief Matrix-vector product (Scalar fallback): y = W * x + b
 */
inline void GemvScalar(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    for (size_t r = 0; r < rows; ++r)
    {
        float val = DotProductScalar(&W[r * cols], x, cols);
        if (b != nullptr)
        {
            val += b[r];
        }
        y[r] = val;
    }
}

#if defined(__x86_64__) || defined(_M_X64)
/**
 * @brief High-throughput Matrix-vector product using 4-row register tiling with AVX2 + FMA: y = W * x + b
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline void GemvAvx2Fma(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    size_t r = 0;

    for (; r + 4 <= rows; r += 4)
    {
        const float *w0 = &W[(r + 0) * cols];
        const float *w1 = &W[(r + 1) * cols];
        const float *w2 = &W[(r + 2) * cols];
        const float *w3 = &W[(r + 3) * cols];

        __m256 acc0 = _mm256_setzero_ps();
        __m256 acc1 = _mm256_setzero_ps();
        __m256 acc2 = _mm256_setzero_ps();
        __m256 acc3 = _mm256_setzero_ps();

        size_t c = 0;
        for (; c + 8 <= cols; c += 8)
        {
            __m256 vx = _mm256_loadu_ps(&x[c]);
            acc0 = _mm256_fmadd_ps(_mm256_loadu_ps(&w0[c]), vx, acc0);
            acc1 = _mm256_fmadd_ps(_mm256_loadu_ps(&w1[c]), vx, acc1);
            acc2 = _mm256_fmadd_ps(_mm256_loadu_ps(&w2[c]), vx, acc2);
            acc3 = _mm256_fmadd_ps(_mm256_loadu_ps(&w3[c]), vx, acc3);
        }

        float s0 = HSum256(acc0);
        float s1 = HSum256(acc1);
        float s2 = HSum256(acc2);
        float s3 = HSum256(acc3);

        for (; c < cols; ++c)
        {
            const float xc = x[c];
            s0 += w0[c] * xc;
            s1 += w1[c] * xc;
            s2 += w2[c] * xc;
            s3 += w3[c] * xc;
        }

        if (b != nullptr)
        {
            s0 += b[r + 0];
            s1 += b[r + 1];
            s2 += b[r + 2];
            s3 += b[r + 3];
        }

        y[r + 0] = s0;
        y[r + 1] = s1;
        y[r + 2] = s2;
        y[r + 3] = s3;
    }

    for (; r < rows; ++r)
    {
        const float *wr = &W[r * cols];
        __m256 acc = _mm256_setzero_ps();
        size_t c = 0;
        for (; c + 8 <= cols; c += 8)
        {
            acc = _mm256_fmadd_ps(_mm256_loadu_ps(&wr[c]), _mm256_loadu_ps(&x[c]), acc);
        }
        float s = HSum256(acc);
        for (; c < cols; ++c)
        {
            s += wr[c] * x[c];
        }
        if (b != nullptr)
        {
            s += b[r];
        }
        y[r] = s;
    }
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief High-throughput Matrix-vector product using 8-row register tiling with ARM NEON: y = W * x + b
 * Leverages all 32 128-bit vector registers on ARM64 for maximum instruction-level parallelism.
 */
inline void GemvNeon(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    size_t r = 0;

    for (; r + 8 <= rows; r += 8)
    {
        const float *w0 = &W[(r + 0) * cols];
        const float *w1 = &W[(r + 1) * cols];
        const float *w2 = &W[(r + 2) * cols];
        const float *w3 = &W[(r + 3) * cols];
        const float *w4 = &W[(r + 4) * cols];
        const float *w5 = &W[(r + 5) * cols];
        const float *w6 = &W[(r + 6) * cols];
        const float *w7 = &W[(r + 7) * cols];

        float32x4_t acc0 = vdupq_n_f32(0.0f);
        float32x4_t acc1 = vdupq_n_f32(0.0f);
        float32x4_t acc2 = vdupq_n_f32(0.0f);
        float32x4_t acc3 = vdupq_n_f32(0.0f);
        float32x4_t acc4 = vdupq_n_f32(0.0f);
        float32x4_t acc5 = vdupq_n_f32(0.0f);
        float32x4_t acc6 = vdupq_n_f32(0.0f);
        float32x4_t acc7 = vdupq_n_f32(0.0f);

        size_t c = 0;
        for (; c + 4 <= cols; c += 4)
        {
            float32x4_t vx = vld1q_f32(&x[c]);
            acc0 = vfmaq_f32(acc0, vld1q_f32(&w0[c]), vx);
            acc1 = vfmaq_f32(acc1, vld1q_f32(&w1[c]), vx);
            acc2 = vfmaq_f32(acc2, vld1q_f32(&w2[c]), vx);
            acc3 = vfmaq_f32(acc3, vld1q_f32(&w3[c]), vx);
            acc4 = vfmaq_f32(acc4, vld1q_f32(&w4[c]), vx);
            acc5 = vfmaq_f32(acc5, vld1q_f32(&w5[c]), vx);
            acc6 = vfmaq_f32(acc6, vld1q_f32(&w6[c]), vx);
            acc7 = vfmaq_f32(acc7, vld1q_f32(&w7[c]), vx);
        }

        float s0 = vaddvq_f32(acc0);
        float s1 = vaddvq_f32(acc1);
        float s2 = vaddvq_f32(acc2);
        float s3 = vaddvq_f32(acc3);
        float s4 = vaddvq_f32(acc4);
        float s5 = vaddvq_f32(acc5);
        float s6 = vaddvq_f32(acc6);
        float s7 = vaddvq_f32(acc7);

        for (; c < cols; ++c)
        {
            const float xc = x[c];
            s0 += w0[c] * xc;
            s1 += w1[c] * xc;
            s2 += w2[c] * xc;
            s3 += w3[c] * xc;
            s4 += w4[c] * xc;
            s5 += w5[c] * xc;
            s6 += w6[c] * xc;
            s7 += w7[c] * xc;
        }

        if (b != nullptr)
        {
            s0 += b[r + 0];
            s1 += b[r + 1];
            s2 += b[r + 2];
            s3 += b[r + 3];
            s4 += b[r + 4];
            s5 += b[r + 5];
            s6 += b[r + 6];
            s7 += b[r + 7];
        }

        y[r + 0] = s0;
        y[r + 1] = s1;
        y[r + 2] = s2;
        y[r + 3] = s3;
        y[r + 4] = s4;
        y[r + 5] = s5;
        y[r + 6] = s6;
        y[r + 7] = s7;
    }

    for (; r + 4 <= rows; r += 4)
    {
        const float *w0 = &W[(r + 0) * cols];
        const float *w1 = &W[(r + 1) * cols];
        const float *w2 = &W[(r + 2) * cols];
        const float *w3 = &W[(r + 3) * cols];

        float32x4_t acc0 = vdupq_n_f32(0.0f);
        float32x4_t acc1 = vdupq_n_f32(0.0f);
        float32x4_t acc2 = vdupq_n_f32(0.0f);
        float32x4_t acc3 = vdupq_n_f32(0.0f);

        size_t c = 0;
        for (; c + 4 <= cols; c += 4)
        {
            float32x4_t vx = vld1q_f32(&x[c]);
            acc0 = vfmaq_f32(acc0, vld1q_f32(&w0[c]), vx);
            acc1 = vfmaq_f32(acc1, vld1q_f32(&w1[c]), vx);
            acc2 = vfmaq_f32(acc2, vld1q_f32(&w2[c]), vx);
            acc3 = vfmaq_f32(acc3, vld1q_f32(&w3[c]), vx);
        }

        float s0 = vaddvq_f32(acc0);
        float s1 = vaddvq_f32(acc1);
        float s2 = vaddvq_f32(acc2);
        float s3 = vaddvq_f32(acc3);

        for (; c < cols; ++c)
        {
            const float xc = x[c];
            s0 += w0[c] * xc;
            s1 += w1[c] * xc;
            s2 += w2[c] * xc;
            s3 += w3[c] * xc;
        }

        if (b != nullptr)
        {
            s0 += b[r + 0];
            s1 += b[r + 1];
            s2 += b[r + 2];
            s3 += b[r + 3];
        }

        y[r + 0] = s0;
        y[r + 1] = s1;
        y[r + 2] = s2;
        y[r + 3] = s3;
    }

    for (; r < rows; ++r)
    {
        const float *wr = &W[r * cols];
        float32x4_t acc = vdupq_n_f32(0.0f);
        size_t c = 0;
        for (; c + 4 <= cols; c += 4)
        {
            acc = vfmaq_f32(acc, vld1q_f32(&wr[c]), vld1q_f32(&x[c]));
        }
        float s = vaddvq_f32(acc);
        for (; c < cols; ++c)
        {
            s += wr[c] * x[c];
        }
        if (b != nullptr)
        {
            s += b[r];
        }
        y[r] = s;
    }
}

#if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC) || (defined(__clang__) || defined(__GNUC__))
#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("+fp16")))
#endif
inline void ConvertFp32ToFp16(const float *src, __fp16 *dst, size_t n) noexcept
{
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        float32x4_t v0 = vld1q_f32(&src[i + 0]);
        float32x4_t v1 = vld1q_f32(&src[i + 4]);
        float16x4_t h0 = vcvt_f16_f32(v0);
        float16x8_t h = vcombine_f16(h0, vcvt_f16_f32(v1));
        vst1q_f16(&dst[i], h);
    }
    for (; i + 4 <= n; i += 4)
    {
        float32x4_t v0 = vld1q_f32(&src[i]);
        vst1_f16(&dst[i], vcvt_f16_f32(v0));
    }
    for (; i < n; ++i)
    {
        dst[i] = static_cast<__fp16>(src[i]);
    }
}

#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("+fp16")))
#endif
inline void GemvNeonFp16(const __fp16 *W, const __fp16 *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    size_t r = 0;
    for (; r + 8 <= rows; r += 8)
    {
        const __fp16 *w0 = &W[(r + 0) * cols];
        const __fp16 *w1 = &W[(r + 1) * cols];
        const __fp16 *w2 = &W[(r + 2) * cols];
        const __fp16 *w3 = &W[(r + 3) * cols];
        const __fp16 *w4 = &W[(r + 4) * cols];
        const __fp16 *w5 = &W[(r + 5) * cols];
        const __fp16 *w6 = &W[(r + 6) * cols];
        const __fp16 *w7 = &W[(r + 7) * cols];

        float16x8_t acc0 = vdupq_n_f16(0.0f);
        float16x8_t acc1 = vdupq_n_f16(0.0f);
        float16x8_t acc2 = vdupq_n_f16(0.0f);
        float16x8_t acc3 = vdupq_n_f16(0.0f);
        float16x8_t acc4 = vdupq_n_f16(0.0f);
        float16x8_t acc5 = vdupq_n_f16(0.0f);
        float16x8_t acc6 = vdupq_n_f16(0.0f);
        float16x8_t acc7 = vdupq_n_f16(0.0f);

        size_t c = 0;
        for (; c + 8 <= cols; c += 8)
        {
            float16x8_t vx = vld1q_f16(&x[c]);
            acc0 = vfmaq_f16(acc0, vld1q_f16(&w0[c]), vx);
            acc1 = vfmaq_f16(acc1, vld1q_f16(&w1[c]), vx);
            acc2 = vfmaq_f16(acc2, vld1q_f16(&w2[c]), vx);
            acc3 = vfmaq_f16(acc3, vld1q_f16(&w3[c]), vx);
            acc4 = vfmaq_f16(acc4, vld1q_f16(&w4[c]), vx);
            acc5 = vfmaq_f16(acc5, vld1q_f16(&w5[c]), vx);
            acc6 = vfmaq_f16(acc6, vld1q_f16(&w6[c]), vx);
            acc7 = vfmaq_f16(acc7, vld1q_f16(&w7[c]), vx);
        }

        auto reduce_f16 = [](float16x8_t v) noexcept -> float {
            float16x4_t lo = vget_low_f16(v);
            float16x4_t hi = vget_high_f16(v);
            return vaddvq_f32(vcvt_f32_f16(vadd_f16(lo, hi)));
        };

        float s0 = reduce_f16(acc0);
        float s1 = reduce_f16(acc1);
        float s2 = reduce_f16(acc2);
        float s3 = reduce_f16(acc3);
        float s4 = reduce_f16(acc4);
        float s5 = reduce_f16(acc5);
        float s6 = reduce_f16(acc6);
        float s7 = reduce_f16(acc7);

        for (; c < cols; ++c)
        {
            float xc = static_cast<float>(x[c]);
            s0 += static_cast<float>(w0[c]) * xc;
            s1 += static_cast<float>(w1[c]) * xc;
            s2 += static_cast<float>(w2[c]) * xc;
            s3 += static_cast<float>(w3[c]) * xc;
            s4 += static_cast<float>(w4[c]) * xc;
            s5 += static_cast<float>(w5[c]) * xc;
            s6 += static_cast<float>(w6[c]) * xc;
            s7 += static_cast<float>(w7[c]) * xc;
        }

        if (b != nullptr)
        {
            s0 += b[r + 0];
            s1 += b[r + 1];
            s2 += b[r + 2];
            s3 += b[r + 3];
            s4 += b[r + 4];
            s5 += b[r + 5];
            s6 += b[r + 6];
            s7 += b[r + 7];
        }

        y[r + 0] = s0;
        y[r + 1] = s1;
        y[r + 2] = s2;
        y[r + 3] = s3;
        y[r + 4] = s4;
        y[r + 5] = s5;
        y[r + 6] = s6;
        y[r + 7] = s7;
    }

    for (; r < rows; ++r)
    {
        const __fp16 *wr = &W[r * cols];
        float16x8_t acc = vdupq_n_f16(0.0f);
        size_t c = 0;
        for (; c + 8 <= cols; c += 8)
        {
            acc = vfmaq_f16(acc, vld1q_f16(&wr[c]), vld1q_f16(&x[c]));
        }
        float16x4_t lo = vget_low_f16(acc);
        float16x4_t hi = vget_high_f16(acc);
        float s = vaddvq_f32(vcvt_f32_f16(vadd_f16(lo, hi)));
        for (; c < cols; ++c)
        {
            s += static_cast<float>(wr[c]) * static_cast<float>(x[c]);
        }
        if (b != nullptr)
        {
            s += b[r];
        }
        y[r] = s;
    }
}
#endif
#endif

/**
 * @brief Dynamic CPU-dispatched Matrix-vector product: y = W * x + b
 */
inline void Gemv(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && cols >= 8)
    {
        GemvAvx2Fma(W, x, b, y, rows, cols);
        return;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (cols >= 4)
    {
        GemvNeon(W, x, b, y, rows, cols);
        return;
    }
#endif
    GemvScalar(W, x, b, y, rows, cols);
}

/**
 * @brief Scalar fallback for y += alpha * x
 */
inline void AxpyScalar(float alpha, const float *x, float *y, size_t n) noexcept
{
    for (size_t i = 0; i < n; ++i)
    {
        y[i] += alpha * x[i];
    }
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline void AxpyAvx2Fma(float alpha, const float *x, float *y, size_t n) noexcept
{
    size_t i = 0;
    __m256 valpha = _mm256_set1_ps(alpha);
    for (; i + 8 <= n; i += 8)
    {
        __m256 vy = _mm256_loadu_ps(&y[i]);
        __m256 vx = _mm256_loadu_ps(&x[i]);
        vy = _mm256_fmadd_ps(valpha, vx, vy);
        _mm256_storeu_ps(&y[i], vy);
    }
    for (; i < n; ++i)
    {
        y[i] += alpha * x[i];
    }
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
inline void AxpyNeon(float alpha, const float *x, float *y, size_t n) noexcept
{
    size_t i = 0;
    float32x4_t valpha = vdupq_n_f32(alpha);
    for (; i + 16 <= n; i += 16)
    {
        float32x4_t vy0 = vld1q_f32(&y[i + 0]);
        float32x4_t vy1 = vld1q_f32(&y[i + 4]);
        float32x4_t vy2 = vld1q_f32(&y[i + 8]);
        float32x4_t vy3 = vld1q_f32(&y[i + 12]);

        vy0 = vfmaq_f32(vy0, valpha, vld1q_f32(&x[i + 0]));
        vy1 = vfmaq_f32(vy1, valpha, vld1q_f32(&x[i + 4]));
        vy2 = vfmaq_f32(vy2, valpha, vld1q_f32(&x[i + 8]));
        vy3 = vfmaq_f32(vy3, valpha, vld1q_f32(&x[i + 12]));

        vst1q_f32(&y[i + 0], vy0);
        vst1q_f32(&y[i + 4], vy1);
        vst1q_f32(&y[i + 8], vy2);
        vst1q_f32(&y[i + 12], vy3);
    }
    for (; i + 4 <= n; i += 4)
    {
        float32x4_t vy = vld1q_f32(&y[i]);
        vy = vfmaq_f32(vy, valpha, vld1q_f32(&x[i]));
        vst1q_f32(&y[i], vy);
    }
    for (; i < n; ++i)
    {
        y[i] += alpha * x[i];
    }
}
#endif

/**
 * @brief Dynamic CPU-dispatched vector accumulate: y += alpha * x
 */
inline void Axpy(float alpha, const float *x, float *y, size_t n) noexcept
{
    if (alpha == 0.0f) return;
#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && n >= 8)
    {
        AxpyAvx2Fma(alpha, x, y, n);
        return;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (n >= 4)
    {
        AxpyNeon(alpha, x, y, n);
        return;
    }
#endif
    AxpyScalar(alpha, x, y, n);
}

/**
 * @brief Transposed matrix-vector accumulate: y += W^T * x
 * where W is [rows, cols], x is [rows], and y is [cols].
 */
inline void GemvTransposedAdd(const float *W, const float *x, float *y, size_t rows, size_t cols) noexcept
{
    for (size_t r = 0; r < rows; ++r)
    {
        const float xr = x[r];
        if (xr != 0.0f)
        {
            Axpy(xr, &W[r * cols], y, cols);
        }
    }
}

/**
 * @brief Standard sigmoid activation function: 1 / (1 + exp(-x))
 */
inline float Sigmoid(float x) noexcept
{
    return 1.0f / (1.0f + std::exp(-x));
}

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief High-precision, branchless vectorized exponential approximation for ARM NEON.
 * Max relative error < 3.3e-6 across range [-87.3, 88.7].
 */
inline float32x4_t FastExpNeon(float32x4_t x) noexcept
{
    x = vmaxq_f32(x, vdupq_n_f32(-87.33654f));
    x = vminq_f32(x, vdupq_n_f32(88.722839f));

    const float32x4_t log2e = vdupq_n_f32(1.4426950408889634f);
    float32x4_t z = vmulq_f32(x, log2e);

    float32x4_t k_f = vrndnq_f32(z);
    int32x4_t k = vcvtq_s32_f32(k_f);

    const float32x4_t c1 = vdupq_n_f32(0.693359375f);
    const float32x4_t c2 = vdupq_n_f32(-2.1219444005469058e-4f);
    float32x4_t r = vmlsq_f32(x, k_f, c1);
    r = vmlsq_f32(r, k_f, c2);

    const float32x4_t p5 = vdupq_n_f32(0.008333333333333f);
    const float32x4_t p4 = vdupq_n_f32(0.041666666666666f);
    const float32x4_t p3 = vdupq_n_f32(0.166666666666666f);
    const float32x4_t p2 = vdupq_n_f32(0.5f);

    float32x4_t poly = vfmaq_f32(p4, p5, r);
    poly = vfmaq_f32(p3, poly, r);
    poly = vfmaq_f32(p2, poly, r);
    poly = vfmaq_f32(vdupq_n_f32(1.0f), poly, r);
    float32x4_t y = vfmaq_f32(vdupq_n_f32(1.0f), poly, r);

    int32x4_t exp_bits = vshlq_n_s32(vaddq_s32(k, vdupq_n_s32(127)), 23);
    float32x4_t pow2k = vreinterpretq_f32_s32(exp_bits);
    return vmulq_f32(y, pow2k);
}

/**
 * @brief Vectorized Sigmoid using FastExpNeon and accurate reciprocal refinement.
 */
inline float32x4_t FastSigmoidNeon(float32x4_t x) noexcept
{
    float32x4_t neg_x = vnegq_f32(x);
    float32x4_t exp_neg = FastExpNeon(neg_x);
    float32x4_t denom = vaddq_f32(vdupq_n_f32(1.0f), exp_neg);
    float32x4_t rec = vrecpeq_f32(denom);
    rec = vmulq_f32(vrecpsq_f32(denom, rec), rec);
    rec = vmulq_f32(vrecpsq_f32(denom, rec), rec);
    return rec;
}

/**
 * @brief Vectorized Tanh using FastSigmoidNeon: 2 * sigmoid(2x) - 1.
 */
inline float32x4_t FastTanhNeon(float32x4_t x) noexcept
{
    float32x4_t x2 = vmulq_n_f32(x, 2.0f);
    float32x4_t sig = FastSigmoidNeon(x2);
    return vsubq_f32(vmulq_n_f32(sig, 2.0f), vdupq_n_f32(1.0f));
}
#endif

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline float ComputeLogSumExpAvx2(const float *in, size_t n) noexcept
{
    if (n == 0) return 0.0f;

    size_t i = 0;
    __m256 max_vec = _mm256_set1_ps(in[0]);
    for (; i + 8 <= n; i += 8)
    {
        max_vec = _mm256_max_ps(max_vec, _mm256_loadu_ps(&in[i]));
    }
    __m128 max_lo = _mm256_castps256_ps128(max_vec);
    __m128 max_hi = _mm256_extractf128_ps(max_vec, 1);
    __m128 m128 = _mm_max_ps(max_lo, max_hi);
    __m128 shuf = _mm_movehl_ps(m128, m128);
    m128 = _mm_max_ps(m128, shuf);
    shuf = _mm_shuffle_ps(m128, m128, 1);
    m128 = _mm_max_ss(m128, shuf);
    float max_val = _mm_cvtss_f32(m128);

    for (; i < n; ++i)
    {
        if (in[i] > max_val)
        {
            max_val = in[i];
        }
    }

    float sum_exp = 0.0f;
    for (size_t j = 0; j < n; ++j)
    {
        sum_exp += std::exp(in[j] - max_val);
    }
    return max_val + std::log(sum_exp);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline void LogSoftmaxAvx2(const float *in, float *out, size_t n, float log_denom) noexcept
{
    size_t i = 0;
    __m256 vdenom = _mm256_set1_ps(log_denom);
    for (; i + 8 <= n; i += 8)
    {
        _mm256_storeu_ps(&out[i], _mm256_sub_ps(_mm256_loadu_ps(&in[i]), vdenom));
    }
    for (; i < n; ++i)
    {
        out[i] = in[i] - log_denom;
    }
}
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief Fully vectorized LogSumExp for ARM NEON using FastExpNeon.
 * Eliminates thousands of scalar std::exp calls.
 */
inline float ComputeLogSumExpNeon(const float *in, size_t n) noexcept
{
    if (n == 0) return 0.0f;

    size_t i = 0;
    float32x4_t max_vec = vdupq_n_f32(in[0]);
    for (; i + 4 <= n; i += 4)
    {
        max_vec = vmaxq_f32(max_vec, vld1q_f32(&in[i]));
    }
    float max_val = vmaxvq_f32(max_vec);
    for (; i < n; ++i)
    {
        if (in[i] > max_val)
        {
            max_val = in[i];
        }
    }

    float32x4_t vmax = vdupq_n_f32(max_val);
    float32x4_t sum_acc = vdupq_n_f32(0.0f);
    size_t j = 0;
    for (; j + 4 <= n; j += 4)
    {
        float32x4_t diff = vsubq_f32(vld1q_f32(&in[j]), vmax);
        sum_acc = vaddq_f32(sum_acc, FastExpNeon(diff));
    }

    float sum_exp = vaddvq_f32(sum_acc);
    for (; j < n; ++j)
    {
        sum_exp += std::exp(in[j] - max_val);
    }
    return max_val + std::log(sum_exp);
}

inline void LogSoftmaxNeon(const float *in, float *out, size_t n, float log_denom) noexcept
{
    size_t i = 0;
    float32x4_t vdenom = vdupq_n_f32(log_denom);
    for (; i + 4 <= n; i += 4)
    {
        vst1q_f32(&out[i], vsubq_f32(vld1q_f32(&in[i]), vdenom));
    }
    for (; i < n; ++i)
    {
        out[i] = in[i] - log_denom;
    }
}
#endif

inline float ComputeLogSumExpScalar(const float *in, size_t n) noexcept
{
    if (n == 0) return 0.0f;

    float max_val = in[0];
    for (size_t i = 1; i < n; ++i)
    {
        if (in[i] > max_val)
        {
            max_val = in[i];
        }
    }

    float sum_exp = 0.0f;
    for (size_t j = 0; j < n; ++j)
    {
        sum_exp += std::exp(in[j] - max_val);
    }
    return max_val + std::log(sum_exp);
}

/**
 * @brief Numerically stable LogSumExp: log(sum(exp(x_i)))
 */
inline float ComputeLogSumExp(const float *in, size_t n) noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && n >= 8)
    {
        return ComputeLogSumExpAvx2(in, n);
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (n >= 4)
    {
        return ComputeLogSumExpNeon(in, n);
    }
#endif
    return ComputeLogSumExpScalar(in, n);
}

/**
 * @brief Numerically stable Log-Softmax computation
 * log_prob[i] = in[i] - LogSumExp(in)
 */
inline void LogSoftmax(const float *in, float *out, size_t n) noexcept
{
    if (n == 0) return;

    const float log_denom = ComputeLogSumExp(in, n);

#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && n >= 8)
    {
        LogSoftmaxAvx2(in, out, n, log_denom);
        return;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (n >= 4)
    {
        LogSoftmaxNeon(in, out, n, log_denom);
        return;
    }
#endif

    for (size_t i = 0; i < n; ++i)
    {
        out[i] = in[i] - log_denom;
    }
}

#if defined(__aarch64__) || defined(_M_ARM64)
/**
 * @brief Vectorized GRU cell gate combinations & state update using ARM NEON.
 */
inline void GruCellUpdateNeon(const float *gi,
                              const float *gh,
                              const float *h_prev,
                              float *h_next,
                              size_t hidden_size) noexcept
{
    const float *gi_r = gi;
    const float *gi_z = gi + hidden_size;
    const float *gi_n = gi + 2 * hidden_size;

    const float *gh_r = gh;
    const float *gh_z = gh + hidden_size;
    const float *gh_n = gh + 2 * hidden_size;

    size_t j = 0;
    const float32x4_t v_one = vdupq_n_f32(1.0f);

    for (; j + 4 <= hidden_size; j += 4)
    {
        float32x4_t vr_in = vaddq_f32(vld1q_f32(&gi_r[j]), vld1q_f32(&gh_r[j]));
        float32x4_t vz_in = vaddq_f32(vld1q_f32(&gi_z[j]), vld1q_f32(&gh_z[j]));

        float32x4_t vr = FastSigmoidNeon(vr_in);
        float32x4_t vz = FastSigmoidNeon(vz_in);

        float32x4_t vn_in = vfmaq_f32(vld1q_f32(&gi_n[j]), vr, vld1q_f32(&gh_n[j]));
        float32x4_t vn = FastTanhNeon(vn_in);

        float32x4_t vhp = vld1q_f32(&h_prev[j]);
        float32x4_t v_one_minus_z = vsubq_f32(v_one, vz);
        float32x4_t vhn = vfmaq_f32(vmulq_f32(vz, vhp), v_one_minus_z, vn);

        vst1q_f32(&h_next[j], vhn);
    }

    for (; j < hidden_size; ++j)
    {
        const float r = Sigmoid(gi_r[j] + gh_r[j]);
        const float z = Sigmoid(gi_z[j] + gh_z[j]);
        const float n = std::tanh(gi_n[j] + r * gh_n[j]);
        h_next[j] = (1.0f - z) * n + z * h_prev[j];
    }
}
#endif

/**
 * @brief GRU gate combination & hidden state update (PyTorch formulation)
 *
 * r = sigmoid(gi[0:H] + gh[0:H])
 * z = sigmoid(gi[H:2H] + gh[H:2H])
 * n = tanh(gi[2H:3H] + r * gh[2H:3H])
 * h_next = (1 - z) * n + z * h_prev
 */
inline void GruCellUpdate(const float *gi,
                          const float *gh,
                          const float *h_prev,
                          float *h_next,
                          size_t hidden_size) noexcept
{
#if defined(__aarch64__) || defined(_M_ARM64)
    if (hidden_size >= 4)
    {
        GruCellUpdateNeon(gi, gh, h_prev, h_next, hidden_size);
        return;
    }
#endif

    const float *gi_r = gi;
    const float *gi_z = gi + hidden_size;
    const float *gi_n = gi + 2 * hidden_size;

    const float *gh_r = gh;
    const float *gh_z = gh + hidden_size;
    const float *gh_n = gh + 2 * hidden_size;

    for (size_t j = 0; j < hidden_size; ++j)
    {
        const float r = Sigmoid(gi_r[j] + gh_r[j]);
        const float z = Sigmoid(gi_z[j] + gh_z[j]);
        const float n = std::tanh(gi_n[j] + r * gh_n[j]);
        h_next[j] = (1.0f - z) * n + z * h_prev[j];
    }
}

} // namespace gruscorer::math
