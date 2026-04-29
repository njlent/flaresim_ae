#include "frame_cuda.h"

#include "output_view.h"
#include "source_extract.h"

#include <cuda_runtime.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>
#include <thrust/sort.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace {

constexpr int kBlockSize2D = 16;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

__device__ __forceinline__ float d_clamp(float v, float lo, float hi)
{
    return fminf(fmaxf(v, lo), hi);
}

__device__ __forceinline__ float d_max3(float a, float b, float c)
{
    return fmaxf(a, fmaxf(b, c));
}

__host__ __device__ __forceinline__ float bright_pixel_luma(const BrightPixel& pixel)
{
    return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
}

__global__ void prepare_scene_kernel(const float* input_pixels,
                                     float* scene_pixels,
                                     int input_row_floats,
                                     int scene_row_floats,
                                     int width,
                                     int height,
                                     float threshold,
                                     float sky_brightness)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }

    const float* src =
        input_pixels + static_cast<std::size_t>(y) * input_row_floats + static_cast<std::size_t>(x) * 4u;
    float* dst =
        scene_pixels + static_cast<std::size_t>(y) * scene_row_floats + static_cast<std::size_t>(x) * 4u;

    float b = src[0];
    float g = src[1];
    float r = src[2];
    const float a = src[3];
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (lum <= threshold) {
        b *= sky_brightness;
        g *= sky_brightness;
        r *= sky_brightness;
    }

    dst[0] = b;
    dst[1] = g;
    dst[2] = r;
    dst[3] = a;
}

__global__ void extract_source_candidates_kernel(const float* scene_pixels,
                                                 int scene_row_floats,
                                                 const float* mask_pixels,
                                                 int mask_row_floats,
                                                 int width,
                                                 int height,
                                                 int stride,
                                                 float threshold,
                                                 float source_cap,
                                                 float tan_half_h,
                                                 float tan_half_v,
                                                 BrightPixel* out_candidates)
{
    const int dx = blockIdx.x * blockDim.x + threadIdx.x;
    const int dy = blockIdx.y * blockDim.y + threadIdx.y;
    const int dw = max(1, width / stride);
    const int dh = max(1, height / stride);
    if (dx >= dw || dy >= dh) {
        return;
    }
    const std::size_t out_index = static_cast<std::size_t>(dy) * static_cast<std::size_t>(dw) +
                                  static_cast<std::size_t>(dx);

    const int x0 = dx * stride;
    const int y0 = dy * stride;
    const int x1 = min(x0 + stride, width);
    const int y1 = min(y0 + stride, height);

    float peak_r = 0.0f;
    float peak_g = 0.0f;
    float peak_b = 0.0f;
    float peak_lum = -1.0f;
    int peak_x = -1;
    int peak_y = -1;

    for (int y = y0; y < y1; ++y) {
        const float* scene_row = scene_pixels + static_cast<std::size_t>(y) * scene_row_floats;
        const float* mask_row = mask_pixels ? (mask_pixels + static_cast<std::size_t>(y) * mask_row_floats) : nullptr;
        for (int x = x0; x < x1; ++x) {
            float mask_alpha = 1.0f;
            if (mask_row) {
                const float* mask_px = mask_row + static_cast<std::size_t>(x) * 4u;
                const float mask_visible =
                    d_clamp(d_max3(mask_px[2], mask_px[1], mask_px[0]), 0.0f, 1.0f);
                mask_alpha = mask_visible * d_clamp(mask_px[3], 0.0f, 1.0f);
                if (mask_alpha <= 0.0f) {
                    continue;
                }
            }

            const float* scene_px = scene_row + static_cast<std::size_t>(x) * 4u;
            const float r = scene_px[2] * mask_alpha;
            const float g = scene_px[1] * mask_alpha;
            const float b = scene_px[0] * mask_alpha;
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

    if (peak_x >= 0 && peak_y >= 0 && source_cap > 0.0f && peak_lum > source_cap) {
        const float scale = source_cap / peak_lum;
        peak_r *= scale;
        peak_g *= scale;
        peak_b *= scale;
        peak_lum = source_cap;
    }

    if (peak_x < 0 || peak_y < 0 || peak_lum <= threshold) {
        out_candidates[out_index] = BrightPixel {};
        return;
    }

    const float cx = static_cast<float>(peak_x) + 0.5f;
    const float cy = static_cast<float>(peak_y) + 0.5f;
    const float ndc_x = cx / static_cast<float>(width) - 0.5f;
    const float ndc_y = cy / static_cast<float>(height) - 0.5f;
    BrightPixel bp {};
    bp.angle_x = atanf(ndc_x * 2.0f * tan_half_h);
    bp.angle_y = atanf(ndc_y * 2.0f * tan_half_v);
    bp.r = peak_r;
    bp.g = peak_g;
    bp.b = peak_b;
    out_candidates[out_index] = bp;
}

struct BrightPixelIntensityGreater
{
    __host__ __device__ bool operator()(const BrightPixel& a, const BrightPixel& b) const
    {
        return bright_pixel_luma(a) > bright_pixel_luma(b);
    }
};

__global__ void clear_rgb_kernel(float* r, float* g, float* b, int count)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    r[i] = 0.0f;
    g[i] = 0.0f;
    b[i] = 0.0f;
}

__global__ void copy_rgb_kernel(const float* src_r,
                                const float* src_g,
                                const float* src_b,
                                float* dst_r,
                                float* dst_g,
                                float* dst_b,
                                int count)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    dst_r[i] = src_r[i];
    dst_g[i] = src_g[i];
    dst_b[i] = src_b[i];
}

