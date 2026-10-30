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

#include "gru_scorer/gpu_backend.hh"
#include "gru_scorer/math_ops.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#endif

namespace gruscorer
{
namespace
{

// ============================================================================
// OpenCL Standard Types & API Signatures (Self-contained, zero SDK dependency)
// ============================================================================

using cl_int = int32_t;
using cl_uint = uint32_t;
using cl_ulong = uint64_t;
using cl_bitfield = cl_ulong;
using cl_platform_id = void *;
using cl_device_id = void *;
using cl_context = void *;
using cl_command_queue = void *;
using cl_mem = void *;
using cl_program = void *;
using cl_kernel = void *;
using cl_event = void *;
using cl_device_type = cl_bitfield;
using cl_mem_flags = cl_bitfield;
using cl_command_queue_properties = cl_bitfield;
using cl_platform_info = cl_uint;
using cl_device_info = cl_uint;
using cl_program_build_info = cl_uint;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_device_type CL_DEVICE_TYPE_DEFAULT = (1 << 0);
constexpr cl_device_type CL_DEVICE_TYPE_CPU = (1 << 1);
constexpr cl_device_type CL_DEVICE_TYPE_GPU = (1 << 2);
constexpr cl_device_type CL_DEVICE_TYPE_ALL = 0xFFFFFFFF;

constexpr cl_platform_info CL_PLATFORM_NAME = 0x0902;
constexpr cl_device_info CL_DEVICE_NAME = 0x102B;
constexpr cl_device_info CL_DEVICE_MAX_WORK_GROUP_SIZE = 0x1004;

constexpr cl_mem_flags CL_MEM_READ_WRITE = (1 << 0);
constexpr cl_mem_flags CL_MEM_WRITE_ONLY = (1 << 1);
constexpr cl_mem_flags CL_MEM_READ_ONLY = (1 << 2);
constexpr cl_mem_flags CL_MEM_COPY_HOST_PTR = (1 << 5);

constexpr cl_program_build_info CL_PROGRAM_BUILD_LOG = 0x1183;
constexpr cl_uint CL_TRUE = 1;
constexpr cl_uint CL_FALSE = 0;

struct OpenClFunctions
{
    cl_int (*GetPlatformIDs)(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms) = nullptr;
    cl_int (*GetPlatformInfo)(cl_platform_id platform, cl_platform_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret) = nullptr;
    cl_int (*GetDeviceIDs)(cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id *devices, cl_uint *num_devices) = nullptr;
    cl_int (*GetDeviceInfo)(cl_device_id device, cl_device_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret) = nullptr;
    cl_context (*CreateContext)(const void *properties, cl_uint num_devices, const cl_device_id *devices, void (*pfn_notify)(const char *, const void *, size_t, void *), void *user_data, cl_int *errcode_ret) = nullptr;
    cl_int (*ReleaseContext)(cl_context context) = nullptr;
    cl_command_queue (*CreateCommandQueue)(cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int *errcode_ret) = nullptr;
    cl_int (*ReleaseCommandQueue)(cl_command_queue command_queue) = nullptr;
    cl_mem (*CreateBuffer)(cl_context context, cl_mem_flags flags, size_t size, void *host_ptr, cl_int *errcode_ret) = nullptr;
    cl_int (*ReleaseMemObject)(cl_mem memobj) = nullptr;
    cl_int (*EnqueueWriteBuffer)(cl_command_queue command_queue, cl_mem buffer, cl_uint blocking_write, size_t offset, size_t size, const void *ptr, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event) = nullptr;
    cl_int (*EnqueueReadBuffer)(cl_command_queue command_queue, cl_mem buffer, cl_uint blocking_read, size_t offset, size_t size, void *ptr, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event) = nullptr;
    cl_program (*CreateProgramWithSource)(cl_context context, cl_uint count, const char **strings, const size_t *lengths, cl_int *errcode_ret) = nullptr;
    cl_int (*BuildProgram)(cl_program program, cl_uint num_devices, const cl_device_id *device_list, const char *options, void (*pfn_notify)(cl_program, void *), void *user_data) = nullptr;
    cl_int (*GetProgramBuildInfo)(cl_program program, cl_device_id device, cl_program_build_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret) = nullptr;
    cl_int (*ReleaseProgram)(cl_program program) = nullptr;
    cl_kernel (*CreateKernel)(cl_program program, const char *kernel_name, cl_int *errcode_ret) = nullptr;
    cl_int (*SetKernelArg)(cl_kernel kernel, cl_uint arg_index, size_t arg_size, const void *arg_value) = nullptr;
    cl_int (*ReleaseKernel)(cl_kernel kernel) = nullptr;
    cl_int (*EnqueueNDRangeKernel)(cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim, const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event) = nullptr;
    cl_int (*Finish)(cl_command_queue command_queue) = nullptr;
};

class OpenClDynamicLoader
{
public:
    static OpenClDynamicLoader &Instance()
    {
        static OpenClDynamicLoader instance;
        return instance;
    }

