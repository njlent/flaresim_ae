#pragma once

#include "bloom.h"
#include "ghost.h"
#include "image.h"
#include "lens.h"

#include <vector>

struct FrameRenderSettings
{
    float fov_h_deg = 60.0f;
    float threshold = 3.0f;
    int downsample = 4;
    int ray_grid = 16;
    int max_sources = 64;
    float min_ghost = 1e-7f;
    float flare_gain = 1000.0f;
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
    std::vector<BrightPixel> sources;
};

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs
);