__global__ void add_weighted_rgb_kernel(float* accum_r,
                                        float* accum_g,
                                        float* accum_b,
                                        const float* add_r,
                                        const float* add_g,
                                        const float* add_b,
                                        int count,
                                        float wr,
                                        float wg,
                                        float wb)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    accum_r[i] += add_r[i] * wr;
    accum_g[i] += add_g[i] * wg;
    accum_b[i] += add_b[i] * wb;
}

__global__ void scale_rgb_kernel(float* r, float* g, float* b, int count, float scale)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    r[i] *= scale;
    g[i] *= scale;
    b[i] *= scale;
}

__global__ void bright_pass_kernel(const float* scene_pixels,
                                   int scene_row_floats,
                                   float* out_r,
                                   float* out_g,
                                   float* out_b,
                                   int width,
                                   int height,
                                   float threshold)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const float* px =
        scene_pixels + static_cast<std::size_t>(y) * scene_row_floats + static_cast<std::size_t>(x) * 4u;
    const std::size_t i = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
    const float r = px[2];
    const float g = px[1];
    const float b = px[0];
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (lum > threshold) {
        const float excess = (lum - threshold) / fmaxf(lum, 1.0e-10f);
        out_r[i] = r * excess;
        out_g[i] = g * excess;
        out_b[i] = b * excess;
    } else {
        out_r[i] = 0.0f;
        out_g[i] = 0.0f;
        out_b[i] = 0.0f;
    }
}

__global__ void box_blur_horizontal_kernel(const float* src, float* dst, int width, int height, int radius)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k) {
        const int sx = min(max(x + k, 0), width - 1);
        sum += src[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(sx)];
    }
    dst[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] =
        sum / static_cast<float>((radius * 2) + 1);
}

__global__ void box_blur_vertical_kernel(const float* src, float* dst, int width, int height, int radius)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    float sum = 0.0f;
    for (int k = -radius; k <= radius; ++k) {
        const int sy = min(max(y + k, 0), height - 1);
        sum += src[static_cast<std::size_t>(sy) * width + static_cast<std::size_t>(x)];
    }
    dst[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] =
        sum / static_cast<float>((radius * 2) + 1);
}

__global__ void init_output_kernel(const float* input_pixels,
                                   const float* scene_pixels,
                                   float* output_pixels,
                                   int input_row_floats,
                                   int scene_row_floats,
                                   int output_row_floats,
                                   int width,
                                   int height,
                                   bool use_scene)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const float* input_px =
        input_pixels + static_cast<std::size_t>(y) * input_row_floats + static_cast<std::size_t>(x) * 4u;
    const float* scene_px = use_scene
        ? (scene_pixels + static_cast<std::size_t>(y) * scene_row_floats + static_cast<std::size_t>(x) * 4u)
        : nullptr;
    float* out_px =
        output_pixels + static_cast<std::size_t>(y) * output_row_floats + static_cast<std::size_t>(x) * 4u;
    out_px[0] = use_scene ? scene_px[0] : 0.0f;
    out_px[1] = use_scene ? scene_px[1] : 0.0f;
    out_px[2] = use_scene ? scene_px[2] : 0.0f;
    out_px[3] = input_px[3];
}

__global__ void add_rgb_to_output_kernel(const float* add_r,
                                         const float* add_g,
                                         const float* add_b,
                                         float* output_pixels,
                                         int output_row_floats,
                                         int width,
                                         int height)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const std::size_t i = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
    float* out_px =
        output_pixels + static_cast<std::size_t>(y) * output_row_floats + static_cast<std::size_t>(x) * 4u;
    out_px[0] += add_b[i];
    out_px[1] += add_g[i];
    out_px[2] += add_r[i];
}

__global__ void expand_alpha_kernel(const float* input_pixels,
                                    float* output_pixels,
                                    int input_row_floats,
                                    int output_row_floats,
                                    int width,
                                    int height)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }
    const float* input_px =
        input_pixels + static_cast<std::size_t>(y) * input_row_floats + static_cast<std::size_t>(x) * 4u;
    float* out_px =
        output_pixels + static_cast<std::size_t>(y) * output_row_floats + static_cast<std::size_t>(x) * 4u;
    const float rgb_alpha = d_clamp(d_max3(out_px[2], out_px[1], out_px[0]), 0.0f, 1.0f);
    out_px[3] = fmaxf(input_px[3], rgb_alpha);
}

