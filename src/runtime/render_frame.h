#pragma once

#include "bloom.h"
#include "ghost.h"
#include "image.h"
#include "lens.h"
#include "starburst.h"

#include <cstdint>
#include <vector>

struct FrameRenderSettings
{
    bool use_sensor_size = false;
    int sensor_preset_index = 1;
    float fov_h_deg = 60.0f;
    float fov_v_deg = 24.0f;
    bool auto_fov_v = true;
    float sensor_width_mm = 36.0f;
    float sensor_height_mm = 24.0f;
    float focal_length_mm = 50.0f;
    float threshold = 8.0f;
    int downsample = 2;
    int ray_grid = 128;
    int max_sources = 256;
    int aperture_blades = 0;
    float aperture_rotation_deg = 0.0f;
    float min_ghost = 1e-7f;
    float flare_gain = 4000.0f;
    float sky_brightness = 1.0f;
    float ghost_blur = 0.0005f;
    int ghost_blur_passes = 3;
    GhostCleanupMode ghost_cleanup_mode = GhostCleanupMode::SharpAdaptive;
    float haze_gain = 0.0f;
    float haze_radius = 0.15f;
    int haze_blur_passes = 3;
    float starburst_gain = 0.0f;
    float starburst_scale = 0.15f;
    int spectral_samples = 3;
    bool ghost_normalize = true;
    float max_area_boost = 100.0f;
    float adaptive_sampling_strength = 0.0f;
    float footprint_radius_bias = 1.0f;
    float footprint_clamp = 1.15f;
    int max_adaptive_pair_grid = 0;
    ProjectedCellsMode projected_cells_mode = ProjectedCellsMode::Off;
    float cell_coverage_bias = 1.0f;
    float cell_edge_inset = 0.1f;
    BloomConfig bloom {};
};

struct FrameRenderPlan
{
    bool need_scene_output = true;
    bool need_source_output = true;
    bool need_flare = true;
    bool need_bloom = true;
    bool need_haze = true;
    bool need_starburst = true;
};

struct FrameRenderStats
{
    bool recomputed_scene = false;
    bool recomputed_sources = false;
    bool recomputed_ghosts = false;
    bool recomputed_bloom = false;
    bool recomputed_haze = false;
    bool recomputed_starburst = false;
};

struct FrameRenderOutputs
{
    int width = 0;
    int height = 0;
    GhostRenderBackend ghost_backend = GhostRenderBackend::CPU;
    FrameRenderStats stats {};
    std::vector<float> scene_r;
    std::vector<float> scene_g;
    std::vector<float> scene_b;
    std::vector<float> flare_r;
    std::vector<float> flare_g;
    std::vector<float> flare_b;
    std::vector<float> bloom_r;
    std::vector<float> bloom_g;
    std::vector<float> bloom_b;
    std::vector<float> haze_r;
    std::vector<float> haze_g;
    std::vector<float> haze_b;
    std::vector<float> starburst_r;
    std::vector<float> starburst_g;
    std::vector<float> starburst_b;
    std::vector<BrightPixel> detected_sources;
    std::vector<BrightPixel> sources;
};

struct FrameRenderCache
{
    std::uint64_t scene_key = 0;
    bool has_scene = false;
    std::vector<float> scene_r;
    std::vector<float> scene_g;
    std::vector<float> scene_b;

    std::uint64_t source_key = 0;
    bool has_sources = false;
    std::vector<BrightPixel> detected_sources;
    std::vector<BrightPixel> sources;

    std::uint64_t ghost_key = 0;
    bool has_ghosts = false;
    GhostRenderBackend ghost_backend = GhostRenderBackend::CPU;
    std::vector<float> flare_r;
    std::vector<float> flare_g;
    std::vector<float> flare_b;

    std::uint64_t bloom_key = 0;
    bool has_bloom = false;
    std::vector<float> bloom_r;
    std::vector<float> bloom_g;
    std::vector<float> bloom_b;

    std::uint64_t haze_key = 0;
    bool has_haze = false;
    std::vector<float> haze_r;
    std::vector<float> haze_g;
    std::vector<float> haze_b;

    std::uint64_t starburst_key = 0;
    bool has_starburst = false;
    std::vector<float> starburst_r;
    std::vector<float> starburst_g;
    std::vector<float> starburst_b;

    void clear();
};

bool compute_camera_fov(const FrameRenderSettings& settings,
                        int width,
                        int height,
                        float& out_fov_h_rad,
                        float& out_fov_v_rad);

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs,
    const MonoImageView* detection_mask = nullptr
);

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs,
    const FrameRenderPlan& plan,
    FrameRenderCache* cache,
    const MonoImageView* detection_mask = nullptr
);
