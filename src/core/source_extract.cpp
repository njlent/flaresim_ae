#include "source_extract.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <utility>

std::vector<BrightPixel> extract_bright_pixels(
    const RgbImageView& img,
    float threshold,
    int downsample,
    float fov_h,
    float fov_v,
    float source_cap,
    const MonoImageView* mask)
{
    std::vector<BrightPixel> result;

    if (!img.r || !img.g || !img.b || img.width <= 0 || img.height <= 0) {
        return result;
    }

    const int stride = std::max(1, downsample);
    const int dw = std::max(1, img.width / stride);
    const int dh = std::max(1, img.height / stride);

    const float tan_half_h = std::tan(fov_h * 0.5f);
    const float tan_half_v = std::tan(fov_v * 0.5f);
    const bool use_mask = mask && mask->value &&
                          mask->width == img.width &&
                          mask->height == img.height;

    for (int dy = 0; dy < dh; ++dy) {
        for (int dx = 0; dx < dw; ++dx) {
            float peak_r = 0.0f;
            float peak_g = 0.0f;
            float peak_b = 0.0f;
            float peak_lum = -1.0f;
            int peak_x = -1;
            int peak_y = -1;

            const int y0 = dy * stride;
            const int x0 = dx * stride;
            const int y1 = std::min(y0 + stride, img.height);
            const int x1 = std::min(x0 + stride, img.width);

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const int i = y * img.width + x;
                    float mask_alpha = 1.0f;
                    if (use_mask) {
                        mask_alpha = std::clamp(mask->value[i], 0.0f, 1.0f);
                        if (mask_alpha <= 0.0f) {
                            continue;
                        }
                    }

                    const float r = img.r[i] * mask_alpha;
                    const float g = img.g[i] * mask_alpha;
                    const float b = img.b[i] * mask_alpha;
                    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    if (lum > peak_lum) {
                        peak_lum = lum;
                        peak_r = r;
                        peak_g = g;
                        peak_b = b;
                        peak_x = x;
                        peak_y = y;
                    }
                }
            }

            if (peak_x < 0 || peak_y < 0) {
                continue;
            }

            if (source_cap > 0.0f && peak_lum > source_cap) {
                const float scale = source_cap / peak_lum;
                peak_r *= scale;
                peak_g *= scale;
                peak_b *= scale;
                peak_lum = source_cap;
            }

            if (peak_lum <= threshold) {
                continue;
            }

            const float cx = static_cast<float>(peak_x) + 0.5f;
            const float cy = static_cast<float>(peak_y) + 0.5f;
            const float ndc_x = cx / img.width - 0.5f;
            const float ndc_y = cy / img.height - 0.5f;

            BrightPixel bp {};
            bp.angle_x = std::atan(ndc_x * 2.0f * tan_half_h);
            bp.angle_y = std::atan(ndc_y * 2.0f * tan_half_v);
            bp.r = peak_r;
            bp.g = peak_g;
            bp.b = peak_b;
            result.push_back(bp);
        }
    }

    return result;
}

void limit_bright_pixels(
    std::vector<BrightPixel>& pixels,
    std::size_t max_sources)
{
    if (max_sources == 0) {
        return;
    }

    if (pixels.size() <= max_sources) {
        return;
    }

    auto intensity = [](const BrightPixel& pixel) {
        return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
    };

    std::partial_sort(
        pixels.begin(),
        pixels.begin() + static_cast<std::ptrdiff_t>(max_sources),
        pixels.end(),
        [&](const BrightPixel& a, const BrightPixel& b) {
            return intensity(a) > intensity(b);
        });

    pixels.resize(max_sources);
}

int cluster_softening_radius_px(int radius_px, int downsample)
{
    return radius_px > 0 && 2 * radius_px > std::max(1, downsample) ? radius_px : 0;
}

void bright_pixel_block(
    const BrightPixel& pixel,
    int downsample,
    int image_width,
    int image_height,
    float tan_half_fov_h,
    float tan_half_fov_v,
    int& out_block_x,
    int& out_block_y)
{
    // Inverse of the angle mapping in extract_bright_pixels. The source sits
    // at a pixel centre, so flooring is safe against float error.
    const int stride = std::max(1, downsample);
    const float px = (std::tan(pixel.angle_x) / (2.0f * tan_half_fov_h) + 0.5f) * static_cast<float>(image_width);
    const float py = (std::tan(pixel.angle_y) / (2.0f * tan_half_fov_v) + 0.5f) * static_cast<float>(image_height);
    out_block_x = static_cast<int>(std::floor(px / static_cast<float>(stride)));
    out_block_y = static_cast<int>(std::floor(py / static_cast<float>(stride)));
}

