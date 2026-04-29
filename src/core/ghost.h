// ============================================================================
// ghost.h — Ghost reflection enumeration and rendering
// ============================================================================
#pragma once

#include "lens.h"
#include "vec3.h"

#include <vector>

enum class GhostRenderBackend
{
    CPU,
    CUDA,
};

enum class GhostCleanupMode
{
    LegacyBlur,
    SharpAdaptive,
    SharpAdaptivePlusBlur,
};

enum class ProjectedCellsMode
{
    Auto,
    Off,
    Force,
};

enum class PupilJitterMode
{
    Off,
    Stratified,
    Halton,
};

// A ghost bounce pair: surfaces where light reflects instead of transmitting.
struct GhostPair
{
    int surf_a; // first bounce surface (closer to front)
    int surf_b; // second bounce surface (closer to sensor)
};

struct GhostPairPlan
{
    GhostPair pair {};
    float area_boost = 1.0f;
    float splat_radius_px = 1.0f;
    float estimated_extent_px = 1.0f;
    float reference_footprint_area_px2 = 1.0f;
    float distortion_score = 0.0f;
    int ray_grid = 0;
    bool use_cell_rasterization = false;
};

struct GhostGridSample
{
    float u = 0.0f;
    float v = 0.0f;
};

struct GhostGridCell
{
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float uc = 0.0f;
    float vc = 0.0f;
};

struct GhostGridBucket
{
    int ray_grid = 0;
    std::vector<GhostGridSample> samples;
    std::vector<GhostGridCell> cells;
};

struct GhostRenderSetup
{
    std::vector<GhostPairPlan> active_pair_plans;
    std::vector<GhostGridBucket> grid_buckets;
    int max_valid_grid_count = 0;
    int min_pair_grid = 0;
    int max_pair_grid = 0;
};

// A bright pixel extracted from the input image.
struct BrightPixel
{
    float angle_x; // horizontal angle from optical axis (radians)
    float angle_y; // vertical angle from optical axis (radians)
    float r, g, b; // HDR intensity
};

// Configuration for the ghost renderer.
struct GhostConfig
{
    int ray_grid = 64;                               // samples per dimension across entrance pupil
    float min_intensity = 1e-7f;                     // skip ghost pairs dimmer than this
    float gain = 1000.0f;                            // ghost intensity multiplier
    float wavelengths[3] = {650.0f, 550.0f, 450.0f}; // R, G, B in nm
    int spectral_samples = 3;                        // 3, 5, 7, 9, 11, 15, 21, 31
    int aperture_blades = 0;                         // 0 = circular
    float aperture_rotation_deg = 0.0f;

    // Per-pair area normalization: boost defocused ghost pairs so they remain
    // visible.  Production renderers (ILM, Weta) use a similar technique.
    bool ghost_normalize = true;   // enable per-pair area correction
    float max_area_boost = 100.0f; // clamp the correction factor
    GhostCleanupMode cleanup_mode = GhostCleanupMode::LegacyBlur;
    float adaptive_quality = 1.0f;           // scales adaptive pair grids; 1.0 = ray_grid baseline
    float adaptive_sampling_strength = 1.0f; // 1.0 = auto baseline
    float footprint_radius_bias = 1.0f;      // 1.0 = traced footprint radius as-is
    float footprint_clamp = 1.15f;           // max multiplier over fallback radius
    int max_adaptive_pair_grid = 0;          // 0 = auto (2x base grid)
    int pair_start_index = 0;                // active-pair index offset after physical filtering
    int pair_count = 0;                      // 0 = all active pairs after pair_start_index
    ProjectedCellsMode projected_cells_mode = ProjectedCellsMode::Auto;
    PupilJitterMode pupil_jitter = PupilJitterMode::Off;
    int pupil_jitter_seed = 0;
    float cell_coverage_bias = 1.0f;         // 1.0 = exact projected quad size
    float cell_edge_inset = 0.1f;            // inward inset before tracing cell corners
};

// Enumerate all valid ghost bounce pairs for the lens system.
// Returns C(N, 2) pairs where N = number of surfaces.
std::vector<GhostPair> enumerate_ghost_pairs(const LensSystem &lens);
const char* ghost_render_backend_name(GhostRenderBackend backend);
int select_ghost_pair_ray_grid(int base_ray_grid,
                               float estimated_extent_px,
                               float distortion_score,
                               float adaptive_quality,
                               float adaptive_sampling_strength,
                               int max_adaptive_pair_grid);
float select_ghost_footprint_radius(float fallback_radius_px,
                                    float footprint_area_px2,
                                    float anisotropy,
                                    float footprint_radius_bias,
                                    float footprint_clamp);
float select_ghost_density_boost(float pair_area_boost,
                                 float reference_footprint_area_px2,
                                 float local_footprint_area_px2);
bool select_ghost_cell_rasterization(float estimated_extent_px,
                                     float distortion_score);
std::vector<GhostPairPlan> plan_active_ghost_pairs(const LensSystem& lens,
                                                   float fov_h,
                                                   float fov_v,
                                                   int width,
                                                   int height,
                                                   const GhostConfig& config);
bool build_ghost_render_setup(const LensSystem& lens,
                              float fov_h,
                              float fov_v,
                              int width,
                              int height,
                              const GhostConfig& config,
                              GhostRenderSetup& out_setup);

// Render all ghost reflections onto the output flare image.
//
// For each ghost pair, traces rays from each bright source through the
// lens system with two reflections, accumulating contributions on the
// output image via bilinear splatting.
//
// out_r/g/b: pre-allocated output buffers (width × height), zeroed.
// fov_h/fov_v: horizontal and vertical FOV in radians.
void render_ghosts(const LensSystem &lens,
                   const std::vector<BrightPixel> &sources,
                   float fov_h, float fov_v,
                   float *out_r, float *out_g, float *out_b,
                   int width, int height,
                   const GhostConfig &config,
                   const GhostRenderSetup* setup = nullptr,
                   struct GpuBufferCache* gpu_cache = nullptr,
                   GhostRenderBackend* out_backend = nullptr);
