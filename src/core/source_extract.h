#pragma once

#include "ghost.h"
#include "image.h"

#include <vector>

std::vector<BrightPixel> extract_bright_pixels(
    const RgbImageView& img,
    float threshold,
    int downsample,
    float fov_h,
    float fov_v
);
