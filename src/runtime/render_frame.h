#pragma once

#include "bloom.h"
#include "ghost.h"
#include "image.h"
#include "lens.h"
#include "starburst.h"

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
    float threshold = 3.0f;
    int downsample = 4;
    int ray_grid = 16;
    int max_sources = 64;
    int aperture_blades = 0;
    float aperture_rotation_deg = 0.0f;
    float min_ghost = 1e-7f;
    float flare_gain = 1000.0f;
    float ghost_blur = 0.003f;
    int ghost_blur_passes = 3;
    float haze_gain = 0.0f;
    float haze_radius = 0.15f;
    int haze_blur_passes = 3;
    float starburst_gain = 0.0f;
    float starburst_scale = 0.15f;
    int spectral_samples = 3;
    bool ghost_normalize = true;
    float max_area_boost = 100.0f;
    BloomConfig bloom {};
};

struct FrameRenderOutputs
{
    int width = 0;
    int height = 0;
    GhostRenderBackend ghost_backend = GhostRenderBackend::CPU;
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
    std::vector<BrightPixel> sources;
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
    FrameRenderOutputs& outputs
);
