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
    float source_cap = 0.0f,
    const MonoImageView* mask = nullptr
);

void limit_bright_pixels(
    std::vector<BrightPixel>& pixels,
    std::size_t max_sources
);

// Replaces detected sources with fewer, stronger ones that keep the shape of
// the bright areas. pixels must come from extract_bright_pixels with the same
// downsample and image size. Two sources link when the gap between their
// downsample blocks is under radius_px (touching blocks always link), links
// chain into regions, and each region is split into an even grid of cells
// about 2 * radius_px across, one source per occupied cell. Energy is kept.
void cluster_bright_pixels(
    std::vector<BrightPixel>& pixels,
    int radius_px,
    int downsample,
    int image_width,
    int image_height,
    float tan_half_fov_h,
    float tan_half_fov_v
);

// Box-blur radius that spreads each clustered source's ghosts and starburst
// over the cell of light it stands for (a box of half-width r is a uniform
// cell 2r across). 0 when clustering is off or cells are no bigger than the
// downsample blocks.
int cluster_softening_radius_px(int radius_px, int downsample);

// Pixel position of a source's downsample block, as extract_bright_pixels
// laid them out.
void bright_pixel_block(
    const BrightPixel& pixel,
    int downsample,
    int image_width,
    int image_height,
    float tan_half_fov_h,
    float tan_half_fov_v,
    int& out_block_x,
    int& out_block_y
);
