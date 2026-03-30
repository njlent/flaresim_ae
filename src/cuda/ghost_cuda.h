#pragma once

#include "ghost.h"
#include "lens.h"

#include <cstddef>
#include <string>
#include <vector>

struct GpuBufferCache
{
    void* d_surfs = nullptr;
    std::size_t surfs_bytes = 0;

    void* d_pairs = nullptr;
    std::size_t pairs_bytes = 0;

    void* d_src = nullptr;
    std::size_t src_bytes = 0;

    void* d_grid = nullptr;
    std::size_t grid_bytes = 0;

    float* d_out_r = nullptr;
    float* d_out_g = nullptr;
    float* d_out_b = nullptr;
    std::size_t out_floats = 0;

    void release();

    ~GpuBufferCache() { release(); }

    GpuBufferCache() = default;
    GpuBufferCache(const GpuBufferCache&) = delete;
    GpuBufferCache& operator=(const GpuBufferCache&) = delete;
};

bool cuda_ghost_renderer_compiled();
bool cuda_ghost_renderer_available(std::string* reason = nullptr);

bool launch_ghost_cuda(
    const LensSystem& lens,
    const std::vector<GhostPair>& active_pairs,
    const std::vector<float>& pair_area_boosts,
    const std::vector<BrightPixel>& sources,
    float sensor_half_w,
    float sensor_half_h,
    float* out_r,
    float* out_g,
    float* out_b,
    int width,
    int height,
    const GhostConfig& config,
    GpuBufferCache& cache,
    std::string* out_error = nullptr
);
