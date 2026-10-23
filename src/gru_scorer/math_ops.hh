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

#include <cmath>
#include <cstddef>
#include <algorithm>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(_WIN32) || defined(_MSC_VER)
#include <intrin.h>
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

/**
 * @brief High-throughput Matrix-vector product using 4-row register tiling with AVX2 + FMA: y = W * x + b
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2,fma")))
#endif
inline void GemvAvx2Fma(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    size_t r = 0;

    // Process 4 rows simultaneously to hide FMA instruction latency (4 cycles)
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

    // Process remaining rows (1 row at a time)
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

inline float DotProduct(const float *a, const float *b, size_t n) noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    if (HasAvx2Fma() && n >= 8)
    {
        return DotProductAvx2Fma(a, b, n);
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
#endif
    GemvScalar(W, x, b, y, rows, cols);
}

/**
 * @brief Standard sigmoid activation function: 1 / (1 + exp(-x))
 */
inline float Sigmoid(float x) noexcept
{
    return 1.0f / (1.0f + std::exp(-x));
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
inline float ComputeLogSumExpAvx2(const float *in, size_t n) noexcept
{
    if (n == 0) return 0.0f;

    // Vectorized max reduction
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
#endif

    for (size_t i = 0; i < n; ++i)
    {
        out[i] = in[i] - log_denom;
    }
}

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