__global__ void cluster_sorted_sources_kernel(const BrightPixel* sorted_candidates,
                                              int candidate_count,
                                              BrightPixel* out_sources,
                                              int max_sources,
                                              int cluster_radius_px,
                                              int image_width,
                                              float tan_half_fov_h)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }

    for (int i = 0; i < max_sources; ++i) {
        out_sources[i] = BrightPixel {};
    }

    if (candidate_count <= 0 || max_sources <= 0) {
        return;
    }

    const bool use_cluster = cluster_radius_px > 0 && image_width > 0 && tan_half_fov_h > 0.0f;
    const float angle_per_px = use_cluster ? (2.0f * tan_half_fov_h / static_cast<float>(image_width)) : 0.0f;
    const float threshold = use_cluster ? (static_cast<float>(cluster_radius_px) * angle_per_px) : 0.0f;
    const float threshold_sq = threshold * threshold;

    int out_count = 0;
    for (int candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
        const BrightPixel candidate = sorted_candidates[candidate_index];
        const float candidate_luma = bright_pixel_luma(candidate);
        if (!(candidate_luma > 0.0f)) {
            break;
        }

        bool merged = false;
        if (use_cluster) {
            for (int source_index = 0; source_index < out_count; ++source_index) {
                BrightPixel& merged_source = out_sources[source_index];
                const float delta_x = merged_source.angle_x - candidate.angle_x;
                const float delta_y = merged_source.angle_y - candidate.angle_y;
                if (delta_x * delta_x + delta_y * delta_y > threshold_sq) {
                    continue;
                }

                const float merged_luma = bright_pixel_luma(merged_source);
                const float sum_luma = merged_luma + candidate_luma;
                if (sum_luma > 0.0f) {
                    merged_source.angle_x =
                        (merged_source.angle_x * merged_luma + candidate.angle_x * candidate_luma) / sum_luma;
                    merged_source.angle_y =
                        (merged_source.angle_y * merged_luma + candidate.angle_y * candidate_luma) / sum_luma;
                }
                merged_source.r += candidate.r;
                merged_source.g += candidate.g;
                merged_source.b += candidate.b;
                merged = true;
                break;
            }
        }

        if (!merged && out_count < max_sources) {
            out_sources[out_count++] = candidate;
        }
    }
}

__global__ void draw_source_block_kernel(const BrightPixel* sources,
                                         int source_index,
                                         float tan_half_h,
                                         float tan_half_v,
                                         float* output_pixels,
                                         int output_row_floats,
                                         int width,
                                         int height,
                                         int block_size,
                                         int mode)
{
    const BrightPixel source = sources[source_index];
    if (!(bright_pixel_luma(source) > 0.0f)) {
        return;
    }

    const float ndc_x = tanf(source.angle_x) / (2.0f * tan_half_h);
    const float ndc_y = tanf(source.angle_y) / (2.0f * tan_half_v);
    const int px = static_cast<int>((ndc_x + 0.5f) * width);
    const int py = static_cast<int>((ndc_y + 0.5f) * height);
    const int local_x = static_cast<int>(blockIdx.x) * blockDim.x + static_cast<int>(threadIdx.x);
    const int local_y = static_cast<int>(blockIdx.y) * blockDim.y + static_cast<int>(threadIdx.y);
    if (local_x >= block_size || local_y >= block_size) {
        return;
    }

    const int x = px - block_size / 2 + local_x;
    const int y = py - block_size / 2 + local_y;
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }

    float* out_px =
        output_pixels + static_cast<std::size_t>(y) * output_row_floats + static_cast<std::size_t>(x) * 4u;
    if (mode == 0) {
        out_px[0] = source.b;
        out_px[1] = source.g;
        out_px[2] = source.r;
    } else {
        out_px[2] = fmaxf(out_px[2], source.r);
        out_px[1] *= 0.25f;
        out_px[0] *= 0.25f;
    }
}

__global__ void rasterize_haze_source_kernel(const BrightPixel* sources,
                                             int source_index,
                                             float tan_half_h,
                                             float tan_half_v,
                                             float* out_r,
                                             float* out_g,
                                             float* out_b,
                                             int width,
                                             int height,
                                             int block)
{
    const BrightPixel source = sources[source_index];
    if (!(bright_pixel_luma(source) > 0.0f)) {
        return;
    }

    const float ndc_x = tanf(source.angle_x) / (2.0f * tan_half_h);
    const float ndc_y = tanf(source.angle_y) / (2.0f * tan_half_v);
    const int px = static_cast<int>((ndc_x + 0.5f) * width);
    const int py = static_cast<int>((ndc_y + 0.5f) * height);
    const int local_x = static_cast<int>(blockIdx.x) * blockDim.x + static_cast<int>(threadIdx.x);
    const int local_y = static_cast<int>(blockIdx.y) * blockDim.y + static_cast<int>(threadIdx.y);
    if (local_x >= block || local_y >= block) {
        return;
    }
    const int x = px - block / 2 + local_x;
    const int y = py - block / 2 + local_y;
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    const std::size_t i = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
    out_r[i] += source.r;
    out_g[i] += source.g;
    out_b[i] += source.b;
}

