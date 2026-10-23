#include "ps_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kMax8 = 255.0f;
constexpr float kMax16 = 32768.0f;
constexpr float kMinAlpha = 1.0e-6f;

float sanitize(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

float read_sample(const unsigned char* row, int depth, int index)
{
    switch (depth) {
        case 8:
            return row[index] / kMax8;
        case 16: {
            std::uint16_t value = 0;
            std::memcpy(&value, row + static_cast<std::size_t>(index) * 2u, sizeof(value));
            return std::min(static_cast<float>(value), kMax16) / kMax16;
        }
        case 32: {
            float value = 0.0f;
            std::memcpy(&value, row + static_cast<std::size_t>(index) * 4u, sizeof(value));
            return sanitize(value);
        }
        default:
            return 0.0f;
    }
}

void write_sample(unsigned char* row, int depth, int index, float value)
{
    value = sanitize(value);
    switch (depth) {
        case 8:
            row[index] = static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 1.0f) * kMax8));
            return;
        case 16: {
            const auto packed = static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * kMax16));
            std::memcpy(row + static_cast<std::size_t>(index) * 2u, &packed, sizeof(packed));
            return;
        }
        case 32:
            std::memcpy(row + static_cast<std::size_t>(index) * 4u, &value, sizeof(value));
            return;
        default:
            return;
    }
}

bool valid_depth(int depth)
{
    return depth == 8 || depth == 16 || depth == 32;
}

bool image_ok(const FloatImageBuffer& image)
{
    const std::size_t np = static_cast<std::size_t>(std::max(image.width, 0)) *
                           static_cast<std::size_t>(std::max(image.height, 0));
    return image.width > 0 && image.height > 0 && image.r.size() == np && image.g.size() == np &&
           image.b.size() == np && image.alpha.size() == np;
}

float luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

std::uint8_t encode_display(float value, bool linear_to_srgb)
{
    value = std::clamp(sanitize(value), 0.0f, 1.0f);
    if (linear_to_srgb) {
        value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
    return static_cast<std::uint8_t>(std::lround(value * 255.0f));
}

} // namespace

bool ps_read_pixels(const PsPixelBuffer& src,
                    const PsChannelMap& map,
                    bool premultiply,
                    FloatImageBuffer& dst,
                    int dst_x,
                    int dst_y)
{
    if (!src.data || !valid_depth(src.depth) || src.planes < 3 || !image_ok(dst) ||
        dst_x < 0 || dst_y < 0 || dst_x + src.width > dst.width || dst_y + src.height > dst.height ||
        map.alpha_plane >= src.planes) {
        return false;
    }

    const auto* base = static_cast<const unsigned char*>(src.data);
    for (int y = 0; y < src.height; ++y) {
        const unsigned char* row = base + static_cast<std::ptrdiff_t>(y) * src.row_bytes;
        std::size_t out = static_cast<std::size_t>(dst_y + y) * static_cast<std::size_t>(dst.width) +
                          static_cast<std::size_t>(dst_x);
        for (int x = 0; x < src.width; ++x, ++out) {
            const int first = x * src.planes;
            float r = read_sample(row, src.depth, first + 0);
            float g = read_sample(row, src.depth, first + 1);
            float b = read_sample(row, src.depth, first + 2);
            float a = 1.0f;
            if (map.alpha_plane >= 0) {
                a = std::clamp(read_sample(row, src.depth, first + map.alpha_plane), 0.0f, 1.0f);
                if (premultiply) {
                    r *= a;
                    g *= a;
                    b *= a;
                }
            }
            dst.r[out] = r;
            dst.g[out] = g;
            dst.b[out] = b;
            dst.alpha[out] = a;
        }
    }
    return true;
}

bool ps_write_pixels(const FloatImageBuffer& src,
                     int src_x,
                     int src_y,
                     const PsChannelMap& map,
                     bool unpremultiply,
                     PsPixelBuffer& dst)
{
    if (!dst.data || !valid_depth(dst.depth) || dst.planes < 3 || !image_ok(src) ||
        src_x < 0 || src_y < 0 || src_x + dst.width > src.width || src_y + dst.height > src.height ||
        map.alpha_plane >= dst.planes) {
        return false;
    }

    auto* base = static_cast<unsigned char*>(dst.data);
    for (int y = 0; y < dst.height; ++y) {
        unsigned char* row = base + static_cast<std::ptrdiff_t>(y) * dst.row_bytes;
        std::size_t in = static_cast<std::size_t>(src_y + y) * static_cast<std::size_t>(src.width) +
                         static_cast<std::size_t>(src_x);
        for (int x = 0; x < dst.width; ++x, ++in) {
            float r = src.r[in];
            float g = src.g[in];
            float b = src.b[in];
            const float a = std::clamp(sanitize(src.alpha[in]), 0.0f, 1.0f);
            if (unpremultiply) {
                if (a > kMinAlpha) {
                    const float inv = 1.0f / a;
                    r *= inv;
                    g *= inv;
                    b *= inv;
                } else {
                    r = g = b = 0.0f;
                }
            }
            const int first = x * dst.planes;
            write_sample(row, dst.depth, first + 0, r);
            write_sample(row, dst.depth, first + 1, g);
            write_sample(row, dst.depth, first + 2, b);
            if (map.alpha_plane >= 0) {
                write_sample(row, dst.depth, first + map.alpha_plane, a);
            }
        }
    }
    return true;
}

