#pragma once

#include "ghost_cuda.h"
#include "parameter_state.h"
#include "starburst.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GpuFrameRenderCache
{
    GpuBufferCache ghost_cache;

    float* d_scene_bgra = nullptr;
    std::size_t scene_floats = 0;

    float* d_bloom_r = nullptr;
    float* d_bloom_g = nullptr;
    float* d_bloom_b = nullptr;
    std::size_t bloom_floats = 0;

    float* d_haze_r = nullptr;
    float* d_haze_g = nullptr;
    float* d_haze_b = nullptr;
    std::size_t haze_floats = 0;

    float* d_starburst_r = nullptr;
    float* d_starburst_g = nullptr;
    float* d_starburst_b = nullptr;
    std::size_t starburst_floats = 0;

    float* d_work0_r = nullptr;
    float* d_work0_g = nullptr;
    float* d_work0_b = nullptr;
    std::size_t work0_floats = 0;

    float* d_work1_r = nullptr;
    float* d_work1_g = nullptr;
    float* d_work1_b = nullptr;
    std::size_t work1_floats = 0;

    BrightPixel* d_candidates = nullptr;
    std::size_t candidate_capacity = 0;

    BrightPixel* d_sources = nullptr;
    std::size_t source_capacity = 0;

    float* d_starburst_psf = nullptr;
    std::size_t starburst_psf_floats = 0;
    int starburst_psf_size = 0;

    std::uint64_t ghost_setup_key = 0;
    bool has_ghost_setup = false;
    GhostRenderSetup ghost_setup;

    std::uint64_t starburst_psf_key = 0;
    StarburstPSF starburst_psf;

    void release();

    ~GpuFrameRenderCache() { release(); }

    GpuFrameRenderCache() = default;
    GpuFrameRenderCache(const GpuFrameRenderCache&) = delete;
    GpuFrameRenderCache& operator=(const GpuFrameRenderCache&) = delete;
};

bool render_frame_cuda_bgra128(
    const LensSystem& lens,
    const FrameRenderSettings& settings,
    AeOutputView view,
    const float* input_pixels,
    float* output_pixels,
    int width,
    int height,
    int input_row_floats,
    int output_row_floats,
    const float* mask_pixels,
    int mask_row_floats,
    GpuFrameRenderCache& cache,
    GhostRenderBackend* out_ghost_backend = nullptr,
    std::string* out_error = nullptr
);