__global__ void render_starburst_source_kernel(const BrightPixel* sources,
                                               int source_index,
                                               const float* psf,
                                               int psf_size,
                                               float gain,
                                               float scale,
                                               float tan_half_h,
                                               float tan_half_v,
                                               float* out_r,
                                               float* out_g,
                                               float* out_b,
                                               int width,
                                               int height,
                                               int span)
{
    const BrightPixel source = sources[source_index];
    if (!(bright_pixel_luma(source) > 0.0f)) {
        return;
    }

    constexpr float kWave[3] = {650.0f, 550.0f, 450.0f};
    const float src_rgb[3] = {source.r, source.g, source.b};
    const float src_px = width * 0.5f + (tanf(source.angle_x) / (2.0f * tan_half_h)) * width;
    const float src_py = height * 0.5f + (tanf(source.angle_y) / (2.0f * tan_half_v)) * height;
    const float diag = sqrtf(static_cast<float>(width * width + height * height));
    const float r_ref = scale * diag;
    const int local_x = static_cast<int>(blockIdx.x) * blockDim.x + static_cast<int>(threadIdx.x);
    const int local_y = static_cast<int>(blockIdx.y) * blockDim.y + static_cast<int>(threadIdx.y);
    if (local_x >= span || local_y >= span) {
        return;
    }
    const int x = static_cast<int>(floorf(src_px - r_ref)) + local_x;
    const int y = static_cast<int>(floorf(src_py - r_ref)) + local_y;
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    const float half_n = psf_size * 0.5f;
    float* out_channels[3] = {out_r, out_g, out_b};

    for (int ch = 0; ch < 3; ++ch) {
        const float inv_r = half_n / (r_ref * (kWave[ch] / 550.0f));
        const float dj_f = (static_cast<float>(y) - src_py) * inv_r + half_n;
        const float di_f = (static_cast<float>(x) - src_px) * inv_r + half_n;
        if (dj_f < 0.0f || di_f < 0.0f || dj_f >= static_cast<float>(psf_size - 1) ||
            di_f >= static_cast<float>(psf_size - 1)) {
            continue;
        }
        const int dj = static_cast<int>(dj_f);
        const int di = static_cast<int>(di_f);
        const float fj = dj_f - dj;
        const float fi = di_f - di;
        const float value =
            psf[static_cast<std::size_t>(dj) * psf_size + static_cast<std::size_t>(di)] * (1.0f - fi) * (1.0f - fj) +
            psf[static_cast<std::size_t>(dj) * psf_size + static_cast<std::size_t>(di + 1)] * fi * (1.0f - fj) +
            psf[static_cast<std::size_t>(dj + 1) * psf_size + static_cast<std::size_t>(di)] * (1.0f - fi) * fj +
            psf[static_cast<std::size_t>(dj + 1) * psf_size + static_cast<std::size_t>(di + 1)] * fi * fj;
        out_channels[ch][static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] +=
            src_rgb[ch] * gain * value;
    }
}

void report_error(const char* message, std::string* out_error)
{
    if (out_error) {
        *out_error = message;
    }
}

template <typename T>
void hash_append(std::uint64_t& hash, const T& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
}

std::uint64_t hash_ghost_setup(const LensSystem& lens,
                               float fov_h,
                               float fov_v,
                               int width,
                               int height,
                               const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append(hash, lens.focal_length);
    hash_append(hash, lens.sensor_z);
    const std::size_t surface_count = lens.surfaces.size();
    hash_append(hash, surface_count);
    for (const Surface& surface : lens.surfaces) {
        hash_append(hash, surface.radius);
        hash_append(hash, surface.thickness);
        hash_append(hash, surface.ior);
        hash_append(hash, surface.abbe_v);
        hash_append(hash, surface.semi_aperture);
        hash_append(hash, surface.coating);
        hash_append(hash, surface.is_stop);
        hash_append(hash, surface.z);
    }
    hash_append(hash, fov_h);
    hash_append(hash, fov_v);
    hash_append(hash, width);
    hash_append(hash, height);
    hash_append(hash, settings.ray_grid);
    hash_append(hash, settings.flare_gain);
    hash_append(hash, settings.spectral_samples);
    hash_append(hash, settings.aperture_blades);
    hash_append(hash, settings.aperture_rotation_deg);
    hash_append(hash, settings.ghost_cleanup_mode);
    hash_append(hash, settings.adaptive_sampling_strength);
    hash_append(hash, settings.max_adaptive_pair_grid);
    hash_append(hash, settings.projected_cells_mode);
    hash_append(hash, settings.pupil_jitter_mode);
    hash_append(hash, settings.pupil_jitter_seed);
    hash_append(hash, settings.cell_coverage_bias);
    hash_append(hash, settings.cell_edge_inset);
    return hash;
}

std::uint64_t hash_starburst_psf(const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append(hash, settings.aperture_blades);
    hash_append(hash, settings.aperture_rotation_deg);
    return hash;
}

bool ensure_device_bytes(void*& ptr, std::size_t& current_bytes, std::size_t needed_bytes, std::string* out_error)
{
    if (ptr && needed_bytes <= current_bytes) {
        return true;
    }
    cudaFree(ptr);
    ptr = nullptr;
    current_bytes = 0;
    const cudaError_t error = cudaMalloc(&ptr, needed_bytes);
    if (error != cudaSuccess) {
        report_error(cudaGetErrorString(error), out_error);
        return false;
    }
    current_bytes = needed_bytes;
    return true;
}

bool ensure_rgb_buffers(float*& r,
                        float*& g,
                        float*& b,
                        std::size_t& current_bytes,
                        std::size_t needed_floats,
                        std::string* out_error)
{
    const std::size_t needed_bytes = needed_floats * sizeof(float);
    return ensure_device_bytes(reinterpret_cast<void*&>(r), current_bytes, needed_bytes, out_error) &&
           ensure_device_bytes(reinterpret_cast<void*&>(g), current_bytes, needed_bytes, out_error) &&
           ensure_device_bytes(reinterpret_cast<void*&>(b), current_bytes, needed_bytes, out_error);
}

bool blur_channel(float* src, float* tmp, float* dst, int width, int height, int radius, int passes)
{
    if (radius < 1 || passes < 1) {
        return true;
    }
    const dim3 block(kBlockSize2D, kBlockSize2D, 1);
    const dim3 grid((width + kBlockSize2D - 1) / kBlockSize2D,
                    (height + kBlockSize2D - 1) / kBlockSize2D,
                    1);
    for (int pass = 0; pass < passes; ++pass) {
        box_blur_horizontal_kernel<<<grid, block>>>(src, tmp, width, height, radius);
        box_blur_vertical_kernel<<<grid, block>>>(tmp, dst, width, height, radius);
        cudaMemcpy(src,
                   dst,
                   static_cast<std::size_t>(width) * height * sizeof(float),
                   cudaMemcpyDeviceToDevice);
    }
    return cudaGetLastError() == cudaSuccess;
}

} // namespace

