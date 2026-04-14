#include "source_extract.h"

#include <algorithm>
#include <cmath>

std::vector<BrightPixel> extract_bright_pixels(
    const RgbImageView& img,
    float threshold,
    int downsample,
    float fov_h,
    float fov_v,
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

void cluster_bright_pixels(
    std::vector<BrightPixel>& pixels,
    int radius_px,
    int image_width,
    float tan_half_fov_h)
{
    if (radius_px <= 0 || image_width <= 0 || tan_half_fov_h <= 0.0f || pixels.size() < 2) {
        return;
    }

    const float angle_per_px = 2.0f * tan_half_fov_h / static_cast<float>(image_width);
    const float threshold = static_cast<float>(radius_px) * angle_per_px;
    const float threshold_sq = threshold * threshold;

    auto luma = [](const BrightPixel& pixel) {
        return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
    };

    std::sort(pixels.begin(),
              pixels.end(),
              [&](const BrightPixel& a, const BrightPixel& b) {
                  return luma(a) > luma(b);
              });

    const int count = static_cast<int>(pixels.size());
    std::vector<bool> consumed(static_cast<std::size_t>(count), false);
    std::vector<BrightPixel> clustered;
    clustered.reserve(pixels.size());

    for (int i = 0; i < count; ++i) {
        if (consumed[static_cast<std::size_t>(i)]) {
            continue;
        }

        const BrightPixel& seed = pixels[static_cast<std::size_t>(i)];
        const float seed_luma = luma(seed);
        float weighted_angle_x = seed.angle_x * seed_luma;
        float weighted_angle_y = seed.angle_y * seed_luma;
        float sum_luma = seed_luma;
        float sum_r = seed.r;
        float sum_g = seed.g;
        float sum_b = seed.b;

        for (int j = i + 1; j < count; ++j) {
            if (consumed[static_cast<std::size_t>(j)]) {
                continue;
            }

            const BrightPixel& candidate = pixels[static_cast<std::size_t>(j)];
            const float delta_x = seed.angle_x - candidate.angle_x;
            const float delta_y = seed.angle_y - candidate.angle_y;
            if (delta_x * delta_x + delta_y * delta_y > threshold_sq) {
                continue;
            }

            const float candidate_luma = luma(candidate);
            weighted_angle_x += candidate.angle_x * candidate_luma;
            weighted_angle_y += candidate.angle_y * candidate_luma;
            sum_luma += candidate_luma;
            sum_r += candidate.r;
            sum_g += candidate.g;
            sum_b += candidate.b;
            consumed[static_cast<std::size_t>(j)] = true;
        }

        BrightPixel merged {};
        merged.angle_x = sum_luma > 0.0f ? weighted_angle_x / sum_luma : seed.angle_x;
        merged.angle_y = sum_luma > 0.0f ? weighted_angle_y / sum_luma : seed.angle_y;
        merged.r = sum_r;
        merged.g = sum_g;
        merged.b = sum_b;
        clustered.push_back(merged);
    }

    pixels = std::move(clustered);
}