bool ps_build_preview_proxy(const FloatImageBuffer& full,
                            int factor,
                            float peak_threshold,
                            FloatImageBuffer& out_proxy)
{
    if (!image_ok(full) || factor < 1) {
        return false;
    }

    const int width = std::max(1, full.width / factor);
    const int height = std::max(1, full.height / factor);
    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    out_proxy.width = width;
    out_proxy.height = height;
    out_proxy.r.assign(np, 0.0f);
    out_proxy.g.assign(np, 0.0f);
    out_proxy.b.assign(np, 0.0f);
    out_proxy.alpha.assign(np, 0.0f);

    for (int py = 0; py < height; ++py) {
        const int y0 = py * factor;
        const int y1 = std::min(full.height, y0 + factor);
        for (int px = 0; px < width; ++px) {
            const int x0 = px * factor;
            const int x1 = std::min(full.width, x0 + factor);

            double sum_r = 0.0;
            double sum_g = 0.0;
            double sum_b = 0.0;
            double sum_a = 0.0;
            float peak_lum = -1.0f;
            std::size_t peak_index = 0;
            int count = 0;
            for (int y = y0; y < y1; ++y) {
                std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(full.width) +
                                static_cast<std::size_t>(x0);
                for (int x = x0; x < x1; ++x, ++i) {
                    sum_r += full.r[i];
                    sum_g += full.g[i];
                    sum_b += full.b[i];
                    sum_a += full.alpha[i];
                    const float lum = luminance(full.r[i], full.g[i], full.b[i]);
                    if (lum > peak_lum) {
                        peak_lum = lum;
                        peak_index = i;
                    }
                    ++count;
                }
            }

            const std::size_t o = static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(px);
            if (count == 0) {
                continue;
            }
            if (peak_lum > peak_threshold) {
                out_proxy.r[o] = full.r[peak_index];
                out_proxy.g[o] = full.g[peak_index];
                out_proxy.b[o] = full.b[peak_index];
                out_proxy.alpha[o] = full.alpha[peak_index];
            } else {
                const double inv = 1.0 / count;
                out_proxy.r[o] = static_cast<float>(sum_r * inv);
                out_proxy.g[o] = static_cast<float>(sum_g * inv);
                out_proxy.b[o] = static_cast<float>(sum_b * inv);
                out_proxy.alpha[o] = static_cast<float>(sum_a * inv);
            }
        }
    }
    return true;
}

void ps_scale_state_for_proxy(AeParameterState& state, int factor)
{
    if (factor <= 1) {
        return;
    }

    const int full_stride = std::max(state.downsample, 1);
    const int proxy_stride =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(full_stride) / factor)));
    // Per-pixel flare brightness goes as gain / stride^2 in render pixels, so
    // keep gain / stride^2 constant across the proxy stride change.
    const float ratio = static_cast<float>(proxy_stride) / static_cast<float>(full_stride);
    state.downsample = proxy_stride;
    state.flare_gain *= ratio * ratio;
    state.cluster_radius_px = static_cast<int>(std::lround(static_cast<double>(state.cluster_radius_px) / factor));
}

void ps_make_display_bitmap(const FloatImageBuffer& image,
                            bool linear_to_srgb,
                            int dst_width,
                            int dst_height,
                            std::vector<std::uint32_t>& out_bgra)
{
    out_bgra.clear();
    if (!image_ok(image) || dst_width <= 0 || dst_height <= 0) {
        return;
    }
    out_bgra.resize(static_cast<std::size_t>(dst_width) * static_cast<std::size_t>(dst_height));

    const double sx = static_cast<double>(image.width) / dst_width;
    const double sy = static_cast<double>(image.height) / dst_height;
    for (int y = 0; y < dst_height; ++y) {
        const int y0 = std::min(image.height - 1, static_cast<int>(y * sy));
        const int y1 = std::clamp(static_cast<int>(std::ceil((y + 1) * sy)), y0 + 1, image.height);
        for (int x = 0; x < dst_width; ++x) {
            const int x0 = std::min(image.width - 1, static_cast<int>(x * sx));
            const int x1 = std::clamp(static_cast<int>(std::ceil((x + 1) * sx)), x0 + 1, image.width);

            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
            double a = 0.0;
            for (int yy = y0; yy < y1; ++yy) {
                std::size_t i = static_cast<std::size_t>(yy) * static_cast<std::size_t>(image.width) +
                                static_cast<std::size_t>(x0);
                for (int xx = x0; xx < x1; ++xx, ++i) {
                    r += sanitize(image.r[i]);
                    g += sanitize(image.g[i]);
                    b += sanitize(image.b[i]);
                    a += std::clamp(sanitize(image.alpha[i]), 0.0f, 1.0f);
                }
            }
            const double inv = 1.0 / (static_cast<double>(x1 - x0) * static_cast<double>(y1 - y0));
            const float alpha = static_cast<float>(a * inv);
            const float checker = (((x >> 3) + (y >> 3)) & 1) ? 0.8f : 0.6f;
            const float backdrop = (linear_to_srgb ? checker * checker : checker) * (1.0f - alpha);

            const std::uint32_t pr = encode_display(static_cast<float>(r * inv) + backdrop, linear_to_srgb);
            const std::uint32_t pg = encode_display(static_cast<float>(g * inv) + backdrop, linear_to_srgb);
            const std::uint32_t pb = encode_display(static_cast<float>(b * inv) + backdrop, linear_to_srgb);
            out_bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_width) + static_cast<std::size_t>(x)] =
                pb | (pg << 8) | (pr << 16) | 0xFF000000u;
        }
    }
}
