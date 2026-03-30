#include "source_extract.h"

#include <algorithm>
#include <cmath>

std::vector<BrightPixel> extract_bright_pixels(
    const RgbImageView& img,
    float threshold,
    int downsample,
    float fov_h,
    float fov_v)
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

    for (int dy = 0; dy < dh; ++dy) {
        for (int dx = 0; dx < dw; ++dx) {
            float sum_r = 0.0f;
            float sum_g = 0.0f;
            float sum_b = 0.0f;
            int count = 0;

            const int y0 = dy * stride;
            const int x0 = dx * stride;
            const int y1 = std::min(y0 + stride, img.height);
            const int x1 = std::min(x0 + stride, img.width);

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const int i = y * img.width + x;
                    sum_r += img.r[i];
                    sum_g += img.g[i];
                    sum_b += img.b[i];
                    ++count;
                }
            }

            if (count == 0) {
                continue;
            }

            const float avg_r = sum_r / count;
            const float avg_g = sum_g / count;
            const float avg_b = sum_b / count;
            const float lum = 0.2126f * avg_r + 0.7152f * avg_g + 0.0722f * avg_b;
            if (lum <= threshold) {
                continue;
            }

            const float cx = (x0 + x1) * 0.5f;
            const float cy = (y0 + y1) * 0.5f;
            const float ndc_x = cx / img.width - 0.5f;
            const float ndc_y = cy / img.height - 0.5f;

            BrightPixel bp {};
            bp.angle_x = std::atan(ndc_x * 2.0f * tan_half_h);
            bp.angle_y = std::atan(ndc_y * 2.0f * tan_half_v);
            bp.r = avg_r;
            bp.g = avg_g;
            bp.b = avg_b;
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
