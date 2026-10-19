#pragma once

#include <cmath>
#include <cstddef>
#include <algorithm>

namespace gruscorer::math
{

/**
 * @brief Computes dot product of two float arrays with loop unrolling for ILP and vectorization.
 */
inline float DotProduct(const float *a, const float *b, size_t n) noexcept
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

/**
 * @brief Matrix-vector product: y = W * x + b
 * @param W Row-major matrix of size [rows x cols]
 * @param x Vector of size [cols]
 * @param b Optional bias vector of size [rows] (can be nullptr)
 * @param y Output vector of size [rows]
 */
inline void Gemv(const float *W, const float *x, const float *b, float *y, size_t rows, size_t cols) noexcept
{
    for (size_t r = 0; r < rows; ++r)
    {
        float val = DotProduct(&W[r * cols], x, cols);
        if (b != nullptr)
        {
            val += b[r];
        }
        y[r] = val;
    }
}

/**
 * @brief Standard sigmoid activation function: 1 / (1 + exp(-x))
 */
inline float Sigmoid(float x) noexcept
{
    return 1.0f / (1.0f + std::exp(-x));
}

/**
 * @brief Numerically stable Log-Softmax computation
 * log_prob[i] = x[i] - (max_x + log(sum(exp(x[j] - max_x))))
 */
inline void LogSoftmax(const float *in, float *out, size_t n) noexcept
{
    if (n == 0) return;

    float max_val = in[0];
    for (size_t i = 1; i < n; ++i)
    {
        if (in[i] > max_val)
        {
            max_val = in[i];
        }
    }

    float sum_exp = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        sum_exp += std::exp(in[i] - max_val);
    }

    const float log_denom = max_val + std::log(sum_exp);
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