void GpuFrameRenderCache::release()
{
    ghost_cache.release();
    cudaFree(d_scene_bgra);
    cudaFree(d_bloom_r); cudaFree(d_bloom_g); cudaFree(d_bloom_b);
    cudaFree(d_haze_r); cudaFree(d_haze_g); cudaFree(d_haze_b);
    cudaFree(d_starburst_r); cudaFree(d_starburst_g); cudaFree(d_starburst_b);
    cudaFree(d_work0_r); cudaFree(d_work0_g); cudaFree(d_work0_b);
    cudaFree(d_work1_r); cudaFree(d_work1_g); cudaFree(d_work1_b);
    cudaFree(d_candidates);
    cudaFree(d_sources);
    cudaFree(d_starburst_psf);
    d_scene_bgra = nullptr; scene_floats = 0;
    d_bloom_r = d_bloom_g = d_bloom_b = nullptr; bloom_floats = 0;
    d_haze_r = d_haze_g = d_haze_b = nullptr; haze_floats = 0;
    d_starburst_r = d_starburst_g = d_starburst_b = nullptr; starburst_floats = 0;
    d_work0_r = d_work0_g = d_work0_b = nullptr; work0_floats = 0;
    d_work1_r = d_work1_g = d_work1_b = nullptr; work1_floats = 0;
    d_candidates = nullptr; candidate_capacity = 0;
    d_sources = nullptr; source_capacity = 0;
    d_starburst_psf = nullptr; starburst_psf_floats = 0; starburst_psf_size = 0;
    ghost_setup_key = 0; has_ghost_setup = false; ghost_setup = {};
    starburst_psf_key = 0; starburst_psf = {};
}