    bool IsLoaded() const noexcept { return is_loaded_; }
    const OpenClFunctions &Fn() const noexcept { return fn_; }

private:
    OpenClDynamicLoader()
    {
        LoadLibrarySymbols();
    }

    ~OpenClDynamicLoader()
    {
#if defined(_WIN32) || defined(_MSC_VER)
        if (module_) FreeLibrary(static_cast<HMODULE>(module_));
#else
        if (module_) dlclose(module_);
#endif
    }

    void *module_ = nullptr;
    bool is_loaded_ = false;
    OpenClFunctions fn_{};

    void *GetSymbol(const char *name)
    {
#if defined(_WIN32) || defined(_MSC_VER)
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(module_), name));
#else
        return dlsym(module_, name);
#endif
    }

    void LoadLibrarySymbols()
    {
#if defined(_WIN32) || defined(_MSC_VER)
        module_ = LoadLibraryW(L"OpenCL.dll");
        if (!module_)
        {
            return;
        }
#elif defined(__APPLE__)
        module_ = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
        if (!module_)
        {
            module_ = dlopen("libOpenCL.dylib", RTLD_NOW | RTLD_LOCAL);
        }
        if (!module_)
        {
            return;
        }
#else
        module_ = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!module_)
        {
            module_ = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (!module_)
        {
            return;
        }
#endif

#define BIND_CL(name)                                                         \
    fn_.name = reinterpret_cast<decltype(fn_.name)>(GetSymbol("cl" #name)); \
    if (!fn_.name) { return; }

        BIND_CL(GetPlatformIDs);
        BIND_CL(GetPlatformInfo);
        BIND_CL(GetDeviceIDs);
        BIND_CL(GetDeviceInfo);
        BIND_CL(CreateContext);
        BIND_CL(ReleaseContext);
        BIND_CL(CreateCommandQueue);
        BIND_CL(ReleaseCommandQueue);
        BIND_CL(CreateBuffer);
        BIND_CL(ReleaseMemObject);
        BIND_CL(EnqueueWriteBuffer);
        BIND_CL(EnqueueReadBuffer);
        BIND_CL(CreateProgramWithSource);
        BIND_CL(BuildProgram);
        BIND_CL(GetProgramBuildInfo);
        BIND_CL(ReleaseProgram);
        BIND_CL(CreateKernel);
        BIND_CL(SetKernelArg);
        BIND_CL(ReleaseKernel);
        BIND_CL(EnqueueNDRangeKernel);
        BIND_CL(Finish);

#undef BIND_CL

        is_loaded_ = true;
    }
};

// ============================================================================
// OpenCL Kernel Source Codes (JIT compiled for any GPU hardware)
// ============================================================================

const char *kOpenClKernelSource = R"CLC(
// Stage 2: Batched Output GEMM + LogSumExp + Target LogProb Kernel
// Each work-group (size WG_SIZE) handles 1 time-step t in 0..T-1.
__kernel void BatchedOutputLogProb(
    __global const float *h_seq,          // [T, hidden_size]
    __global const float *w_out,          // [vocab_size, hidden_size]
    __global const float *b_out,          // [vocab_size]
    __global const ushort *target_tokens, // [T]
    __global float *step_log_probs,       // [T]
    __global float *logits_storage,       // [T, vocab_size]
    const uint vocab_size,
    const uint hidden_size)
{
    const uint t = get_group_id(0);
    const uint lid = get_local_id(0);
    const uint wg_size = get_local_size(0);

    __global const float *h_t = h_seq + t * hidden_size;
    __global float *logits_t = logits_storage + t * vocab_size;

    // 1. Parallel compute logits: logits[v] = W_out[v] · h_t + b_out[v]
    float local_max = -1e30f;
    for (uint v = lid; v < vocab_size; v += wg_size)
    {
        float sum = b_out[v];
        __global const float *wv = w_out + v * hidden_size;
        for (uint c = 0; c < hidden_size; ++c)
        {
            sum += wv[c] * h_t[c];
        }
        logits_t[v] = sum;
        if (sum > local_max)
        {
            local_max = sum;
        }
    }

    // 2. Parallel Work-Group Max Reduction in local memory
    __local float s_reduce[256];
    s_reduce[lid] = local_max;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint s = wg_size / 2; s > 0; s >>= 1)
    {
        if (lid < s)
        {
            if (s_reduce[lid + s] > s_reduce[lid])
            {
                s_reduce[lid] = s_reduce[lid + s];
            }
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    const float grp_max = s_reduce[0];
    barrier(CLK_LOCAL_MEM_FENCE);

    // 3. Parallel Compute sum(exp(logits - max))
    float local_sum_exp = 0.0f;
    for (uint v = lid; v < vocab_size; v += wg_size)
    {
        local_sum_exp += exp(logits_t[v] - grp_max);
    }

    s_reduce[lid] = local_sum_exp;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (uint s = wg_size / 2; s > 0; s >>= 1)
    {
        if (lid < s)
        {
            s_reduce[lid] += s_reduce[lid + s];
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    const float grp_sum_exp = s_reduce[0];
    const float log_denom = grp_max + log(grp_sum_exp);

    // 4. Thread 0 writes the target token log probability
    if (lid == 0)
    {
        uint target = (uint)target_tokens[t];
        if (target >= vocab_size)
        {
            target = 0; // kUnkTokenId
        }
        float target_lp = logits_t[target] - log_denom;
        if (target == 0)
        {
            target_lp = fmin(target_lp, -6.0f) - 6.0f;
        }
        step_log_probs[t] = target_lp;
    }
}

// Stage 2: Online Training Softmax Gradient & Output Weight SGD Update Kernel
__kernel void TrainOutputUpdate(
    __global const float *h_seq,          // [T, hidden_size]
    __global const float *logits_storage, // [T, vocab_size]
    __global const ushort *target_tokens, // [T]
    __global float *w_out,                // [vocab_size, hidden_size]
    __global float *b_out,                // [vocab_size]
    __global const float *base_w_out,     // [vocab_size, hidden_size]
    __global const float *base_b_out,     // [vocab_size]
    const uint T,
    const uint valid_targets,
    const uint vocab_size,
    const uint hidden_size,
    const float eff_lr,
    const float anchor_decay)
{
    const uint v = get_global_id(0);
    if (v >= vocab_size) return;

    const float inv_valid = 1.0f / (float)valid_targets;

    // Accumulate gradients across all T steps for vocabulary element v
    float g_bv = 0.0f;
    for (uint t = 0; t < T; ++t)
    {
        uint target = (uint)target_tokens[t];
        if (target != 0) // only valid non-<unk> targets
        {
            __global const float *logits_t = logits_storage + t * vocab_size;
            // log_denom was computed in previous step, but we can compute p_v directly
            // from logits if available, or compute dL
        }
    }
}
)CLC";

// ============================================================================
// CPU Fallback Implementation
// ============================================================================

class CpuFallbackBackend final : public GruGpuBackend
{
public:
    CpuFallbackBackend() = default;
    ~CpuFallbackBackend() override = default;

    bool IsAvailable() const noexcept override { return true; }
    const char *GetDeviceName() const noexcept override { return "Host CPU (SIMD)"; }
    const char *GetBackendType() const noexcept override { return "CPU_Fallback"; }

    bool Initialize(size_t vocab_size, size_t hidden_size) override
    {
        vocab_size_ = vocab_size;
        hidden_size_ = hidden_size;
        return true;
    }

    bool UploadWeights(const float *, const float *, const float *, const float *, const float *, const float *w_out, const float *b_out) override
    {
        cached_w_out_ = const_cast<float *>(w_out);
        cached_b_out_ = const_cast<float *>(b_out);
        return true;
    }

    bool SyncOutputWeights(const float *w_out, const float *b_out) override
    {
        cached_w_out_ = const_cast<float *>(w_out);
        cached_b_out_ = const_cast<float *>(b_out);
        return true;
    }

    bool DownloadOutputWeights(float *w_out, float *b_out) override
    {
        if (cached_w_out_ && w_out && cached_w_out_ != w_out)
        {
            std::memcpy(w_out, cached_w_out_, vocab_size_ * hidden_size_ * sizeof(float));
        }
        if (cached_b_out_ && b_out && cached_b_out_ != b_out)
        {
            std::memcpy(b_out, cached_b_out_, vocab_size_ * sizeof(float));
        }
        return true;
    }

    bool DownloadAllWeights(float *, float *, float *, float *, float *, float *, float *) override
    {
        return true;
    }

    bool ScoreSequenceOutputBatch(const float *h_seq,
                                  const uint16_t *target_tokens,
                                  size_t T,
                                  float *step_log_probs,
                                  float *total_log_prob_out) override
    {
        if (T == 0 || h_seq == nullptr || target_tokens == nullptr)
        {
            return false;
        }

        float total_lp = 0.0f;
        std::vector<float> logits(vocab_size_);

        for (size_t t = 0; t < T; ++t)
        {
            const float *h_t = h_seq + t * hidden_size_;
            uint16_t target = target_tokens[t];
            if (target >= vocab_size_) target = 0;

            // Compute output logits
            math::Gemv(cached_w_out_, h_t, cached_b_out_, logits.data(), vocab_size_, hidden_size_);
            const float log_denom = math::ComputeLogSumExp(logits.data(), vocab_size_);
            float lp = logits[target] - log_denom;
            if (target == 0)
            {
                lp = std::min(lp, -6.0f) - 6.0f;
            }

            if (step_log_probs != nullptr)
            {
                step_log_probs[t] = lp;
            }
            total_lp += lp;
        }

        if (total_log_prob_out != nullptr)
        {
            *total_log_prob_out = total_lp;
        }
        return true;
    }

    bool TrainOutputOnlyBatch(const float *h_seq,
                              const uint16_t *target_tokens,
                              size_t T,
                              size_t valid_targets,
                              float learning_rate,
                              float weight_decay,
                              float max_grad_norm,
                              const float *base_w_out,
                              const float *base_b_out,
                              float *total_log_prob_out) override
    {
        if (T == 0 || valid_targets == 0 || cached_w_out_ == nullptr || cached_b_out_ == nullptr)
        {
            return false;
        }

        const size_t V = vocab_size_;
        const size_t H = hidden_size_;
        const float inv_valid = 1.0f / static_cast<float>(valid_targets);

        std::vector<float> g_out(V * H, 0.0f);
        std::vector<float> g_bout(V, 0.0f);
        std::vector<float> logits(V, 0.0f);

        float total_lp = 0.0f;

        for (size_t t = 0; t < T; ++t)
        {
            const float *h_t = h_seq + t * H;
            const uint16_t y_t = target_tokens[t];

            math::Gemv(cached_w_out_, h_t, cached_b_out_, logits.data(), V, H);
            const float log_denom = math::ComputeLogSumExp(logits.data(), V);
            float token_lp = logits[y_t] - log_denom;

            if (y_t == 0)
            {
                token_lp = std::min(token_lp, -6.0f) - 6.0f;
            }
            else
            {
                for (size_t v = 0; v < V; ++v)
                {
                    const float p_v = std::exp(logits[v] - log_denom);
                    const float dl_v = (p_v - ((v == y_t) ? 1.0f : 0.0f)) * inv_valid;
                    g_bout[v] += dl_v;
                    math::Axpy(dl_v, h_t, &g_out[v * H], H);
                }
            }
            total_lp += token_lp;
        }

        // L2 norm clipping and anchor update
        float grad_norm_sq = math::DotProduct(g_bout.data(), g_bout.data(), V);
        grad_norm_sq += math::DotProduct(g_out.data(), g_out.data(), V * H);
        const float grad_norm = std::sqrt(grad_norm_sq);

        float grad_scale = 1.0f;
        if (max_grad_norm > 0.0f && grad_norm > max_grad_norm)
        {
            grad_scale = max_grad_norm / grad_norm;
        }

        const float eff_lr = learning_rate * grad_scale;
        const float anchor_decay = learning_rate * weight_decay;

        for (size_t i = 0; i < V * H; ++i)
        {
            cached_w_out_[i] -= eff_lr * g_out[i] + anchor_decay * (cached_w_out_[i] - base_w_out[i]);
        }
        for (size_t i = 0; i < V; ++i)
        {
            cached_b_out_[i] -= eff_lr * g_bout[i] + anchor_decay * (cached_b_out_[i] - base_b_out[i]);
        }

        if (total_log_prob_out != nullptr)
        {
            *total_log_prob_out = total_lp;
        }
        return true;
    }

    void SetActivePointers(float *w_out, float *b_out)
    {
        cached_w_out_ = w_out;
        cached_b_out_ = b_out;
    }

private:
    size_t vocab_size_ = 0;
    size_t hidden_size_ = 0;
    float *cached_w_out_ = nullptr;
    float *cached_b_out_ = nullptr;
};

// ============================================================================
// OpenCL Backend Implementation
// ============================================================================

class OpenClBackend final : public GruGpuBackend
{
public:
    OpenClBackend(cl_platform_id platform, cl_device_id device, std::string device_name)
        : platform_(platform), device_(device), device_name_(std::move(device_name))
    {
    }

    ~OpenClBackend() override
    {
        ReleaseResources();
    }

    bool IsAvailable() const noexcept override { return is_initialized_; }
    const char *GetDeviceName() const noexcept override { return device_name_.c_str(); }
    const char *GetBackendType() const noexcept override { return "OpenCL"; }

    bool Initialize(size_t vocab_size, size_t hidden_size) override
    {
        const auto &cl = OpenClDynamicLoader::Instance().Fn();
        vocab_size_ = vocab_size;
        hidden_size_ = hidden_size;

        cl_int err = CL_SUCCESS;
        context_ = cl.CreateContext(nullptr, 1, &device_, nullptr, nullptr, &err);
        if (err != CL_SUCCESS || !context_)
        {
            return false;
        }

        queue_ = cl.CreateCommandQueue(context_, device_, 0, &err);
        if (err != CL_SUCCESS || !queue_)
        {
            ReleaseResources();
            return false;
        }

        // Build JIT program
        const char *src = kOpenClKernelSource;
        program_ = cl.CreateProgramWithSource(context_, 1, &src, nullptr, &err);
        if (err != CL_SUCCESS || !program_)
        {
            ReleaseResources();
            return false;
        }

        err = cl.BuildProgram(program_, 1, &device_, "-cl-mad-enable -cl-fast-relaxed-math", nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            ReleaseResources();
            return false;
        }

        kernel_score_ = cl.CreateKernel(program_, "BatchedOutputLogProb", &err);
        if (err != CL_SUCCESS || !kernel_score_)
        {
            ReleaseResources();
            return false;
        }

        // Allocate device buffers
        const size_t size_out = vocab_size_ * hidden_size_ * sizeof(float);
        const size_t size_bout = vocab_size_ * sizeof(float);
        constexpr size_t kMaxSeqLen = 256;
        const size_t size_hseq = kMaxSeqLen * hidden_size_ * sizeof(float);
        const size_t size_logits = kMaxSeqLen * vocab_size_ * sizeof(float);
        const size_t size_tokens = kMaxSeqLen * sizeof(uint16_t);
        const size_t size_probs = kMaxSeqLen * sizeof(float);

        buf_w_out_ = cl.CreateBuffer(context_, CL_MEM_READ_WRITE, size_out, nullptr, &err);
        buf_b_out_ = cl.CreateBuffer(context_, CL_MEM_READ_WRITE, size_bout, nullptr, &err);
        buf_h_seq_ = cl.CreateBuffer(context_, CL_MEM_READ_ONLY, size_hseq, nullptr, &err);
        buf_tokens_ = cl.CreateBuffer(context_, CL_MEM_READ_ONLY, size_tokens, nullptr, &err);
        buf_logits_ = cl.CreateBuffer(context_, CL_MEM_READ_WRITE, size_logits, nullptr, &err);
        buf_probs_ = cl.CreateBuffer(context_, CL_MEM_WRITE_ONLY, size_probs, nullptr, &err);

        if (!buf_w_out_ || !buf_b_out_ || !buf_h_seq_ || !buf_tokens_ || !buf_logits_ || !buf_probs_)
        {
            return false;
        }

        is_initialized_ = true;
        return true;
    }

    bool UploadWeights(const float *, const float *, const float *, const float *, const float *, const float *w_out, const float *b_out) override
    {
        return SyncOutputWeights(w_out, b_out);
    }

    bool SyncOutputWeights(const float *w_out, const float *b_out) override
    {
        if (!is_initialized_) return false;
        const auto &cl = OpenClDynamicLoader::Instance().Fn();
        cl_int err1 = cl.EnqueueWriteBuffer(queue_, buf_w_out_, CL_FALSE, 0, vocab_size_ * hidden_size_ * sizeof(float), w_out, 0, nullptr, nullptr);
        cl_int err2 = cl.EnqueueWriteBuffer(queue_, buf_b_out_, CL_TRUE, 0, vocab_size_ * sizeof(float), b_out, 0, nullptr, nullptr);
        return (err1 == CL_SUCCESS && err2 == CL_SUCCESS);
    }

    bool DownloadOutputWeights(float *w_out, float *b_out) override
    {
        if (!is_initialized_) return false;
        const auto &cl = OpenClDynamicLoader::Instance().Fn();
        cl_int err1 = cl.EnqueueReadBuffer(queue_, buf_w_out_, CL_FALSE, 0, vocab_size_ * hidden_size_ * sizeof(float), w_out, 0, nullptr, nullptr);
        cl_int err2 = cl.EnqueueReadBuffer(queue_, buf_b_out_, CL_TRUE, 0, vocab_size_ * sizeof(float), b_out, 0, nullptr, nullptr);
        return (err1 == CL_SUCCESS && err2 == CL_SUCCESS);
    }

    bool DownloadAllWeights(float *, float *, float *, float *, float *, float *w_out, float *b_out) override
    {
        return DownloadOutputWeights(w_out, b_out);
    }

    bool ScoreSequenceOutputBatch(const float *h_seq,
                                  const uint16_t *target_tokens,
                                  size_t T,
                                  float *step_log_probs,
                                  float *total_log_prob_out) override
    {
        if (!is_initialized_ || T == 0 || h_seq == nullptr || target_tokens == nullptr)
        {
            return false;
        }
        constexpr size_t kMaxSeqLen = 256;
        if (T > kMaxSeqLen) return false;

        const auto &cl = OpenClDynamicLoader::Instance().Fn();

        // 1. Enqueue input buffers
        cl.EnqueueWriteBuffer(queue_, buf_h_seq_, CL_FALSE, 0, T * hidden_size_ * sizeof(float), h_seq, 0, nullptr, nullptr);
        cl.EnqueueWriteBuffer(queue_, buf_tokens_, CL_FALSE, 0, T * sizeof(uint16_t), target_tokens, 0, nullptr, nullptr);

        // 2. Set Kernel arguments
        const cl_uint v_size = static_cast<cl_uint>(vocab_size_);
        const cl_uint h_size = static_cast<cl_uint>(hidden_size_);

        cl.SetKernelArg(kernel_score_, 0, sizeof(cl_mem), &buf_h_seq_);
        cl.SetKernelArg(kernel_score_, 1, sizeof(cl_mem), &buf_w_out_);
        cl.SetKernelArg(kernel_score_, 2, sizeof(cl_mem), &buf_b_out_);
        cl.SetKernelArg(kernel_score_, 3, sizeof(cl_mem), &buf_tokens_);
        cl.SetKernelArg(kernel_score_, 4, sizeof(cl_mem), &buf_probs_);
        cl.SetKernelArg(kernel_score_, 5, sizeof(cl_mem), &buf_logits_);
        cl.SetKernelArg(kernel_score_, 6, sizeof(cl_uint), &v_size);
        cl.SetKernelArg(kernel_score_, 7, sizeof(cl_uint), &h_size);

        // 3. Dispatch: 1 work-group per time step t
        constexpr size_t kLocalSize = 256;
        const size_t global_size = T * kLocalSize;
        const size_t local_size = kLocalSize;

        cl_int err = cl.EnqueueNDRangeKernel(queue_, kernel_score_, 1, nullptr, &global_size, &local_size, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            return false;
        }

        // 4. Read back log_probs array
        std::vector<float> tmp_probs(T);
        cl.EnqueueReadBuffer(queue_, buf_probs_, CL_TRUE, 0, T * sizeof(float), tmp_probs.data(), 0, nullptr, nullptr);

        float total_lp = 0.0f;
        for (size_t t = 0; t < T; ++t)
        {
            total_lp += tmp_probs[t];
            if (step_log_probs != nullptr)
            {
                step_log_probs[t] = tmp_probs[t];
            }
        }

        if (total_log_prob_out != nullptr)
        {
            *total_log_prob_out = total_lp;
        }
        return true;
    }

    bool TrainOutputOnlyBatch(const float *h_seq,
                              const uint16_t *target_tokens,
                              size_t T,
                              size_t valid_targets,
                              float learning_rate,
                              float weight_decay,
                              float max_grad_norm,
                              const float *base_w_out,
                              const float *base_b_out,
                              float *total_log_prob_out) override
    {
        // For online training, read weights back or execute CPU path to ensure bit-level accuracy with base weights
        std::vector<float> host_w_out(vocab_size_ * hidden_size_);
        std::vector<float> host_b_out(vocab_size_);
        if (!DownloadOutputWeights(host_w_out.data(), host_b_out.data()))
        {
            return false;
        }

        CpuFallbackBackend cpu_fallback;
        cpu_fallback.Initialize(vocab_size_, hidden_size_);
        cpu_fallback.SetActivePointers(host_w_out.data(), host_b_out.data());

        bool ok = cpu_fallback.TrainOutputOnlyBatch(
            h_seq, target_tokens, T, valid_targets, learning_rate, weight_decay, max_grad_norm,
            base_w_out, base_b_out, total_log_prob_out);

        if (ok)
        {
            SyncOutputWeights(host_w_out.data(), host_b_out.data());
        }
        return ok;
    }

private:
    void ReleaseResources()
    {
        const auto &cl = OpenClDynamicLoader::Instance().Fn();
        if (!OpenClDynamicLoader::Instance().IsLoaded()) return;

        if (buf_w_out_) { cl.ReleaseMemObject(buf_w_out_); buf_w_out_ = nullptr; }
        if (buf_b_out_) { cl.ReleaseMemObject(buf_b_out_); buf_b_out_ = nullptr; }
        if (buf_h_seq_) { cl.ReleaseMemObject(buf_h_seq_); buf_h_seq_ = nullptr; }
        if (buf_tokens_) { cl.ReleaseMemObject(buf_tokens_); buf_tokens_ = nullptr; }
        if (buf_logits_) { cl.ReleaseMemObject(buf_logits_); buf_logits_ = nullptr; }
        if (buf_probs_) { cl.ReleaseMemObject(buf_probs_); buf_probs_ = nullptr; }
        if (kernel_score_) { cl.ReleaseKernel(kernel_score_); kernel_score_ = nullptr; }
        if (program_) { cl.ReleaseProgram(program_); program_ = nullptr; }
        if (queue_) { cl.ReleaseCommandQueue(queue_); queue_ = nullptr; }
        if (context_) { cl.ReleaseContext(context_); context_ = nullptr; }

        is_initialized_ = false;
    }

    cl_platform_id platform_ = nullptr;
    cl_device_id device_ = nullptr;
    std::string device_name_;
    bool is_initialized_ = false;

    size_t vocab_size_ = 0;
    size_t hidden_size_ = 0;

    cl_context context_ = nullptr;
    cl_command_queue queue_ = nullptr;
    cl_program program_ = nullptr;
    cl_kernel kernel_score_ = nullptr;

    cl_mem buf_w_out_ = nullptr;
    cl_mem buf_b_out_ = nullptr;
    cl_mem buf_h_seq_ = nullptr;
    cl_mem buf_tokens_ = nullptr;
    cl_mem buf_logits_ = nullptr;
    cl_mem buf_probs_ = nullptr;
};

} // namespace

std::unique_ptr<GruGpuBackend> GruGpuBackend::Create(size_t vocab_size, size_t hidden_size)
{
    // Check environment variable MOGRU_USE_GPU
    const char *env_gpu = std::getenv("MOGRU_USE_GPU");
    if (!env_gpu)
    {
        env_gpu = std::getenv("MOZC_GRU_USE_GPU");
    }

    // Try OpenCL Dynamic Loader
#if defined(__linux__)
    bool has_accessible_dri = false;
    for (const char *dev_path : {"/dev/dri/renderD128", "/dev/dri/card0"})
    {
        int fd = open(dev_path, O_RDWR);
        if (fd >= 0)
        {
            close(fd);
            has_accessible_dri = true;
            break;
        }
    }

    if (!has_accessible_dri)
    {
        if (env_gpu)
        {
            std::string mode(env_gpu);
            std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
            if (mode == "1" || mode == "true" || mode == "force" || mode == "on")
            {
                auto fallback = std::make_unique<CpuFallbackBackend>();
                fallback->Initialize(vocab_size, hidden_size);
                return fallback;
            }
        }
        return nullptr;
    }
#endif

    const auto &loader = OpenClDynamicLoader::Instance();
    if (loader.IsLoaded())
    {
        const auto &cl = loader.Fn();
        cl_uint num_platforms = 0;
        if (cl.GetPlatformIDs(0, nullptr, &num_platforms) == CL_SUCCESS && num_platforms > 0)
        {
            std::vector<cl_platform_id> platforms(num_platforms);
            cl.GetPlatformIDs(num_platforms, platforms.data(), nullptr);

            for (cl_platform_id plat : platforms)
            {
                cl_uint num_devices = 0;
                // Prefer GPU devices
                if (cl.GetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices) == CL_SUCCESS && num_devices > 0)
                {
                    std::vector<cl_device_id> devices(num_devices);
                    cl.GetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, num_devices, devices.data(), nullptr);

                    char dev_name[256] = {0};
                    cl.GetDeviceInfo(devices[0], CL_DEVICE_NAME, sizeof(dev_name), dev_name, nullptr);

                    auto backend = std::make_unique<OpenClBackend>(plat, devices[0], std::string(dev_name));
                    if (backend->Initialize(vocab_size, hidden_size))
                    {
                        return backend;
                    }
                }
            }
        }
    }

    // If explicit force mode requested, return CPU fallback backend with GPU interface
    if (env_gpu)
    {
        std::string mode(env_gpu);
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
        if (mode == "1" || mode == "true" || mode == "force" || mode == "on")
        {
            auto fallback = std::make_unique<CpuFallbackBackend>();
            fallback->Initialize(vocab_size, hidden_size);
            return fallback;
        }
    }

    return nullptr;
}

} // namespace gruscorer
