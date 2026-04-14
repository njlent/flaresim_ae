#pragma once

#include "ghost.h"
#include "image.h"

#include <cstddef>
#include <vector>

std::vector<BrightPixel> extract_bright_pixels(
    const RgbImageView& img,
    float threshold,
    int downsample,
    float fov_h,
    float fov_v,
    const MonoImageView* mask = nullptr
);

void limit_bright_pixels(
    std::vector<BrightPixel>& pixels,
    std::size_t max_sources
);

void cluster_bright_pixels(
    std::vector<BrightPixel>& pixels,
    int radius_px,
    int image_width,
    float tan_half_fov_h
);