bool render_frame_cuda_bgra128(const LensSystem& lens,
                               const FrameRenderSettings& settings,
                               AeOutputView view,
                               const float* input_pixels,
                               float* output_pixels,
                               int width,
                               int height,
                               int input_row_floats,
                               int output_row_floats,
                               const float* mask_pixels,
                               int mask_row_floats,
                               GpuFrameRenderCache& cache,
                               GhostRenderBackend* out_ghost_backend,
                               std::string* out_error)
{
    if (!input_pixels || !output_pixels || width <= 0 || height <= 0 || input_row_floats < width * 4 ||
        output_row_floats < width * 4) {
        report_error("Invalid GPU frame buffers.", out_error);
        return false;
    }

    const FrameRenderPlan plan = build_output_view_render_plan(view);
    const bool need_sources = plan.need_source_output || plan.need_flare || plan.need_haze || plan.need_starburst;
    const int source_slots = std::max(0, settings.max_sources);
    float fov_h = 0.0f;
    float fov_v = 0.0f;
    if (need_sources && !compute_camera_fov(settings, width, height, fov_h, fov_v)) {
        report_error("Failed to compute camera FOV.", out_error);
        return false;
    }

    if (!ensure_device_bytes(reinterpret_cast<void*&>(cache.d_scene_bgra),
                             cache.scene_floats,
                             static_cast<std::size_t>(width) * height * 4u * sizeof(float),
                             out_error)) {
        return false;
    }

    const int scene_row_floats = width * 4;
    const std::size_t np = static_cast<std::size_t>(width) * height;
    const int count = static_cast<int>(np);
    const dim3 block2d(kBlockSize2D, kBlockSize2D, 1);
    const dim3 grid2d((width + kBlockSize2D - 1) / kBlockSize2D,
                      (height + kBlockSize2D - 1) / kBlockSize2D,
                      1);
    const int grid1d = (count + 255) / 256;

    prepare_scene_kernel<<<grid2d, block2d>>>(input_pixels,
                                              cache.d_scene_bgra,
                                              input_row_floats,
                                              scene_row_floats,
                                              width,
                                              height,
                                              settings.threshold,
                                              settings.sky_brightness);
    if (cudaGetLastError() != cudaSuccess) {
        report_error("prepare_scene_kernel failed.", out_error);
        return false;
    }

    if (need_sources && source_slots > 0) {
        const int stride = std::max(1, settings.downsample);
        const int dw = std::max(1, width / stride);
        const int dh = std::max(1, height / stride);
        const std::size_t max_candidates = static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh);
        if (!ensure_device_bytes(reinterpret_cast<void*&>(cache.d_candidates),
                                 cache.candidate_capacity,
                                 max_candidates * sizeof(BrightPixel),
                                 out_error) ||
            !ensure_device_bytes(reinterpret_cast<void*&>(cache.d_sources),
                                 cache.source_capacity,
                                 static_cast<std::size_t>(source_slots) * sizeof(BrightPixel),
                                 out_error)) {
            return false;
        }
        const dim3 source_grid((dw + kBlockSize2D - 1) / kBlockSize2D,
                               (dh + kBlockSize2D - 1) / kBlockSize2D,
                               1);
        extract_source_candidates_kernel<<<source_grid, block2d>>>(cache.d_scene_bgra,
                                                                   scene_row_floats,
                                                                   mask_pixels,
                                                                   mask_pixels ? (mask_row_floats > 0 ? mask_row_floats : input_row_floats) : 0,
                                                                   width,
                                                                   height,
                                                                   stride,
                                                                   settings.threshold,
                                                                   settings.source_cap,
                                                                   std::tan(fov_h * 0.5f),
                                                                   std::tan(fov_v * 0.5f),
                                                                   cache.d_candidates);
        if (cudaGetLastError() != cudaSuccess) {
            report_error("extract_source_candidates_kernel failed.", out_error);
            return false;
        }

        try {
            auto candidates_begin = thrust::device_pointer_cast(cache.d_candidates);
            thrust::sort(thrust::device,
                         candidates_begin,
                         candidates_begin + static_cast<std::ptrdiff_t>(max_candidates),
                         BrightPixelIntensityGreater {});
        } catch (const std::exception& error) {
            report_error(error.what(), out_error);
            return false;
        }

        const int precluster_count =
            std::min<int>(static_cast<int>(max_candidates), std::max(source_slots * 8, source_slots));
        cluster_sorted_sources_kernel<<<1, 1>>>(cache.d_candidates,
                                                precluster_count,
                                                cache.d_sources,
                                                source_slots,
                                                settings.cluster_radius_px,
                                                width,
                                                std::tan(fov_h * 0.5f));
        if (cudaGetLastError() != cudaSuccess) {
            report_error("cluster_sorted_sources_kernel failed.", out_error);
            return false;
        }
    }

    if (plan.need_flare && source_slots > 0) {
        GhostConfig ghost {};
        ghost.ray_grid = settings.ray_grid;
        ghost.min_intensity = settings.min_ghost;
        ghost.gain = settings.flare_gain;
        ghost.spectral_samples = settings.spectral_samples;
        ghost.aperture_blades = settings.aperture_blades;
        ghost.aperture_rotation_deg = settings.aperture_rotation_deg;
        ghost.ghost_normalize = settings.ghost_normalize;
        ghost.max_area_boost = settings.max_area_boost;
        ghost.cleanup_mode = settings.ghost_cleanup_mode;
        ghost.adaptive_sampling_strength = settings.adaptive_sampling_strength;
        ghost.footprint_radius_bias = settings.footprint_radius_bias;
        ghost.footprint_clamp = settings.footprint_clamp;
        ghost.max_adaptive_pair_grid = settings.max_adaptive_pair_grid;
        ghost.projected_cells_mode = settings.projected_cells_mode;
        ghost.pupil_jitter = settings.pupil_jitter_mode;
        ghost.pupil_jitter_seed = settings.pupil_jitter_seed;
        ghost.cell_coverage_bias = settings.cell_coverage_bias;
        ghost.cell_edge_inset = settings.cell_edge_inset;

        const std::uint64_t setup_key = hash_ghost_setup(lens, fov_h, fov_v, width, height, settings);
        if (!cache.has_ghost_setup || cache.ghost_setup_key != setup_key) {
            if (!build_ghost_render_setup(lens, fov_h, fov_v, width, height, ghost, cache.ghost_setup)) {
                report_error("Failed to build ghost setup.", out_error);
                return false;
            }
            cache.has_ghost_setup = true;
            cache.ghost_setup_key = setup_key;
        }

        const float sensor_half_w = lens.focal_length * std::tan(fov_h * 0.5f);
        const float sensor_half_h = lens.focal_length * std::tan(fov_v * 0.5f);
        if (!launch_ghost_cuda_device(lens,
                                      cache.ghost_setup,
                                      cache.d_sources,
                                      source_slots,
                                      sensor_half_w,
                                      sensor_half_h,
                                      width,
                                      height,
                                      ghost,
                                      cache.ghost_cache,
                                      out_error)) {
            return false;
        }
        if (settings.ghost_blur > 0.0f && settings.ghost_blur_passes > 0 &&
            (settings.ghost_cleanup_mode == GhostCleanupMode::LegacyBlur ||
             settings.ghost_cleanup_mode == GhostCleanupMode::SharpAdaptivePlusBlur)) {
            const int radius = std::max(1,
                                        static_cast<int>(std::round(settings.ghost_blur *
                                                                    std::sqrt(static_cast<float>(width * width + height * height)))));
            if (!ensure_rgb_buffers(cache.d_work0_r, cache.d_work0_g, cache.d_work0_b, cache.work0_floats, np, out_error) ||
                !ensure_rgb_buffers(cache.d_work1_r, cache.d_work1_g, cache.d_work1_b, cache.work1_floats, np, out_error) ||
                !blur_channel(cache.ghost_cache.d_out_r, cache.d_work0_r, cache.d_work1_r, width, height, radius, settings.ghost_blur_passes) ||
                !blur_channel(cache.ghost_cache.d_out_g, cache.d_work0_g, cache.d_work1_g, width, height, radius, settings.ghost_blur_passes) ||
                !blur_channel(cache.ghost_cache.d_out_b, cache.d_work0_b, cache.d_work1_b, width, height, radius, settings.ghost_blur_passes)) {
                report_error("Failed to blur flare buffers.", out_error);
                return false;
            }
        }
        if (out_ghost_backend) {
            *out_ghost_backend = GhostRenderBackend::CUDA;
        }
    } else if (out_ghost_backend) {
        *out_ghost_backend = GhostRenderBackend::CPU;
    }

    if (plan.need_haze) {
        if (!ensure_rgb_buffers(cache.d_haze_r, cache.d_haze_g, cache.d_haze_b, cache.haze_floats, np, out_error)) {
            return false;
        }
        clear_rgb_kernel<<<grid1d, 256>>>(cache.d_haze_r, cache.d_haze_g, cache.d_haze_b, count);
        if (source_slots > 0 && settings.haze_gain > 0.0f) {
            const int haze_block = std::max(1, settings.downsample);
            const float tan_half_h = std::tan(fov_h * 0.5f);
            const float tan_half_v = std::tan(fov_v * 0.5f);
            const dim3 haze_grid((haze_block + kBlockSize2D - 1) / kBlockSize2D,
                                 (haze_block + kBlockSize2D - 1) / kBlockSize2D,
                                 1);
            for (int source_index = 0; source_index < source_slots; ++source_index) {
                rasterize_haze_source_kernel<<<haze_grid, block2d>>>(cache.d_sources,
                                                                     source_index,
                                                                     tan_half_h,
                                                                     tan_half_v,
                                                                     cache.d_haze_r,
                                                                     cache.d_haze_g,
                                                                     cache.d_haze_b,
                                                                     width,
                                                                     height,
                                                                     haze_block);
            }
            if (settings.haze_radius > 0.0f && settings.haze_blur_passes > 0) {
                const int radius = std::max(1,
                                            static_cast<int>(std::round(settings.haze_radius *
                                                                        std::sqrt(static_cast<float>(width * width + height * height)))));
                if (!ensure_rgb_buffers(cache.d_work0_r, cache.d_work0_g, cache.d_work0_b, cache.work0_floats, np, out_error) ||
                    !ensure_rgb_buffers(cache.d_work1_r, cache.d_work1_g, cache.d_work1_b, cache.work1_floats, np, out_error) ||
                    !blur_channel(cache.d_haze_r, cache.d_work0_r, cache.d_work1_r, width, height, radius, settings.haze_blur_passes) ||
                    !blur_channel(cache.d_haze_g, cache.d_work0_g, cache.d_work1_g, width, height, radius, settings.haze_blur_passes) ||
                    !blur_channel(cache.d_haze_b, cache.d_work0_b, cache.d_work1_b, width, height, radius, settings.haze_blur_passes)) {
                    report_error("Failed to blur haze buffers.", out_error);
                    return false;
                }
            }
            scale_rgb_kernel<<<grid1d, 256>>>(cache.d_haze_r, cache.d_haze_g, cache.d_haze_b, count, settings.haze_gain);
        }
    }

    if (plan.need_starburst) {
        if (!ensure_rgb_buffers(cache.d_starburst_r, cache.d_starburst_g, cache.d_starburst_b, cache.starburst_floats, np, out_error)) {
            return false;
        }
        clear_rgb_kernel<<<grid1d, 256>>>(cache.d_starburst_r, cache.d_starburst_g, cache.d_starburst_b, count);
        if (source_slots > 0 && settings.starburst_gain > 0.0f) {
            const std::uint64_t psf_key = hash_starburst_psf(settings);
            if (cache.starburst_psf_key != psf_key || cache.starburst_psf.empty()) {
                StarburstConfig config {};
                config.aperture_blades = settings.aperture_blades;
                config.aperture_rotation_deg = settings.aperture_rotation_deg;
                compute_starburst_psf(config, cache.starburst_psf);
                cache.starburst_psf_key = psf_key;
                cache.starburst_psf_size = cache.starburst_psf.N;
                if (!ensure_device_bytes(reinterpret_cast<void*&>(cache.d_starburst_psf),
                                         cache.starburst_psf_floats,
                                         cache.starburst_psf.data.size() * sizeof(float),
                                         out_error)) {
                    return false;
                }
                cudaMemcpy(cache.d_starburst_psf,
                           cache.starburst_psf.data.data(),
                           cache.starburst_psf.data.size() * sizeof(float),
                           cudaMemcpyHostToDevice);
            }

            const float tan_half_h = std::tan(fov_h * 0.5f);
            const float tan_half_v = std::tan(fov_v * 0.5f);
            const float diag = std::sqrt(static_cast<float>(width * width + height * height));
            const float radius = settings.starburst_scale * diag * (650.0f / 550.0f);
            const int span = std::max(1, static_cast<int>(std::ceil(radius * 2.0f)) + 1);
            const dim3 starburst_grid((span + kBlockSize2D - 1) / kBlockSize2D,
                                      (span + kBlockSize2D - 1) / kBlockSize2D,
                                      1);
            for (int source_index = 0; source_index < source_slots; ++source_index) {
                render_starburst_source_kernel<<<starburst_grid, block2d>>>(cache.d_sources,
                                                                            source_index,
                                                                            cache.d_starburst_psf,
                                                                            cache.starburst_psf_size,
                                                                            settings.starburst_gain,
                                                                            settings.starburst_scale,
                                                                            tan_half_h,
                                                                            tan_half_v,
                                                                            cache.d_starburst_r,
                                                                            cache.d_starburst_g,
                                                                            cache.d_starburst_b,
                                                                            width,
                                                                            height,
                                                                            span);
            }
        }
    }

    if (plan.need_bloom) {
        if (!ensure_rgb_buffers(cache.d_bloom_r, cache.d_bloom_g, cache.d_bloom_b, cache.bloom_floats, np, out_error) ||
            !ensure_rgb_buffers(cache.d_work0_r, cache.d_work0_g, cache.d_work0_b, cache.work0_floats, np, out_error) ||
            !ensure_rgb_buffers(cache.d_work1_r, cache.d_work1_g, cache.d_work1_b, cache.work1_floats, np, out_error)) {
            return false;
        }
        clear_rgb_kernel<<<grid1d, 256>>>(cache.d_bloom_r, cache.d_bloom_g, cache.d_bloom_b, count);
        if (settings.bloom.strength > 0.0f) {
            bright_pass_kernel<<<grid2d, block2d>>>(
                cache.d_scene_bgra, scene_row_floats, cache.d_work0_r, cache.d_work0_g, cache.d_work0_b, width, height, settings.bloom.threshold);
            static const float chroma_r[] = {1.00f, 1.00f, 1.00f, 1.00f, 0.95f, 0.80f};
            static const float chroma_g[] = {1.00f, 0.90f, 0.65f, 0.40f, 0.20f, 0.10f};
            static const float chroma_b[] = {1.00f, 0.55f, 0.22f, 0.08f, 0.03f, 0.01f};
            float octave_weight = 1.0f;
            const int octaves = std::clamp(settings.bloom.octaves, 1, 6);
            const int passes = std::clamp(settings.bloom.passes, 1, 10);
            const int base_kernel = std::max(static_cast<int>(settings.bloom.radius *
                                                               std::sqrt(static_cast<float>(width * width + height * height))),
                                             1);
            for (int oct = 0; oct < octaves; ++oct) {
                copy_rgb_kernel<<<grid1d, 256>>>(cache.d_work0_r,
                                                 cache.d_work0_g,
                                                 cache.d_work0_b,
                                                 cache.d_work1_r,
                                                 cache.d_work1_g,
                                                 cache.d_work1_b,
                                                 count);
                int radius = base_kernel;
                for (int k = 0; k < oct; ++k) {
                    radius = static_cast<int>(radius * 2.5f);
                }
                radius = std::max(1, std::min(radius, static_cast<int>(std::sqrt(static_cast<float>(width * width + height * height)) * 0.25f)));
                if (!blur_channel(cache.d_work1_r, cache.d_work0_r, cache.d_work1_r, width, height, radius, passes) ||
                    !blur_channel(cache.d_work1_g, cache.d_work0_g, cache.d_work1_g, width, height, radius, passes) ||
                    !blur_channel(cache.d_work1_b, cache.d_work0_b, cache.d_work1_b, width, height, radius, passes)) {
                    report_error("Failed to blur bloom buffers.", out_error);
                    return false;
                }
                const int ci = std::min(oct, 5);
                add_weighted_rgb_kernel<<<grid1d, 256>>>(cache.d_bloom_r,
                                                         cache.d_bloom_g,
                                                         cache.d_bloom_b,
                                                         cache.d_work1_r,
                                                         cache.d_work1_g,
                                                         cache.d_work1_b,
                                                         count,
                                                         octave_weight * settings.bloom.strength * (settings.bloom.chromatic ? chroma_r[ci] : 1.0f),
                                                         octave_weight * settings.bloom.strength * (settings.bloom.chromatic ? chroma_g[ci] : 1.0f),
                                                         octave_weight * settings.bloom.strength * (settings.bloom.chromatic ? chroma_b[ci] : 1.0f));
                octave_weight *= 0.55f;
            }
        }
    }

    init_output_kernel<<<grid2d, block2d>>>(input_pixels,
                                            cache.d_scene_bgra,
                                            output_pixels,
                                            input_row_floats,
                                            scene_row_floats,
                                            output_row_floats,
                                            width,
                                            height,
                                            view == AeOutputView::Composite || view == AeOutputView::Diagnostics);
    if (view == AeOutputView::Composite || view == AeOutputView::FlareOnly) {
        if (plan.need_flare && cache.ghost_cache.d_out_r) {
            add_rgb_to_output_kernel<<<grid2d, block2d>>>(
                cache.ghost_cache.d_out_r, cache.ghost_cache.d_out_g, cache.ghost_cache.d_out_b, output_pixels, output_row_floats, width, height);
        }
        if (plan.need_haze) {
            add_rgb_to_output_kernel<<<grid2d, block2d>>>(
                cache.d_haze_r, cache.d_haze_g, cache.d_haze_b, output_pixels, output_row_floats, width, height);
        }
        if (plan.need_starburst) {
            add_rgb_to_output_kernel<<<grid2d, block2d>>>(
                cache.d_starburst_r, cache.d_starburst_g, cache.d_starburst_b, output_pixels, output_row_floats, width, height);
        }
    }
    if (view == AeOutputView::Composite || view == AeOutputView::BloomOnly) {
        if (plan.need_bloom) {
            add_rgb_to_output_kernel<<<grid2d, block2d>>>(
                cache.d_bloom_r, cache.d_bloom_g, cache.d_bloom_b, output_pixels, output_row_floats, width, height);
        }
    }
    if (view == AeOutputView::Sources || view == AeOutputView::Diagnostics) {
        const float tan_half_h = std::tan(fov_h * 0.5f);
        const float tan_half_v = std::tan(fov_v * 0.5f);
        const int block = std::max(3, settings.downsample);
        const dim3 source_grid((block + kBlockSize2D - 1) / kBlockSize2D,
                               (block + kBlockSize2D - 1) / kBlockSize2D,
                               1);
        for (int source_index = 0; source_index < source_slots; ++source_index) {
            draw_source_block_kernel<<<source_grid, block2d>>>(cache.d_sources,
                                                               source_index,
                                                               tan_half_h,
                                                               tan_half_v,
                                                               output_pixels,
                                                               output_row_floats,
                                                               width,
                                                               height,
                                                               block,
                                                               view == AeOutputView::Sources ? 0 : 1);
        }
    }

    expand_alpha_kernel<<<grid2d, block2d>>>(
        input_pixels, output_pixels, input_row_floats, output_row_floats, width, height);
    const cudaError_t sync_error = cudaDeviceSynchronize();
    if (sync_error != cudaSuccess) {
        report_error(cudaGetErrorString(sync_error), out_error);
        return false;
    }

    return true;
}
