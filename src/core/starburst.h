#pragma once

#include "ghost.h"

#include <vector>

struct StarburstConfig
{
    float gain = 0.0f;
    float scale = 0.15f;
    int aperture_blades = 0;
    float aperture_rotation_deg = 0.0f;
    float wavelengths[3] = {650.0f, 550.0f, 450.0f};
};

struct StarburstPSF
{
    int N = 0;
    std::vector<float> data;

    bool empty() const { return N == 0 || data.empty(); }
};

void compute_starburst_psf(const StarburstConfig& config,
                           StarburstPSF& out_psf,
                           int fft_size = 512);

void render_starburst(const StarburstPSF& psf,
                      const StarburstConfig& config,
                      const std::vector<BrightPixel>& sources,
                      float tan_half_h,
                      float tan_half_v,
                      float* out_r,
                      float* out_g,
                      float* out_b,
                      int width,
                      int height,
                      int fmt_w,
                      int fmt_h,
                      int fmt_x0_in_buf,
                      int fmt_y0_in_buf);
