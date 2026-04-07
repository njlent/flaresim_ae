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