void cluster_bright_pixels(
    std::vector<BrightPixel>& pixels,
    int radius_px,
    int downsample,
    int image_width,
    int image_height,
    float tan_half_fov_h,
    float tan_half_fov_v)
{
    if (radius_px <= 0 || image_width <= 0 || image_height <= 0 ||
        tan_half_fov_h <= 0.0f || tan_half_fov_v <= 0.0f || pixels.size() < 2) {
        return;
    }

    // Linking runs on the downsample blocks, not on source positions: a
    // source sits at its block's brightest pixel, so neighbouring sources of
    // one highlight can be up to two blocks apart, and a pixel radius below
    // that would split the highlight into scattered pieces. Two blocks link
    // when the gap between them is under the radius; touching blocks always
    // link. Links chain, so a bright region of any size is found whole.
    const std::int64_t stride = std::max(1, downsample);
    const std::int64_t radius_sq = static_cast<std::int64_t>(radius_px) * radius_px;
    auto gap_sq = [&](int dx, int dy) {
        const std::int64_t gx = std::max(0, std::abs(dx) - 1) * stride;
        const std::int64_t gy = std::max(0, std::abs(dy) - 1) * stride;
        return gx * gx + gy * gy;
    };

    // Cells of cell x cell blocks are small enough that any two sources in
    // one cell are linked, so the union-find runs over cells and only nearby
    // cells need pair tests.
    int cell = 2;
    while (gap_sq(cell, cell) < radius_sq) {
        ++cell;
    }
    const int max_block_delta = static_cast<int>(radius_px / stride) + 1;
    const int reach = (max_block_delta + cell - 1) / cell;
    auto floor_div = [](int value, int divisor) {
        return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
    };
    auto cell_key = [](int cx, int cy) {
        return (static_cast<std::int64_t>(cx) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(cy));
    };

    const std::size_t count = pixels.size();
    std::vector<std::pair<int, int>> blocks(count);
    std::unordered_map<std::int64_t, std::uint32_t> cell_index;
    std::vector<std::vector<std::uint32_t>> cell_members;
    std::vector<std::pair<int, int>> cell_coords;
    std::vector<std::uint32_t> cell_of(count);
    cell_index.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        bright_pixel_block(pixels[i], downsample, image_width, image_height, tan_half_fov_h, tan_half_fov_v,
                           blocks[i].first, blocks[i].second);
        const int cx = floor_div(blocks[i].first, cell);
        const int cy = floor_div(blocks[i].second, cell);
        const auto [it, inserted] = cell_index.try_emplace(cell_key(cx, cy), static_cast<std::uint32_t>(cell_members.size()));
        if (inserted) {
            cell_members.emplace_back();
            cell_coords.emplace_back(cx, cy);
        }
        cell_members[it->second].push_back(static_cast<std::uint32_t>(i));
        cell_of[i] = it->second;
    }

    std::vector<std::uint32_t> parent(cell_members.size());
    for (std::size_t c = 0; c < parent.size(); ++c) {
        parent[c] = static_cast<std::uint32_t>(c);
    }
    auto find = [&](std::uint32_t c) {
        while (parent[c] != c) {
            parent[c] = parent[parent[c]];
            c = parent[c];
        }
        return c;
    };

    auto cells_linked = [&](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
        for (const std::uint32_t i : a) {
            for (const std::uint32_t j : b) {
                if (gap_sq(blocks[i].first - blocks[j].first, blocks[i].second - blocks[j].second) < radius_sq) {
                    return true;
                }
            }
        }
        return false;
    };

    for (std::size_t c = 0; c < cell_members.size(); ++c) {
        const auto [cx, cy] = cell_coords[c];
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                const auto it = cell_index.find(cell_key(cx + dx, cy + dy));
                if (it == cell_index.end() || it->second <= c) {
                    continue;
                }
                const std::uint32_t ra = find(static_cast<std::uint32_t>(c));
                const std::uint32_t rb = find(it->second);
                if (ra != rb && cells_linked(cell_members[c], cell_members[it->second])) {
                    parent[rb] = ra;
                }
            }
        }
    }

    auto luma = [](const BrightPixel& pixel) {
        return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
    };

    struct Accumulator
    {
        double angle_x = 0.0;
        double angle_y = 0.0;
        double weight = 0.0;
        double plain_x = 0.0;
        double plain_y = 0.0;
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        std::size_t members = 0;
        std::size_t first = 0;
    };

    // Each linked region is split into an even grid of cells about twice the
    // radius across, and each occupied cell becomes one source carrying its
    // members' energy at their brightness-weighted centre. A region smaller
    // than a cell becomes a single source; a larger one keeps its outline at
    // that resolution, so its flare keeps its shape with far fewer sources.
    struct Region
    {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        int nx = 1;
        int ny = 1;
        std::size_t first_cell = 0;
    };
    std::vector<std::int64_t> region_of_root(cell_members.size(), -1);
    std::vector<std::uint32_t> region_of(count);
    std::vector<Region> regions;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t root = find(cell_of[i]);
        const auto [bx, by] = blocks[i];
        if (region_of_root[root] < 0) {
            region_of_root[root] = static_cast<std::int64_t>(regions.size());
            regions.push_back({bx, by, bx, by});
        } else {
            Region& region = regions[static_cast<std::size_t>(region_of_root[root])];
            region.min_x = std::min(region.min_x, bx);
            region.min_y = std::min(region.min_y, by);
            region.max_x = std::max(region.max_x, bx);
            region.max_y = std::max(region.max_y, by);
        }
        region_of[i] = static_cast<std::uint32_t>(region_of_root[root]);
    }
    const double cell_px = 2.0 * radius_px;
    std::size_t cell_total = 0;
    for (Region& region : regions) {
        const double width_px = static_cast<double>(region.max_x - region.min_x + 1) * static_cast<double>(stride);
        const double height_px = static_cast<double>(region.max_y - region.min_y + 1) * static_cast<double>(stride);
        region.nx = std::max(1, static_cast<int>(std::ceil(width_px / cell_px - 1.0e-9)));
        region.ny = std::max(1, static_cast<int>(std::ceil(height_px / cell_px - 1.0e-9)));
        region.first_cell = cell_total;
        cell_total += static_cast<std::size_t>(region.nx) * static_cast<std::size_t>(region.ny);
    }

    // Accumulate in source order so the result does not depend on hashing.
    std::vector<std::int64_t> slot_of_cell(cell_total, -1);
    std::vector<Accumulator> clusters;
    for (std::size_t i = 0; i < count; ++i) {
        const Region& region = regions[region_of[i]];
        const auto [bx, by] = blocks[i];
        const int ix = (bx - region.min_x) * region.nx / (region.max_x - region.min_x + 1);
        const int iy = (by - region.min_y) * region.ny / (region.max_y - region.min_y + 1);
        const std::size_t cell_slot = region.first_cell + static_cast<std::size_t>(iy) * region.nx + ix;
        if (slot_of_cell[cell_slot] < 0) {
            slot_of_cell[cell_slot] = static_cast<std::int64_t>(clusters.size());
            clusters.emplace_back();
            clusters.back().first = i;
        }
        Accumulator& acc = clusters[static_cast<std::size_t>(slot_of_cell[cell_slot])];
        const BrightPixel& p = pixels[i];
        const double w = std::max(0.0f, luma(p));
        acc.angle_x += p.angle_x * w;
        acc.angle_y += p.angle_y * w;
        acc.weight += w;
        acc.plain_x += p.angle_x;
        acc.plain_y += p.angle_y;
        acc.r += p.r;
        acc.g += p.g;
        acc.b += p.b;
        ++acc.members;
    }

    std::vector<BrightPixel> clustered;
    clustered.reserve(clusters.size());
    for (const Accumulator& acc : clusters) {
        BrightPixel merged {};
        if (acc.weight > 0.0) {
            merged.angle_x = static_cast<float>(acc.angle_x / acc.weight);
            merged.angle_y = static_cast<float>(acc.angle_y / acc.weight);
        } else {
            merged.angle_x = static_cast<float>(acc.plain_x / static_cast<double>(acc.members));
            merged.angle_y = static_cast<float>(acc.plain_y / static_cast<double>(acc.members));
        }
        merged.r = static_cast<float>(acc.r);
        merged.g = static_cast<float>(acc.g);
        merged.b = static_cast<float>(acc.b);
        clustered.push_back(merged);
    }

    // Brightest first, so Max Sources keeps the strongest clusters.
    std::stable_sort(clustered.begin(),
                     clustered.end(),
                     [&](const BrightPixel& a, const BrightPixel& b) {
                         return luma(a) > luma(b);
                     });

    pixels = std::move(clustered);
}
