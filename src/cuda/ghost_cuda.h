#pragma once

#include "ghost.h"
#include "lens.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GpuLaunchBucketCache
{
    int ray_grid = 0;

    void* d_splat_pairs = nullptr;
    std::size_t splat_pairs_bytes = 0;
    int splat_pair_count = 0;

    void* d_splat_grid = nullptr;
    std::size_t splat_grid_bytes = 0;
    int splat_grid_count = 0;

    void* d_cell_pairs = nullptr;
    std::size_t cell_pairs_bytes = 0;
    int cell_pair_count = 0;

    void* d_cell_grid = nullptr;
    std::size_t cell_grid_bytes = 0;
    int cell_grid_count = 0;
};

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

    void* d_spec = nullptr;
    std::size_t spec_bytes = 0;

    float* d_out_r = nullptr;
    float* d_out_g = nullptr;
    float* d_out_b = nullptr;
    std::size_t out_floats = 0;

    void* h_src = nullptr;
    std::size_t h_src_bytes = 0;

    void* h_spec = nullptr;
    std::size_t h_spec_bytes = 0;

    float* h_out_r = nullptr;
    float* h_out_g = nullptr;
    float* h_out_b = nullptr;
    std::size_t host_out_floats = 0;

    void* stream = nullptr;
    void* graph = nullptr;
    void* graph_exec = nullptr;

    std::uint64_t lens_key = 0;
    std::uint64_t spec_key = 0;
    std::uint64_t setup_key = 0;
    std::uint64_t graph_key = 0;

    std::vector<GpuLaunchBucketCache> launch_buckets;

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
    const GhostRenderSetup& setup,
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
