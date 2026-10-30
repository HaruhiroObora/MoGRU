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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace gruscorer
{

/**
 * @brief Abstract GPU accelerator interface for GRU operations.
 *
 * Implements Stage 2 batched output layer GEMM, fused LogSumExp, target token
 * log probability scoring, and online learning gradient descent updates.
 */
class GruGpuBackend
{
public:
    virtual ~GruGpuBackend() = default;

    /**
     * @brief Returns whether the GPU backend was successfully initialized and has a valid device.
     */
    virtual bool IsAvailable() const noexcept = 0;

    /**
     * @brief Returns a human-readable name of the GPU device (e.g. "Intel Iris Xe Graphics").
     */
    virtual const char *GetDeviceName() const noexcept = 0;

    /**
     * @brief Returns the backend framework name ("OpenCL", "Metal", "CPU_Fallback").
     */
    virtual const char *GetBackendType() const noexcept = 0;

    /**
     * @brief Initialize device memory, buffers, and compile compute kernels for (vocab_size, hidden_size).
     */
    virtual bool Initialize(size_t vocab_size, size_t hidden_size) = 0;

    /**
     * @brief Upload full model weights to GPU memory buffers.
     */
    virtual bool UploadWeights(const float *w_emb,
                               const float *w_ih,
                               const float *w_hh,
                               const float *b_ih,
                               const float *b_hh,
                               const float *w_out,
                               const float *b_out) = 0;

    /**
     * @brief Synchronize active output weights (w_out and b_out) to GPU memory.
     */
    virtual bool SyncOutputWeights(const float *w_out, const float *b_out) = 0;

    /**
     * @brief Download active output weights from GPU memory to host memory.
     */
    virtual bool DownloadOutputWeights(float *w_out, float *b_out) = 0;

    /**
     * @brief Download all model weights from GPU memory to host memory.
     */
    virtual bool DownloadAllWeights(float *w_emb,
                                    float *w_ih,
                                    float *w_hh,
                                    float *b_ih,
                                    float *b_hh,
                                    float *w_out,
                                    float *b_out) = 0;

    /**
     * @brief Executes Stage 2 batched output layer computation for a sequence of T steps on GPU.
     *
     * Computes for each step t in 0..T-1:
     *   logits_t = W_out * h_seq[t] + b_out
     *   log_denom_t = LogSumExp(logits_t)
     *   step_log_probs[t] = logits_t[target_tokens[t]] - log_denom_t
     *
     * Executed as a single GPU kernel dispatch without per-step host-device round-trips.
     *
     * @param h_seq Host array of hidden states of size [T * hidden_size]
     * @param target_tokens Host array of target token IDs of size [T]
     * @param T Number of time steps in sequence
     * @param step_log_probs Output array of size [T] receiving log probabilities (can be nullptr)
     * @param total_log_prob_out Output pointer receiving the sum of step log probabilities
     * @return true on success, false on failure.
     */
    virtual bool ScoreSequenceOutputBatch(const float *h_seq,
                                          const uint16_t *target_tokens,
                                          size_t T,
                                          float *step_log_probs,
                                          float *total_log_prob_out) = 0;

    /**
     * @brief Executes Stage 2 online training step (Output-Only) on GPU.
     *
     * Computes forward logits, loss, cross-entropy gradients dl, accumulates
     * dW_out and db_out across all T steps, applies global gradient clipping
     * and L2 anchor SGD update directly on GPU memory.
     *
     * @param h_seq Host array of hidden states of size [T * hidden_size]
     * @param target_tokens Host array of target token IDs of size [T]
     * @param T Number of time steps in sequence
     * @param valid_targets Number of valid (non-<unk>) tokens
     * @param learning_rate SGD learning rate
     * @param weight_decay L2 anchor regularization towards base weights
     * @param max_grad_norm Gradient clipping threshold
     * @param base_w_out Base model anchor weights for W_out [V * H]
     * @param base_b_out Base model anchor weights for b_out [V]
     * @param total_log_prob_out Output pre-update average log probability
     * @return true on success, false on failure.
     */
    virtual bool TrainOutputOnlyBatch(const float *h_seq,
                                      const uint16_t *target_tokens,
                                      size_t T,
                                      size_t valid_targets,
                                      float learning_rate,
                                      float weight_decay,
                                      float max_grad_norm,
                                      const float *base_w_out,
                                      const float *base_b_out,
                                      float *total_log_prob_out) = 0;

    /**
     * @brief Factory function: detects and creates the best available GPU accelerator.
     *
     * Respects the MOGRU_USE_GPU environment variable:
     * - "0", "false", "off", "disable": Disables GPU, returns nullptr.
     * - "1", "true", "on", "force": Force enables GPU backend.
     * - "auto" (default): Enables GPU if a compatible device is detected.
     */
    static std::unique_ptr<GruGpuBackend> Create(size_t vocab_size, size_t hidden_size);
};

} // namespace gruscorer
