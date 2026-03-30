#include "bloom.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

void compute_box_radii(float sigma, int num_passes, std::vector<int>& radii)
{
    radii.resize(num_passes);
    if (num_passes <= 0) {
        return;
    }

    const float ideal = std::sqrt((12.0f * sigma * sigma / num_passes) + 1.0f);
    int w_lo = std::max((int)std::floor(ideal), 1);
    if ((w_lo % 2) == 0) {
        --w_lo;
    }
    if (w_lo < 1) {
        w_lo = 1;
    }
    const int w_hi = w_lo + 2;
    const float target_var = sigma * sigma;
    const float var_lo = (w_lo * w_lo - 1.0f) / 12.0f;
    const float var_hi = (w_hi * w_hi - 1.0f) / 12.0f;
    const int n_hi = (var_hi > var_lo + 1e-6f)
        ? std::clamp((int)std::round((target_var - num_passes * var_lo) / (var_hi - var_lo)), 0, num_passes)
        : 0;

    for (int i = 0; i < num_passes; ++i) {
        radii[i] = ((i < n_hi) ? w_hi : w_lo) / 2;
    }
}

void box_blur_pass(const std::vector<float>& src_r,
                   const std::vector<float>& src_g,
                   const std::vector<float>& src_b,
                   std::vector<float>& dst_r,
                   std::vector<float>& dst_g,
                   std::vector<float>& dst_b,
                   int width,
                   int height,
                   int radius)
{
    const float inv_w = 1.0f / (2 * radius + 1);
    const size_t np = (size_t)width * height;

    std::vector<float> tmp_r(np), tmp_g(np), tmp_b(np);

    for (int y = 0; y < height; ++y) {
        const int row = y * width;
        float sr = 0.0f;
        float sg = 0.0f;
        float sb = 0.0f;
        for (int k = -radius; k <= radius; ++k) {
            const int sx = std::clamp(k, 0, width - 1);
            sr += src_r[row + sx];
            sg += src_g[row + sx];
            sb += src_b[row + sx];
        }
        tmp_r[row] = sr * inv_w;
        tmp_g[row] = sg * inv_w;
        tmp_b[row] = sb * inv_w;

        for (int x = 1; x < width; ++x) {
            const int add = std::min(x + radius, width - 1);
            const int rem = std::clamp(x - radius - 1, 0, width - 1);
            sr += src_r[row + add] - src_r[row + rem];
            sg += src_g[row + add] - src_g[row + rem];
            sb += src_b[row + add] - src_b[row + rem];
            tmp_r[row + x] = sr * inv_w;
            tmp_g[row + x] = sg * inv_w;
            tmp_b[row + x] = sb * inv_w;
        }
    }

    for (int x = 0; x < width; ++x) {
        float sr = 0.0f;
        float sg = 0.0f;
        float sb = 0.0f;
        for (int k = -radius; k <= radius; ++k) {
            const int sy = std::clamp(k, 0, height - 1);
            const int idx = sy * width + x;
            sr += tmp_r[idx];
            sg += tmp_g[idx];
            sb += tmp_b[idx];
        }
        dst_r[x] = sr * inv_w;
        dst_g[x] = sg * inv_w;
        dst_b[x] = sb * inv_w;

        for (int y = 1; y < height; ++y) {
            const int add_row = std::min(y + radius, height - 1);
            const int rem_row = std::clamp(y - radius - 1, 0, height - 1);
            sr += tmp_r[add_row * width + x] - tmp_r[rem_row * width + x];
            sg += tmp_g[add_row * width + x] - tmp_g[rem_row * width + x];
            sb += tmp_b[add_row * width + x] - tmp_b[rem_row * width + x];
            dst_r[y * width + x] = sr * inv_w;
            dst_g[y * width + x] = sg * inv_w;
            dst_b[y * width + x] = sb * inv_w;
        }
    }
}

} // namespace

void generate_bloom(
    const RgbImageView& input,
    MutableRgbImageView output,
    const BloomConfig& config)
{
    if (!input.r || !input.g || !input.b ||
        !output.r || !output.g || !output.b ||
        input.width <= 0 || input.height <= 0 ||
        input.width != output.width || input.height != output.height ||
        config.strength < 1e-6f) {
        return;
    }

    const int w = input.width;
    const int h = input.height;
    const size_t np = (size_t)w * h;
    const float diag = std::sqrt((float)(w * w + h * h));
    const int base_kernel = std::max((int)(config.radius * diag), 1);

    std::vector<float> bright_r(np, 0.0f), bright_g(np, 0.0f), bright_b(np, 0.0f);
    for (size_t i = 0; i < np; ++i) {
        const float lum = 0.2126f * input.r[i] + 0.7152f * input.g[i] + 0.0722f * input.b[i];
        if (lum > config.threshold) {
            const float excess = (lum - config.threshold) / std::max(lum, 1e-10f);
            bright_r[i] = input.r[i] * excess;
            bright_g[i] = input.g[i] * excess;
            bright_b[i] = input.b[i] * excess;
        }
    }

    static const float chroma_r[] = {1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 0.80f};
    static const float chroma_g[] = {1.00f, 0.90f, 0.65f, 0.40f, 0.20f, 0.10f};
    static const float chroma_b[] = {1.00f, 0.55f, 0.22f, 0.08f, 0.03f, 0.01f};

    float octave_weight = 1.0f;
    constexpr float weight_decay = 0.55f;
    const int octaves = std::clamp(config.octaves, 1, 6);
    const int passes = std::clamp(config.passes, 1, 10);

    for (int oct = 0; oct < octaves; ++oct) {
        float oct_radius = (float)base_kernel;
        for (int k = 0; k < oct; ++k) {
            oct_radius *= 2.5f;
        }
        const int oct_kernel = std::min((int)oct_radius, (int)(diag * 0.25f));
        const float oct_sigma = oct_kernel / 3.0f;

        std::vector<int> box_radii;
        compute_box_radii(oct_sigma, passes, box_radii);

        const int ci = std::min(oct, 5);
        const float tint_r = config.chromatic ? chroma_r[ci] : 1.0f;
        const float tint_g = config.chromatic ? chroma_g[ci] : 1.0f;
        const float tint_b = config.chromatic ? chroma_b[ci] : 1.0f;

        std::vector<float> blur_r(bright_r.begin(), bright_r.end());
        std::vector<float> blur_g(bright_g.begin(), bright_g.end());
        std::vector<float> blur_b(bright_b.begin(), bright_b.end());
        std::vector<float> tmp_r(np), tmp_g(np), tmp_b(np);

        for (int pass = 0; pass < passes; ++pass) {
            box_blur_pass(blur_r, blur_g, blur_b, tmp_r, tmp_g, tmp_b, w, h, box_radii[pass]);
            std::swap(blur_r, tmp_r);
            std::swap(blur_g, tmp_g);
            std::swap(blur_b, tmp_b);
        }

        const float wr = octave_weight * tint_r * config.strength;
        const float wg = octave_weight * tint_g * config.strength;
        const float wb = octave_weight * tint_b * config.strength;
        for (size_t i = 0; i < np; ++i) {
            output.r[i] += wr * blur_r[i];
            output.g[i] += wg * blur_g[i];
            output.b[i] += wb * blur_b[i];
        }

        octave_weight *= weight_decay;
    }
}
