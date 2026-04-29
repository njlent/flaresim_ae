// ============================================================================
// ghost.cpp — Ghost reflection rendering
//
// For each ghost bounce pair (a, b), traces a grid of rays through the
// lens system.  Each ray enters the front element as a collimated beam
// at the angle of a bright source pixel, reflects at surfaces a and b,
// and lands on the sensor plane.  The contribution is splatted onto the
// output image with bilinear weighting.
//
// Pre-filtering: a single on-axis ray is traced per pair to estimate the
// Fresnel weight.  Pairs below the min_intensity threshold are skipped.
// ============================================================================

#include "ghost.h"
#include "ghost_cuda.h"
#include "trace.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>
#include <atomic>
#include <array>
#include <cstdint>

#ifdef _OPENMP
#include <omp.h>
#endif

// ---------------------------------------------------------------------------
// Enumerate all ghost pairs: every combination of 2 surfaces.
// ---------------------------------------------------------------------------

std::vector<GhostPair> enumerate_ghost_pairs(const LensSystem &lens)
{
    std::vector<GhostPair> pairs;
    int N = lens.num_surfaces();
    for (int a = 0; a < N; ++a)
        for (int b = a + 1; b < N; ++b)
            pairs.push_back({a, b});
    return pairs;
}

const char* ghost_render_backend_name(GhostRenderBackend backend)
{
    switch (backend) {
        case GhostRenderBackend::CPU:
            return "CPU";
        case GhostRenderBackend::CUDA:
            return "CUDA";
        default:
            return "Unknown";
    }
}

int select_ghost_pair_ray_grid(int base_ray_grid,
                               float estimated_extent_px,
                               float distortion_score,
                               float adaptive_quality,
                               float adaptive_sampling_strength,
                               int max_adaptive_pair_grid)
{
    if (base_ray_grid <= 0) {
        return 0;
    }

    const float quality = std::clamp(adaptive_quality, 0.25f, 2.0f);
    const float strength = std::clamp(adaptive_sampling_strength, 0.0f, 2.0f);
    const int quality_grid = std::max(4, static_cast<int>(std::lround(base_ray_grid * quality)));
    const int min_grid = std::max(4, quality_grid / 2);
    const int auto_max_grid = std::max(quality_grid, static_cast<int>(std::lround(base_ray_grid * quality * 2.0f)));
    const int max_grid = max_adaptive_pair_grid > 0
        ? std::max(quality_grid, max_adaptive_pair_grid)
        : auto_max_grid;
    const float approx_valid_samples = std::max(0.78539816339f * quality_grid * quality_grid, 1.0f);
    const float linear_density = std::sqrt(approx_valid_samples);
    const float sample_spacing_px = estimated_extent_px / linear_density;
    const float promote_spacing = std::max(1.5f, 3.5f - strength * 1.25f) / std::sqrt(quality);
    const float demote_spacing = std::max(0.75f, 1.25f + (1.0f - strength) * 0.4f) * std::sqrt(quality);
    const float promote_distortion = std::max(0.02f, 0.12f - strength * 0.04f) / quality;
    const float demote_distortion = std::max(0.0f, 0.03f + (1.0f - strength) * 0.02f) * quality;

    if (strength > 0.0f && (sample_spacing_px > promote_spacing || distortion_score > promote_distortion)) {
        return max_grid;
    }
    if (strength < 1.75f && sample_spacing_px < demote_spacing && distortion_score < demote_distortion) {
        return min_grid;
    }
    return quality_grid;
}

// ---------------------------------------------------------------------------
// Bilinear splat: distribute a contribution to 4 neighbouring pixels.
// ---------------------------------------------------------------------------

using GridSample = GhostGridSample;
using GridCell = GhostGridCell;

struct PixelPoint
{
    float x, y;
};

static inline void splat_tent(float *buf, int w, int h,
                              float px, float py, float value, float radius);

struct GhostSampleFootprint
{
    float area_px2 = 0.0f;
    float axis_u_px = 0.0f;
    float axis_v_px = 0.0f;
    float anisotropy = 1.0f;
    bool valid = false;
};

static bool is_valid_pupil_sample(float u,
                                  float v,
                                  int aperture_blades,
                                  float aperture_rotation_deg)
{
    const float r2 = u * u + v * v;
    if (r2 > 1.0f) {
        return false;
    }

    if (aperture_blades < 3) {
        return true;
    }

    const float rotation = aperture_rotation_deg * 3.14159265358979323846f / 180.0f;
    const float sector_angle = 2.0f * 3.14159265358979323846f / aperture_blades;
    const float apothem = std::cos(3.14159265358979323846f / aperture_blades);
    float angle = std::atan2(v, u) - rotation;
    float sector = std::fmod(angle, sector_angle);
    if (sector < 0.0f) {
        sector += sector_angle;
    }

    return std::sqrt(r2) * std::cos(sector - sector_angle * 0.5f) <= apothem;
}

static bool trace_ghost_sensor_position_px(const LensSystem& lens,
                                           int bounce_a,
                                           int bounce_b,
                                           const Vec3f& beam_dir,
                                           float u,
                                           float v,
                                           float wavelength_nm,
                                           float front_radius,
                                           float start_z,
                                           float sensor_half_w,
                                           float sensor_half_h,
                                           int width,
                                           int height,
                                           float& out_px,
                                           float& out_py,
                                           float* out_weight = nullptr)
{
    Ray ray;
    ray.origin = Vec3f(u * front_radius, v * front_radius, start_z);
    ray.dir = beam_dir;

    const TraceResult res = trace_ghost_ray(ray, lens, bounce_a, bounce_b, wavelength_nm);
    if (!res.valid) {
        return false;
    }

    out_px = (res.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
    out_py = (res.position.y / (2.0f * sensor_half_h) + 0.5f) * height;
    if (out_weight) {
        *out_weight = res.weight;
    }
    return std::isfinite(out_px) && std::isfinite(out_py);
}

static bool choose_probe_offset(float u,
                                float v,
                                float step,
                                bool along_u,
                                int aperture_blades,
                                float aperture_rotation_deg,
                                float& out_probe_u,
                                float& out_probe_v,
                                float& out_delta)
{
    const float primary_u = along_u ? (u + step) : u;
    const float primary_v = along_u ? v : (v + step);
    if (is_valid_pupil_sample(primary_u, primary_v, aperture_blades, aperture_rotation_deg)) {
        out_probe_u = primary_u;
        out_probe_v = primary_v;
        out_delta = step;
        return true;
    }

    const float secondary_u = along_u ? (u - step) : u;
    const float secondary_v = along_u ? v : (v - step);
    if (is_valid_pupil_sample(secondary_u, secondary_v, aperture_blades, aperture_rotation_deg)) {
        out_probe_u = secondary_u;
        out_probe_v = secondary_v;
        out_delta = -step;
        return true;
    }

    return false;
}

float select_ghost_footprint_radius(float fallback_radius_px,
                                    float footprint_area_px2,
                                    float anisotropy,
                                    float footprint_radius_bias,
                                    float footprint_clamp)
{
    const float fallback = std::clamp(fallback_radius_px, 1.25f, 16.0f);
    if (!(footprint_area_px2 > 0.0f) || !std::isfinite(footprint_area_px2)) {
        return fallback;
    }

    const float bias = std::clamp(footprint_radius_bias, 0.25f, 2.0f);
    const float clamp_scale = std::clamp(footprint_clamp, 0.5f, 4.0f);
    float radius = std::sqrt(footprint_area_px2) * 0.75f * bias;
    if (std::isfinite(anisotropy) && anisotropy > 2.5f) {
        radius *= 0.85f;
    }

    radius = std::clamp(radius, 1.15f, 16.0f);
    return std::clamp(std::min(radius, fallback * clamp_scale), 1.15f, 16.0f);
}

float select_ghost_density_boost(float pair_area_boost,
                                 float reference_footprint_area_px2,
                                 float local_footprint_area_px2)
{
    const float pair_boost = std::max(pair_area_boost, 1.0f);
    if (!(reference_footprint_area_px2 > 0.0f) ||
        !(local_footprint_area_px2 > 0.0f) ||
        !std::isfinite(reference_footprint_area_px2) ||
        !std::isfinite(local_footprint_area_px2)) {
        return pair_boost;
    }

    // Blend pair-wide normalization with local Jacobian area. Larger projected
    // footprints get a bit more compensation; compressed regions rely more on
    // their naturally smaller deposition footprint for sharpness.
    const float area_ratio = local_footprint_area_px2 / reference_footprint_area_px2;
    const float local_scale = std::clamp(std::sqrt(area_ratio), 0.5f, 2.5f);
    return pair_boost * local_scale;
}

bool select_ghost_cell_rasterization(float estimated_extent_px,
                                     float distortion_score)
{
    return estimated_extent_px >= 24.0f || distortion_score >= 0.08f;
}

static float signed_triangle_area(const PixelPoint& a,
                                  const PixelPoint& b,
                                  const PixelPoint& c)
{
    return 0.5f * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

static void rasterize_triangle_linear(float* buf,
                                      int width,
                                      int height,
                                      const PixelPoint& a,
                                      const PixelPoint& b,
                                      const PixelPoint& c,
                                      float va,
                                      float vb,
                                      float vc,
                                      float density_scale)
{
    const float signed_area = signed_triangle_area(a, b, c);
    const float area = std::abs(signed_area);
    if (!(area > 1.0e-4f) || !std::isfinite(area) || density_scale <= 1.0e-14f) {
        return;
    }

    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}) - 0.5f)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}) + 0.5f)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}) - 0.5f)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}) + 0.5f)));
    const float inv_twice_signed_area = 1.0f / (2.0f * signed_area);

    for (int y = y0; y <= y1; ++y) {
        const float py = y + 0.5f;
        const int row = y * width;
        for (int x = x0; x <= x1; ++x) {
            const float px = x + 0.5f;
            const float w0 = ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) * inv_twice_signed_area;
            const float w1 = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) * inv_twice_signed_area;
            const float w2 = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) * inv_twice_signed_area;
            if (w0 >= -1.0e-4f && w1 >= -1.0e-4f && w2 >= -1.0e-4f) {
                const float density = std::max(0.0f, va * w2 + vb * w0 + vc * w1);
                buf[row + x] += density * density_scale;
            }
        }
    }
}

static void rasterize_quad_linear(float* buf,
                                  int width,
                                  int height,
                                  const PixelPoint& p00,
                                  const PixelPoint& p10,
                                  const PixelPoint& p11,
                                  const PixelPoint& p01,
                                  float v00,
                                  float v10,
                                  float v11,
                                  float v01,
                                  float total_value)
{
    const float area0 = std::abs(signed_triangle_area(p00, p10, p11));
    const float area1 = std::abs(signed_triangle_area(p00, p11, p01));
    const float total_area = area0 + area1;
    if (!(total_area > 1.0e-4f) || !std::isfinite(total_area) || total_value <= 1.0e-14f) {
        const float center_x = 0.25f * (p00.x + p10.x + p11.x + p01.x);
        const float center_y = 0.25f * (p00.y + p10.y + p11.y + p01.y);
        splat_tent(buf, width, height, center_x, center_y, total_value, 1.5f);
        return;
    }

    const float weighted_integral =
        area0 * (v00 + v10 + v11) / 3.0f +
        area1 * (v00 + v11 + v01) / 3.0f;
    if (!(weighted_integral > 1.0e-6f) || !std::isfinite(weighted_integral)) {
        const float center_x = 0.25f * (p00.x + p10.x + p11.x + p01.x);
        const float center_y = 0.25f * (p00.y + p10.y + p11.y + p01.y);
        splat_tent(buf, width, height, center_x, center_y, total_value, 1.5f);
        return;
    }

    const float density_scale = total_value / weighted_integral;
    rasterize_triangle_linear(buf, width, height, p00, p10, p11, v00, v10, v11, density_scale);
    rasterize_triangle_linear(buf, width, height, p00, p11, p01, v00, v11, v01, density_scale);
}

static GhostSampleFootprint estimate_ghost_sample_footprint(const LensSystem& lens,
                                                            int bounce_a,
                                                            int bounce_b,
                                                            const Vec3f& beam_dir,
                                                            float u,
                                                            float v,
                                                            float cell_size,
                                                            float front_radius,
                                                            float start_z,
                                                            float sensor_half_w,
                                                            float sensor_half_h,
                                                            int width,
                                                            int height,
                                                            const GhostConfig& config)
{
    GhostSampleFootprint footprint;

    float center_px = 0.0f;
    float center_py = 0.0f;
    if (!trace_ghost_sensor_position_px(lens,
                                        bounce_a,
                                        bounce_b,
                                        beam_dir,
                                        u,
                                        v,
                                        config.wavelengths[1],
                                        front_radius,
                                        start_z,
                                        sensor_half_w,
                                        sensor_half_h,
                                        width,
                                        height,
                                        center_px,
                                        center_py)) {
        return footprint;
    }

    const float probe_step = std::max(cell_size * 0.5f, 1.0e-3f);

    float probe_u_u = 0.0f;
    float probe_u_v = 0.0f;
    float delta_u = 0.0f;
    float probe_v_u = 0.0f;
    float probe_v_v = 0.0f;
    float delta_v = 0.0f;
    if (!choose_probe_offset(u, v, probe_step, true,
                             config.aperture_blades,
                             config.aperture_rotation_deg,
                             probe_u_u, probe_u_v, delta_u) ||
        !choose_probe_offset(u, v, probe_step, false,
                             config.aperture_blades,
                             config.aperture_rotation_deg,
                             probe_v_u, probe_v_v, delta_v)) {
        return footprint;
    }

    float probe_px_u = 0.0f;
    float probe_py_u = 0.0f;
    float probe_px_v = 0.0f;
    float probe_py_v = 0.0f;
    if (!trace_ghost_sensor_position_px(lens,
                                        bounce_a,
                                        bounce_b,
                                        beam_dir,
                                        probe_u_u,
                                        probe_u_v,
                                        config.wavelengths[1],
                                        front_radius,
                                        start_z,
                                        sensor_half_w,
                                        sensor_half_h,
                                        width,
                                        height,
                                        probe_px_u,
                                        probe_py_u) ||
        !trace_ghost_sensor_position_px(lens,
                                        bounce_a,
                                        bounce_b,
                                        beam_dir,
                                        probe_v_u,
                                        probe_v_v,
                                        config.wavelengths[1],
                                        front_radius,
                                        start_z,
                                        sensor_half_w,
                                        sensor_half_h,
                                        width,
                                        height,
                                        probe_px_v,
                                        probe_py_v)) {
        return footprint;
    }

    const float inv_delta_u = 1.0f / delta_u;
    const float inv_delta_v = 1.0f / delta_v;
    const float dpx_du = (probe_px_u - center_px) * inv_delta_u;
    const float dpy_du = (probe_py_u - center_py) * inv_delta_u;
    const float dpx_dv = (probe_px_v - center_px) * inv_delta_v;
    const float dpy_dv = (probe_py_v - center_py) * inv_delta_v;

    footprint.axis_u_px = std::sqrt(dpx_du * dpx_du + dpy_du * dpy_du) * cell_size;
    footprint.axis_v_px = std::sqrt(dpx_dv * dpx_dv + dpy_dv * dpy_dv) * cell_size;
    const float minor_axis = std::max(std::min(footprint.axis_u_px, footprint.axis_v_px), 1.0e-3f);
    const float major_axis = std::max(footprint.axis_u_px, footprint.axis_v_px);
    footprint.anisotropy = major_axis / minor_axis;
    footprint.area_px2 = std::abs(dpx_du * dpy_dv - dpy_du * dpx_dv) * cell_size * cell_size;
    footprint.valid =
        std::isfinite(footprint.area_px2) &&
        std::isfinite(footprint.axis_u_px) &&
        std::isfinite(footprint.axis_v_px) &&
        footprint.area_px2 > 0.0f;
    return footprint;
}

static inline void splat_bilinear(float *buf, int w, int h,
                                  float px, float py, float value)
{
    int x0 = (int)std::floor(px - 0.5f);
    int y0 = (int)std::floor(py - 0.5f);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = (px - 0.5f) - x0;
    float fy = (py - 0.5f) - y0;

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
        buf[y0 * w + x0] += value * w00;
    if (x1 >= 0 && x1 < w && y0 >= 0 && y0 < h)
        buf[y0 * w + x1] += value * w10;
    if (x0 >= 0 && x0 < w && y1 >= 0 && y1 < h)
        buf[y1 * w + x0] += value * w01;
    if (x1 >= 0 && x1 < w && y1 >= 0 && y1 < h)
        buf[y1 * w + x1] += value * w11;
}

// ---------------------------------------------------------------------------
// Tent-filter splat: distribute a contribution over a wider footprint.
//
// Energy-conserving: weights sum to 1, so total deposited energy = value.
// Falls back to bilinear when radius <= 1.5 for efficiency.
// ---------------------------------------------------------------------------

static inline void splat_tent(float *buf, int w, int h,
                              float px, float py, float value, float radius)
{
    if (radius <= 1.5f)
    {
        splat_bilinear(buf, w, h, px, py, value);
        return;
    }

    int ix0 = std::max((int)std::floor(px - radius), 0);
    int ix1 = std::min((int)std::ceil(px + radius), w - 1);
    int iy0 = std::max((int)std::floor(py - radius), 0);
    int iy1 = std::min((int)std::ceil(py + radius), h - 1);

    float inv_r = 1.0f / radius;

    // Analytical normalisation: the integral of a 2D tent over [-r,r]² = r².
    // (Each 1D integral of max(0, 1-|x|/r) over [-r,r] = r, so 2D = r*r.)
    float norm = value / (radius * radius);

    for (int y = iy0; y <= iy1; ++y)
    {
        float wy = std::max(1.0f - std::abs(y + 0.5f - py) * inv_r, 0.0f);
        float wy_norm = wy * norm;
        int row = y * w;
        for (int x = ix0; x <= ix1; ++x)
        {
            float wx = std::max(1.0f - std::abs(x + 0.5f - px) * inv_r, 0.0f);
            buf[row + x] += wy_norm * wx;
        }
    }
}

// ---------------------------------------------------------------------------
// Pre-filter: trace a single on-axis ray through each ghost pair.
// Returns the average Fresnel weight across RGB wavelengths.
// ---------------------------------------------------------------------------

static float estimate_ghost_intensity(const LensSystem &lens,
                                      int bounce_a, int bounce_b,
                                      const GhostConfig &config)
{
    // On-axis ray at the centre of the entrance pupil
    Ray ray;
    ray.origin = Vec3f(0, 0, lens.surfaces[0].z - 20.0f);
    ray.dir = Vec3f(0, 0, 1);

    float total = 0;
    for (int ch = 0; ch < 3; ++ch)
    {
        TraceResult r = trace_ghost_ray(ray, lens, bounce_a, bounce_b,
                                        config.wavelengths[ch]);
        if (r.valid)
            total += r.weight;
    }
    return total / 3.0f;
}

// ---------------------------------------------------------------------------
// Estimate the ghost image spread for a bounce pair relative to the sensor.
//
// Traces a coarse grid of on-axis rays across the entrance pupil and
// measures the bounding box of sensor landing positions.  Returns a
// correction factor = ghost_area / sensor_area, clamped to [1, max_boost].
//
// Defocused ghost pairs produce images much larger than the sensor,
// diluting per-pixel brightness.  This correction factor compensates
// for that geometric dilution so all ghost pairs remain visible.
// ---------------------------------------------------------------------------

static float estimate_ghost_spread(const LensSystem &lens,
                                   int bounce_a, int bounce_b,
                                   float sensor_half_w, float sensor_half_h,
                                   const GhostConfig &config,
                                   float *out_ghost_w_mm = nullptr,
                                   float *out_ghost_h_mm = nullptr)
{
    constexpr int G = 8; // coarse grid for spread estimation
    float front_R = lens.surfaces[0].semi_aperture;
    float start_z = lens.surfaces[0].z - 20.0f;

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;
    int valid_count = 0;

    for (int gy = 0; gy < G; ++gy)
    {
        for (int gx = 0; gx < G; ++gx)
        {
            float u = ((gx + 0.5f) / G) * 2.0f - 1.0f;
            float v = ((gy + 0.5f) / G) * 2.0f - 1.0f;
            if (u * u + v * v > 1.0f)
                continue;

            Ray ray;
            ray.origin = Vec3f(u * front_R, v * front_R, start_z);
            ray.dir = Vec3f(0, 0, 1); // on-axis

            // Use green wavelength for spread estimation
            TraceResult res = trace_ghost_ray(ray, lens, bounce_a, bounce_b,
                                              config.wavelengths[1]);
            if (!res.valid)
                continue;

            min_x = std::min(min_x, res.position.x);
            max_x = std::max(max_x, res.position.x);
            min_y = std::min(min_y, res.position.y);
            max_y = std::max(max_y, res.position.y);
            ++valid_count;
        }
    }

    if (valid_count < 2)
        return 1.0f; // too few hits to estimate

    float ghost_w = std::max(max_x - min_x, 0.01f);
    float ghost_h = std::max(max_y - min_y, 0.01f);
    float sensor_w = 2.0f * sensor_half_w;
    float sensor_h = 2.0f * sensor_half_h;

    // Output ghost dimensions in mm for splat radius computation
    if (out_ghost_w_mm)
        *out_ghost_w_mm = ghost_w;
    if (out_ghost_h_mm)
        *out_ghost_h_mm = ghost_h;

    // Correction = how much larger the ghost image is than the sensor.
    // Clamped to [1, max_boost] — never dim a focused ghost, and cap the boost.
    float area_ratio = (ghost_w * ghost_h) / (sensor_w * sensor_h);
    return std::clamp(area_ratio, 1.0f, config.max_area_boost);
}

static std::vector<GridSample> build_grid_samples(int ray_grid,
                                                  int aperture_blades,
                                                  float aperture_rotation_deg,
                                                  PupilJitterMode jitter_mode,
                                                  int jitter_seed)
{
    std::vector<GridSample> grid_samples;
    grid_samples.reserve(ray_grid * ray_grid);

    auto wang_hash = [](std::uint32_t value) {
        value = (value ^ 61u) ^ (value >> 16u);
        value *= 9u;
        value ^= value >> 4u;
        value *= 0x27d4eb2du;
        value ^= value >> 15u;
        return value;
    };
    auto halton2 = [](std::uint32_t value) {
        value = (value << 16u) | (value >> 16u);
        value = ((value & 0x00ff00ffu) << 8u) | ((value & 0xff00ff00u) >> 8u);
        value = ((value & 0x0f0f0f0fu) << 4u) | ((value & 0xf0f0f0f0u) >> 4u);
        value = ((value & 0x33333333u) << 2u) | ((value & 0xccccccccu) >> 2u);
        value = ((value & 0x55555555u) << 1u) | ((value & 0xaaaaaaaau) >> 1u);
        return static_cast<float>(value) * (1.0f / 4294967296.0f);
    };
    auto halton3 = [](std::uint32_t value) {
        float result = 0.0f;
        float factor = 1.0f / 3.0f;
        while (value > 0) {
            result += static_cast<float>(value % 3u) * factor;
            value /= 3u;
            factor /= 3.0f;
        }
        return result;
    };

    const std::uint32_t seed_offset = static_cast<std::uint32_t>(std::max(jitter_seed, 0)) * 1000003u;
    const int candidate_count = ray_grid * ray_grid;
    for (int k = 0; k < candidate_count; ++k) {
        const int gx = k % ray_grid;
        const int gy = k / ray_grid;
        float u = 0.0f;
        float v = 0.0f;
        if (jitter_mode == PupilJitterMode::Halton) {
            u = halton2(static_cast<std::uint32_t>(k)) * 2.0f - 1.0f;
            v = halton3(static_cast<std::uint32_t>(k)) * 2.0f - 1.0f;
        } else {
            const float ju = jitter_mode == PupilJitterMode::Stratified
                ? static_cast<float>(wang_hash(static_cast<std::uint32_t>(k) + seed_offset)) * (1.0f / 4294967296.0f)
                : 0.5f;
            const float jv = jitter_mode == PupilJitterMode::Stratified
                ? static_cast<float>(wang_hash(static_cast<std::uint32_t>(k + candidate_count) + seed_offset)) * (1.0f / 4294967296.0f)
                : 0.5f;
            u = ((gx + ju) / ray_grid) * 2.0f - 1.0f;
            v = ((gy + jv) / ray_grid) * 2.0f - 1.0f;
        }

        if (is_valid_pupil_sample(u, v, aperture_blades, aperture_rotation_deg)) {
            grid_samples.push_back({u, v});
        }
    }

    return grid_samples;
}

static std::vector<GridCell> build_grid_cells(int ray_grid,
                                              int aperture_blades,
                                              float aperture_rotation_deg,
                                              float cell_edge_inset)
{
    std::vector<GridCell> cells;
    cells.reserve(ray_grid * ray_grid);
    const float inset = std::clamp(cell_edge_inset, 0.0f, 0.45f);
    const float trace_scale = 1.0f - inset;

    for (int gy = 0; gy < ray_grid; ++gy) {
        const float v0 = (gy / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
        const float v1 = ((gy + 1) / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
        for (int gx = 0; gx < ray_grid; ++gx) {
            const float u0 = (gx / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
            const float u1 = ((gx + 1) / static_cast<float>(ray_grid)) * 2.0f - 1.0f;
            const float uc = 0.5f * (u0 + u1);
            const float vc = 0.5f * (v0 + v1);
            const float tu0 = uc + (u0 - uc) * trace_scale;
            const float tv0 = vc + (v0 - vc) * trace_scale;
            const float tu1 = uc + (u1 - uc) * trace_scale;
            const float tv1 = vc + (v1 - vc) * trace_scale;

            if (!is_valid_pupil_sample(tu0, tv0, aperture_blades, aperture_rotation_deg) ||
                !is_valid_pupil_sample(tu1, tv0, aperture_blades, aperture_rotation_deg) ||
                !is_valid_pupil_sample(tu1, tv1, aperture_blades, aperture_rotation_deg) ||
                !is_valid_pupil_sample(tu0, tv1, aperture_blades, aperture_rotation_deg) ||
                !is_valid_pupil_sample(uc, vc, aperture_blades, aperture_rotation_deg)) {
                continue;
            }

            cells.push_back({u0, v0, u1, v1, uc, vc});
        }
    }

    return cells;
}

static GhostGridBucket build_grid_bucket(int ray_grid, const GhostConfig& config)
{
    GhostGridBucket bucket;
    bucket.ray_grid = ray_grid;
    bucket.samples = build_grid_samples(ray_grid,
                                        config.aperture_blades,
                                        config.aperture_rotation_deg,
                                        config.pupil_jitter,
                                        config.pupil_jitter_seed);
    bucket.cells = build_grid_cells(ray_grid,
                                    config.aperture_blades,
                                    config.aperture_rotation_deg,
                                    config.cell_edge_inset);
    return bucket;
}

static const GhostGridBucket* find_grid_bucket(const GhostRenderSetup& setup, int ray_grid)
{
    const auto it = std::find_if(setup.grid_buckets.begin(),
                                 setup.grid_buckets.end(),
                                 [&](const GhostGridBucket& bucket) {
                                     return bucket.ray_grid == ray_grid;
                                 });
    return it != setup.grid_buckets.end() ? &(*it) : nullptr;
}

static float estimate_ghost_distortion(const LensSystem& lens,
                                       int bounce_a,
                                       int bounce_b,
                                       float sensor_half_w,
                                       float sensor_half_h,
                                       int width,
                                       int height,
                                       const GhostConfig& config)
{
    constexpr float kProbeRadius = 0.6f;
    const float front_R = lens.surfaces[0].semi_aperture;
    const float start_z = lens.surfaces[0].z - 20.0f;

    struct ProbeSample
    {
        float u = 0.0f;
        float v = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        bool valid = false;
    };

    const std::array<ProbeSample, 9> probe_template {{
        {-kProbeRadius, -kProbeRadius},
        {0.0f,         -kProbeRadius},
        {kProbeRadius, -kProbeRadius},
        {-kProbeRadius, 0.0f},
        {0.0f,          0.0f},
        {kProbeRadius,  0.0f},
        {-kProbeRadius, kProbeRadius},
        {0.0f,          kProbeRadius},
        {kProbeRadius,  kProbeRadius},
    }};

    std::array<ProbeSample, 9> probes = probe_template;
    float min_x = 1.0e30f;
    float max_x = -1.0e30f;
    float min_y = 1.0e30f;
    float max_y = -1.0e30f;
    int valid_count = 0;

    for (ProbeSample& probe : probes) {
        if (probe.u * probe.u + probe.v * probe.v > 1.0f) {
            continue;
        }

        Ray ray;
        ray.origin = Vec3f(probe.u * front_R, probe.v * front_R, start_z);
        ray.dir = Vec3f(0.0f, 0.0f, 1.0f);

        const TraceResult res = trace_ghost_ray(ray, lens, bounce_a, bounce_b, config.wavelengths[1]);
        if (!res.valid) {
            continue;
        }

        probe.x = (res.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
        probe.y = (res.position.y / (2.0f * sensor_half_h) + 0.5f) * height;
        probe.valid = true;

        min_x = std::min(min_x, probe.x);
        max_x = std::max(max_x, probe.x);
        min_y = std::min(min_y, probe.y);
        max_y = std::max(max_y, probe.y);
        ++valid_count;
    }

    if (!probes[4].valid || !probes[1].valid || !probes[3].valid || !probes[5].valid || !probes[7].valid ||
        valid_count < 5) {
        return 0.0f;
    }

    const float extent_px = std::max(std::max(max_x - min_x, max_y - min_y), 1.0f);
    const float center_x = probes[4].x;
    const float center_y = probes[4].y;
    const float inv_probe_span = 0.5f / kProbeRadius;
    const float du_x = (probes[5].x - probes[3].x) * inv_probe_span;
    const float du_y = (probes[5].y - probes[3].y) * inv_probe_span;
    const float dv_x = (probes[7].x - probes[1].x) * inv_probe_span;
    const float dv_y = (probes[7].y - probes[1].y) * inv_probe_span;

    float residual_sq = 0.0f;
    int residual_count = 0;
    for (const ProbeSample& probe : probes) {
        if (!probe.valid) {
            continue;
        }

        const float pred_x = center_x + du_x * probe.u + dv_x * probe.v;
        const float pred_y = center_y + du_y * probe.u + dv_y * probe.v;
        const float err_x = probe.x - pred_x;
        const float err_y = probe.y - pred_y;
        residual_sq += err_x * err_x + err_y * err_y;
        ++residual_count;
    }

    if (residual_count == 0) {
        return 0.0f;
    }

    const float rms_px = std::sqrt(residual_sq / residual_count);
    return std::clamp(rms_px / extent_px, 0.0f, 1.0f);
}

static void select_cell_trace_corner(float exact_u,
                                     float exact_v,
                                     float inset_u,
                                     float inset_v,
                                     int aperture_blades,
                                     float aperture_rotation_deg,
                                     float& out_u,
                                     float& out_v)
{
    if (is_valid_pupil_sample(exact_u, exact_v, aperture_blades, aperture_rotation_deg)) {
        out_u = exact_u;
        out_v = exact_v;
        return;
    }

    out_u = inset_u;
    out_v = inset_v;
}

static void compute_cell_trace_corners(const GridCell& cell,
                                       float cell_edge_inset,
                                       int aperture_blades,
                                       float aperture_rotation_deg,
                                       float& out_u00,
                                       float& out_v00,
                                       float& out_u10,
                                       float& out_v10,
                                       float& out_u11,
                                       float& out_v11,
                                       float& out_u01,
                                       float& out_v01)
{
    const float inset = std::clamp(cell_edge_inset, 0.0f, 0.45f);
    const float trace_scale = 1.0f - inset;
    const float inset_u00 = cell.uc + (cell.u0 - cell.uc) * trace_scale;
    const float inset_v00 = cell.vc + (cell.v0 - cell.vc) * trace_scale;
    const float inset_u10 = cell.uc + (cell.u1 - cell.uc) * trace_scale;
    const float inset_v10 = cell.vc + (cell.v0 - cell.vc) * trace_scale;
    const float inset_u11 = cell.uc + (cell.u1 - cell.uc) * trace_scale;
    const float inset_v11 = cell.vc + (cell.v1 - cell.vc) * trace_scale;
    const float inset_u01 = cell.uc + (cell.u0 - cell.uc) * trace_scale;
    const float inset_v01 = cell.vc + (cell.v1 - cell.vc) * trace_scale;

    select_cell_trace_corner(cell.u0, cell.v0, inset_u00, inset_v00,
                             aperture_blades, aperture_rotation_deg,
                             out_u00, out_v00);
    select_cell_trace_corner(cell.u1, cell.v0, inset_u10, inset_v10,
                             aperture_blades, aperture_rotation_deg,
                             out_u10, out_v10);
    select_cell_trace_corner(cell.u1, cell.v1, inset_u11, inset_v11,
                             aperture_blades, aperture_rotation_deg,
                             out_u11, out_v11);
    select_cell_trace_corner(cell.u0, cell.v1, inset_u01, inset_v01,
                             aperture_blades, aperture_rotation_deg,
                             out_u01, out_v01);
}

static void apply_cell_coverage_bias(PixelPoint& p00,
                                     PixelPoint& p10,
                                     PixelPoint& p11,
                                     PixelPoint& p01,
                                     float cell_coverage_bias)
{
    const float bias = std::clamp(cell_coverage_bias, 0.5f, 2.5f);
    if (std::abs(bias - 1.0f) <= 1.0e-6f) {
        return;
    }

    const PixelPoint center {
        0.25f * (p00.x + p10.x + p11.x + p01.x),
        0.25f * (p00.y + p10.y + p11.y + p01.y),
    };
    auto scale_point = [&](PixelPoint& point) {
        point.x = center.x + (point.x - center.x) * bias;
        point.y = center.y + (point.y - center.y) * bias;
    };
    scale_point(p00);
    scale_point(p10);
    scale_point(p11);
    scale_point(p01);
}

std::vector<GhostPairPlan> plan_active_ghost_pairs(const LensSystem& lens,
                                                   float fov_h,
                                                   float fov_v,
                                                   int width,
                                                   int height,
                                                   const GhostConfig& config)
{
    std::vector<GhostPairPlan> plans;
    if (width <= 0 || height <= 0 || lens.num_surfaces() <= 0) {
        return plans;
    }

    const auto pairs = enumerate_ghost_pairs(lens);
    const float sensor_half_w = lens.focal_length * std::tan(fov_h * 0.5f);
    const float sensor_half_h = lens.focal_length * std::tan(fov_v * 0.5f);

    for (const GhostPair& pair : pairs) {
        const auto ior_before_a = lens.ior_before(pair.surf_a);
        const auto ior_after_a = lens.surfaces[pair.surf_a].ior;
        const auto ior_before_b = lens.ior_before(pair.surf_b);
        const auto ior_after_b = lens.surfaces[pair.surf_b].ior;
        if (std::abs(ior_before_a - ior_after_a) < 0.001f ||
            std::abs(ior_before_b - ior_after_b) < 0.001f) {
            continue;
        }

        const float est = estimate_ghost_intensity(lens, pair.surf_a, pair.surf_b, config);
        if (est < config.min_intensity) {
            continue;
        }

        GhostPairPlan plan;
        plan.pair = pair;

        float ghost_w_mm = 0.0f;
        float ghost_h_mm = 0.0f;
        if (config.ghost_normalize || config.cleanup_mode != GhostCleanupMode::LegacyBlur) {
            plan.area_boost = estimate_ghost_spread(lens,
                                                    pair.surf_a,
                                                    pair.surf_b,
                                                    sensor_half_w,
                                                    sensor_half_h,
                                                    config,
                                                    &ghost_w_mm,
                                                    &ghost_h_mm);
        }

        const float ghost_w_px = ghost_w_mm > 0.0f
            ? (ghost_w_mm / (2.0f * sensor_half_w)) * width
            : 1.0f;
        const float ghost_h_px = ghost_h_mm > 0.0f
            ? (ghost_h_mm / (2.0f * sensor_half_h)) * height
            : 1.0f;
        const float ghost_area_px2 = std::max(ghost_w_px * ghost_h_px, 1.0f);
        plan.estimated_extent_px = std::max(std::max(ghost_w_px, ghost_h_px), 1.0f);
        plan.distortion_score = estimate_ghost_distortion(lens,
                                                          pair.surf_a,
                                                          pair.surf_b,
                                                          sensor_half_w,
                                                          sensor_half_h,
                                                          width,
                                                          height,
                                                          config);
        const bool sharp_cleanup = config.cleanup_mode != GhostCleanupMode::LegacyBlur;
        switch (config.projected_cells_mode) {
            case ProjectedCellsMode::Off:
                plan.use_cell_rasterization = false;
                break;
            case ProjectedCellsMode::Force:
                plan.use_cell_rasterization = sharp_cleanup;
                break;
            case ProjectedCellsMode::Auto:
            default:
                plan.use_cell_rasterization =
                    sharp_cleanup &&
                    select_ghost_cell_rasterization(plan.estimated_extent_px, plan.distortion_score);
                break;
        }
        plan.ray_grid = select_ghost_pair_ray_grid(config.ray_grid,
                                                   plan.estimated_extent_px,
                                                   plan.distortion_score,
                                                   config.adaptive_quality,
                                                   config.adaptive_sampling_strength,
                                                   config.max_adaptive_pair_grid);

        if (config.cleanup_mode != GhostCleanupMode::LegacyBlur) {
            const float approx_valid_samples =
                std::max(0.78539816339f * plan.ray_grid * plan.ray_grid, 1.0f);
            plan.reference_footprint_area_px2 = ghost_area_px2 / approx_valid_samples;
            const float spacing_px = plan.estimated_extent_px / std::sqrt(approx_valid_samples);
            plan.splat_radius_px = std::clamp(spacing_px * 1.2f, 1.25f, 12.0f);
        } else {
            plan.reference_footprint_area_px2 = 1.0f;
        }

        plans.push_back(plan);
    }

    return plans;
}

bool build_ghost_render_setup(const LensSystem& lens,
                              float fov_h,
                              float fov_v,
                              int width,
                              int height,
                              const GhostConfig& config,
                              GhostRenderSetup& out_setup)
{
    out_setup = {};
    if (width <= 0 || height <= 0 || lens.num_surfaces() <= 0 || config.ray_grid <= 0) {
        return false;
    }

    out_setup.active_pair_plans = plan_active_ghost_pairs(lens, fov_h, fov_v, width, height, config);
    out_setup.min_pair_grid = config.ray_grid;
    out_setup.max_pair_grid = config.ray_grid;

    std::vector<int> unique_grids;
    unique_grids.reserve(out_setup.active_pair_plans.size() + 1);
    unique_grids.push_back(config.ray_grid);

    for (const GhostPairPlan& pair_plan : out_setup.active_pair_plans) {
        if (std::find(unique_grids.begin(), unique_grids.end(), pair_plan.ray_grid) == unique_grids.end()) {
            unique_grids.push_back(pair_plan.ray_grid);
        }
        out_setup.min_pair_grid = std::min(out_setup.min_pair_grid, pair_plan.ray_grid);
        out_setup.max_pair_grid = std::max(out_setup.max_pair_grid, pair_plan.ray_grid);
    }

    out_setup.grid_buckets.reserve(unique_grids.size());
    for (int ray_grid : unique_grids) {
        GhostGridBucket bucket = build_grid_bucket(ray_grid, config);
        out_setup.max_valid_grid_count = std::max(out_setup.max_valid_grid_count,
                                                  static_cast<int>(bucket.samples.size()));
        out_setup.grid_buckets.push_back(std::move(bucket));
    }

    const GhostGridBucket* base_bucket = find_grid_bucket(out_setup, config.ray_grid);
    return base_bucket && !base_bucket->samples.empty();
}

// ---------------------------------------------------------------------------
// Render all ghost reflections.
// ---------------------------------------------------------------------------

void render_ghosts(const LensSystem &lens,
                   const std::vector<BrightPixel> &sources,
                   float fov_h, float fov_v,
                   float *out_r, float *out_g, float *out_b,
                   int width, int height,
                   const GhostConfig &config,
                   const GhostRenderSetup* setup,
                   GpuBufferCache* gpu_cache,
                   GhostRenderBackend* out_backend)
{
    if (out_backend) {
        *out_backend = GhostRenderBackend::CPU;
    }

    const auto pairs = enumerate_ghost_pairs(lens);
    printf("Total ghost pairs: %zu\n", pairs.size());

    // Sensor dimensions from focal length and FOV
    float sensor_half_w = lens.focal_length * std::tan(fov_h * 0.5f);
    float sensor_half_h = lens.focal_length * std::tan(fov_v * 0.5f);

    // Entrance pupil sampling setup
    float front_R = lens.surfaces[0].semi_aperture;
    float start_z = lens.surfaces[0].z - 20.0f;
    int N = config.ray_grid;
    size_t num_px = (size_t)width * height;

    GhostRenderSetup local_setup;
    if (!setup) {
        build_ghost_render_setup(lens, fov_h, fov_v, width, height, config, local_setup);
        setup = &local_setup;
    }

    const GhostGridBucket* base_bucket = find_grid_bucket(*setup, config.ray_grid);
    if (!base_bucket || base_bucket->samples.empty()) {
        fprintf(stderr, "WARNING: no valid entrance pupil samples\n");
        return;
    }

    const auto& active_pair_plans = setup->active_pair_plans;
    const int max_valid_grid_count = std::max(setup->max_valid_grid_count,
                                              static_cast<int>(base_bucket->samples.size()));
    const int min_pair_grid = setup->min_pair_grid;
    const int max_pair_grid = setup->max_pair_grid;


    printf("Entrance pupil: %.2f mm radius, base %d×%d grid → %zu rays/source\n",
           front_R, N, N, base_bucket->samples.size());
    printf("Bright sources: %zu\n", sources.size());
    printf("Sensor: %.2f × %.2f mm (at z = %.2f mm)\n",
           sensor_half_w * 2, sensor_half_h * 2, lens.sensor_z);
    printf("Active ghost pairs (above %.1e threshold): %zu / %zu\n",
           config.min_intensity, active_pair_plans.size(), pairs.size());
    printf("Adaptive pair ray grids: %d..%d\n", min_pair_grid, max_pair_grid);
    if (config.ghost_normalize)
    {
        printf("Area normalization: ON (max boost %.0f×)\n", config.max_area_boost);
        for (size_t i = 0; i < active_pair_plans.size(); ++i)
        {
            if (active_pair_plans[i].area_boost > 1.01f)
                printf("  pair (%d,%d): area boost %.1f×\n",
                       active_pair_plans[i].pair.surf_a, active_pair_plans[i].pair.surf_b,
                       active_pair_plans[i].area_boost);
        }
    }

    if (!active_pair_plans.empty() && !sources.empty()) {
        thread_local GpuBufferCache fallback_gpu_cache;
        GpuBufferCache& cuda_cache = gpu_cache ? *gpu_cache : fallback_gpu_cache;
        std::string cuda_error;

        if (launch_ghost_cuda(lens,
                              *setup,
                              sources,
                              sensor_half_w,
                              sensor_half_h,
                              out_r,
                              out_g,
                              out_b,
                              width,
                              height,
                              config,
                              cuda_cache,
                              &cuda_error)) {
            if (out_backend) {
                *out_backend = GhostRenderBackend::CUDA;
            }
            printf("Ghost renderer backend: CUDA\n");
            return;
        }

        if (!cuda_error.empty()) {
            fprintf(stderr, "FlareSim CUDA unavailable, falling back to CPU: %s\n", cuda_error.c_str());
        }
    }

    // ---- CPU path ----
    printf("Ghost renderer backend: CPU fallback\n");
    printf("Splat mode: %s\n",
           config.cleanup_mode == GhostCleanupMode::LegacyBlur
               ? "legacy bilinear"
               : "sharp adaptive");

    auto t0 = std::chrono::steady_clock::now();

    // ====================================================================
    // Parallel over ghost pairs
    //
    // Each thread gets its own full-image accumulation buffer, allocated
    // ONCE before the loop.  Threads process different ghost pairs via
    // dynamic scheduling, iterating over all bright sources within each
    // pair.  This eliminates the old pattern of:
    //   1) per-pair buffer allocation/deallocation (hundreds of MB churn)
    //   2) per-pair thread-barrier overhead
    //   3) per-pair serial reduction
    // and provides much better load balancing across pairs with wildly
    // different hit rates.
    // ====================================================================

    int num_threads = 1;
#ifdef _OPENMP
    num_threads = omp_get_max_threads();
    // Cap threads to the number of work items (ghost pairs) — on large
    // NUMA machines (e.g. 200+ cores), using more threads than pairs
    // just wastes memory on per-thread splat buffers and causes remote
    // NUMA access penalties.  This alone can be a 5-10× speedup.
    if (num_threads > (int)active_pair_plans.size())
        num_threads = std::max((int)active_pair_plans.size(), 1);
    omp_set_num_threads(num_threads);
#endif

    // Pre-allocate per-thread accumulation buffers (ONE allocation total)
    std::vector<std::vector<float>> tbuf_r(num_threads, std::vector<float>(num_px, 0));
    std::vector<std::vector<float>> tbuf_g(num_threads, std::vector<float>(num_px, 0));
    std::vector<std::vector<float>> tbuf_b(num_threads, std::vector<float>(num_px, 0));

    // Per-pair stats (written from threads, read after parallel section)
    std::vector<long long> pair_hits(active_pair_plans.size(), 0);
    std::vector<long long> pair_attempts(active_pair_plans.size(), 0);
    std::vector<double> pair_time(active_pair_plans.size(), 0);

    printf("Rendering %zu ghost pairs × %zu sources across %d thread(s)...\n",
           active_pair_plans.size(), sources.size(), num_threads);
    fflush(stdout);

    // Progress tracking for ETA (atomic so all threads can update safely)
    int total_pairs = (int)active_pair_plans.size();
    std::atomic<int> pairs_done{0};

#pragma omp parallel for schedule(dynamic, 1)
    for (int pi = 0; pi < (int)active_pair_plans.size(); ++pi)
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        const GhostPairPlan& pair_plan = active_pair_plans[pi];
        int a = pair_plan.pair.surf_a;
        int b = pair_plan.pair.surf_b;
        const float area_boost = pair_plan.area_boost;
        const GhostGridBucket* pair_bucket = find_grid_bucket(*setup, pair_plan.ray_grid);
        if (!pair_bucket || pair_bucket->samples.empty()) {
            continue;
        }
        const auto& pair_grid_samples = pair_bucket->samples;
        const auto& pair_grid_cells = !pair_bucket->cells.empty() ? pair_bucket->cells : base_bucket->cells;
        const int valid_grid_count = static_cast<int>(pair_grid_samples.size());
        const float ray_weight = 1.0f / valid_grid_count;
        const float cell_size = 2.0f / pair_plan.ray_grid;

        auto tp0 = std::chrono::steady_clock::now();
        long long hits = 0, attempts = 0;

        // Hit record for collect-then-splat
        struct SplatHit
        {
            float px, py, value, radius;
            int ch;
        };

        // Pre-allocate hit buffer once per thread — max possible hits is
        // grid_samples × 3 channels.  Reused across sources without heap alloc.
        std::vector<SplatHit> hit_buf;
        hit_buf.reserve((size_t)max_valid_grid_count * std::max(config.spectral_samples, 3));

        // Process every bright source for this ghost pair
        for (int si = 0; si < (int)sources.size(); ++si)
        {
            const BrightPixel &src = sources[si];

            // Collimated beam direction for this source
            Vec3f beam_dir = Vec3f(std::tan(src.angle_x),
                                   std::tan(src.angle_y),
                                   1.0f)
                                 .normalized();

            if (pair_plan.use_cell_rasterization && !pair_grid_cells.empty())
            {
                const float cell_weight = 1.0f / static_cast<float>(pair_grid_cells.size());

                for (const GridCell& cell : pair_grid_cells)
                {
                    float cell_u00 = 0.0f;
                    float cell_v00 = 0.0f;
                    float cell_u10 = 0.0f;
                    float cell_v10 = 0.0f;
                    float cell_u11 = 0.0f;
                    float cell_v11 = 0.0f;
                    float cell_u01 = 0.0f;
                    float cell_v01 = 0.0f;
                    compute_cell_trace_corners(cell,
                                               config.cell_edge_inset,
                                               config.aperture_blades,
                                               config.aperture_rotation_deg,
                                               cell_u00,
                                               cell_v00,
                                               cell_u10,
                                               cell_v10,
                                               cell_u11,
                                               cell_v11,
                                               cell_u01,
                                               cell_v01);
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        float p00x = 0.0f, p00y = 0.0f;
                        float p10x = 0.0f, p10y = 0.0f;
                        float p11x = 0.0f, p11y = 0.0f;
                        float p01x = 0.0f, p01y = 0.0f;
                        float w00 = 0.0f;
                        float w10 = 0.0f;
                        float w11 = 0.0f;
                        float w01 = 0.0f;
                        if (!trace_ghost_sensor_position_px(lens, a, b, beam_dir,
                                                            cell_u00, cell_v00, config.wavelengths[ch],
                                                            front_R, start_z, sensor_half_w, sensor_half_h,
                                                            width, height, p00x, p00y, &w00) ||
                            !trace_ghost_sensor_position_px(lens, a, b, beam_dir,
                                                            cell_u10, cell_v10, config.wavelengths[ch],
                                                            front_R, start_z, sensor_half_w, sensor_half_h,
                                                            width, height, p10x, p10y, &w10) ||
                            !trace_ghost_sensor_position_px(lens, a, b, beam_dir,
                                                            cell_u11, cell_v11, config.wavelengths[ch],
                                                            front_R, start_z, sensor_half_w, sensor_half_h,
                                                            width, height, p11x, p11y, &w11) ||
                            !trace_ghost_sensor_position_px(lens, a, b, beam_dir,
                                                            cell_u01, cell_v01, config.wavelengths[ch],
                                                            front_R, start_z, sensor_half_w, sensor_half_h,
                                                            width, height, p01x, p01y, &w01)) {
                            continue;
                        }

                        PixelPoint p00 {p00x, p00y};
                        PixelPoint p10 {p10x, p10y};
                        PixelPoint p11 {p11x, p11y};
                        PixelPoint p01 {p01x, p01y};
                        apply_cell_coverage_bias(p00, p10, p11, p01, config.cell_coverage_bias);
                        const float quad_area =
                            std::abs(signed_triangle_area(p00, p10, p11)) +
                            std::abs(signed_triangle_area(p00, p11, p01));
                        if (!(quad_area > 1.0e-4f) || !std::isfinite(quad_area)) {
                            continue;
                        }

                        const float edge_u = std::sqrt((p10.x - p00.x) * (p10.x - p00.x) +
                                                       (p10.y - p00.y) * (p10.y - p00.y));
                        const float edge_v = std::sqrt((p01.x - p00.x) * (p01.x - p00.x) +
                                                       (p01.y - p00.y) * (p01.y - p00.y));
                        const GhostSampleFootprint footprint {
                            quad_area,
                            edge_u,
                            edge_v,
                            std::max(edge_u, edge_v) / std::max(std::min(edge_u, edge_v), 1.0e-3f),
                            true
                        };
                        const float density_boost = select_ghost_density_boost(pair_plan.area_boost,
                                                                               pair_plan.reference_footprint_area_px2,
                                                                               footprint.area_px2);
                        attempts += 4;
                        hits += 4;
                        const float src_i = (ch == 0) ? src.r : (ch == 1) ? src.g : src.b;
                        const float total_area =
                            std::abs(signed_triangle_area(p00, p10, p11)) +
                            std::abs(signed_triangle_area(p00, p11, p01));
                        const float weighted_integral =
                            std::abs(signed_triangle_area(p00, p10, p11)) * (w00 + w10 + w11) / 3.0f +
                            std::abs(signed_triangle_area(p00, p11, p01)) * (w00 + w11 + w01) / 3.0f;
                        if (!(total_area > 1.0e-4f) || !(weighted_integral > 1.0e-6f)) {
                            continue;
                        }
                        const float avg_weight = weighted_integral / total_area;
                        const float contribution =
                            src_i * avg_weight * cell_weight * config.gain * density_boost;
                        if (contribution < 1.0e-12f) {
                            continue;
                        }

                        auto& buf = (ch == 0) ? tbuf_r[tid] : (ch == 1) ? tbuf_g[tid] : tbuf_b[tid];
                        rasterize_quad_linear(buf.data(),
                                              width,
                                              height,
                                              p00,
                                              p10,
                                              p11,
                                              p01,
                                              w00,
                                              w10,
                                              w11,
                                              w01,
                                              contribution);
                    }
                }

                continue;
            }

            // ---- Pass 1: trace all rays and collect hits ----
            hit_buf.clear(); // reuse capacity, no realloc

            // Track bounding box of hit positions (green channel only
            // to avoid chromatic dispersion inflating the extent)
            float bbox_min_x = 1e30f, bbox_max_x = -1e30f;
            float bbox_min_y = 1e30f, bbox_max_y = -1e30f;
            int green_hits = 0;

            for (int gi = 0; gi < valid_grid_count; ++gi)
            {
                const float u = pair_grid_samples[gi].u;
                const float v = pair_grid_samples[gi].v;

                Ray ray;
                ray.origin = Vec3f(u * front_R, v * front_R, start_z);
                ray.dir = beam_dir;

                GhostSampleFootprint footprint;
                float local_radius_px = pair_plan.splat_radius_px;
                float local_density_boost = pair_plan.area_boost;
                if (config.cleanup_mode != GhostCleanupMode::LegacyBlur) {
                    footprint = estimate_ghost_sample_footprint(lens,
                                                                a,
                                                                b,
                                                                beam_dir,
                                                                u,
                                                                v,
                                                                cell_size,
                                                                front_R,
                                                                start_z,
                                                                sensor_half_w,
                                                                sensor_half_h,
                                                                width,
                                                                height,
                                                                config);
                    if (footprint.valid) {
                        local_density_boost = select_ghost_density_boost(pair_plan.area_boost,
                                                                         pair_plan.reference_footprint_area_px2,
                                                                         footprint.area_px2);
                        local_radius_px = select_ghost_footprint_radius(pair_plan.splat_radius_px,
                                                                        footprint.area_px2,
                                                                        footprint.anisotropy,
                                                                        config.footprint_radius_bias,
                                                                        config.footprint_clamp);
                    }
                }

                // Trace each wavelength independently (chromatic dispersion)
                for (int ch = 0; ch < 3; ++ch)
                {
                    ++attempts;
                    TraceResult res = trace_ghost_ray(ray, lens, a, b,
                                                      config.wavelengths[ch]);
                    if (!res.valid)
                        continue;

                    ++hits;

                    // Map sensor position to pixel coordinates
                    float px = (res.position.x / (2.0f * sensor_half_w) + 0.5f) * width;
                    float py = (res.position.y / (2.0f * sensor_half_h) + 0.5f) * height;

                    // Source intensity for this channel
                    float src_i = (ch == 0) ? src.r : (ch == 1) ? src.g
                                                                : src.b;
                    float contribution = src_i * res.weight * ray_weight * config.gain * local_density_boost;

                    if (contribution < 1e-12f)
                        continue;

                    hit_buf.push_back({px, py, contribution, local_radius_px, ch});

                    // Update bounding box from green channel
                    if (ch == 1)
                    {
                        bbox_min_x = std::min(bbox_min_x, px);
                        bbox_max_x = std::max(bbox_max_x, px);
                        bbox_min_y = std::min(bbox_min_y, py);
                        bbox_max_y = std::max(bbox_max_y, py);
                        ++green_hits;
                    }
                }
            }

            // ---- Compute adaptive splat radius from actual hit density ----
            //
            // sqrt(green_hits) approximates the linear density of hits.
            // Dividing the ghost extent by this gives the effective inter-hit
            // spacing.  A 1.2× tent radius fills inter-ray gaps with some
            // overlap, producing continuous filled shapes rather than scattered
            // points.  Unlike the old per-pair on-axis estimate, this uses
            // real hit data so it adapts correctly to off-axis vignetting.
            float adaptive_r = 1.5f; // minimum: slightly above bilinear
            if (green_hits >= 4)
            {
                float extent_x = bbox_max_x - bbox_min_x;
                float extent_y = bbox_max_y - bbox_min_y;
                float extent = std::max(std::max(extent_x, extent_y), 1.0f);

                float linear_density = std::sqrt((float)green_hits);
                float spacing = extent / linear_density;

                // 1.2× spacing gives good tent overlap; clamp to [1.5, 80]
                adaptive_r = std::clamp(spacing * 1.2f, 1.5f, 80.0f);
            }

            // ---- Pass 2: splat all collected hits ----
            for (auto &h : hit_buf)
            {
               auto &buf = (h.ch == 0)   ? tbuf_r[tid]
                            : (h.ch == 1) ? tbuf_g[tid]
                                          : tbuf_b[tid];
                if (config.cleanup_mode == GhostCleanupMode::LegacyBlur) {
                    splat_bilinear(buf.data(), width, height, h.px, h.py, h.value);
                } else {
                    splat_tent(buf.data(),
                               width,
                               height,
                               h.px,
                               h.py,
                               h.value,
                               std::max(adaptive_r, h.radius));
                }
            }
        }

        auto tp1 = std::chrono::steady_clock::now();
        pair_hits[pi] = hits;
        pair_attempts[pi] = attempts;
        pair_time[pi] = std::chrono::duration<double>(tp1 - tp0).count();

        // Progress + ETA (printed from whichever thread finishes a pair)
        int done = ++pairs_done;
        double elapsed = std::chrono::duration<double>(tp1 - t0).count();
        double per_pair = elapsed / done;
        int remaining = total_pairs - done;
        double eta_sec = per_pair * remaining;
        int eta_m = (int)(eta_sec / 60.0);
        int eta_s = (int)(eta_sec) % 60;
        printf("\r  [%d/%d] pair (%d,%d) done in %.1fs  |  "
               "grid %d  elapsed %.0fs  ETA %dm%02ds   ",
               done, total_pairs, a, b, pair_time[pi],
               pair_plan.ray_grid, elapsed, eta_m, eta_s);
        fflush(stdout);
    }
    printf("\n"); // newline after last \r progress line

    // Print per-pair diagnostics (after parallel section completes)
    for (size_t pi = 0; pi < active_pair_plans.size(); ++pi)
    {
        printf("  [%zu/%zu] Ghost pair (%d, %d) [grid %d, %s, area ×%.1f, ref %.2f px2, extent %.1f px, dist %.3f]  %.1f s  "
               "(%lld/%lld rays, %.1f%%)\n",
               pi + 1, active_pair_plans.size(),
               active_pair_plans[pi].pair.surf_a, active_pair_plans[pi].pair.surf_b,
               active_pair_plans[pi].ray_grid,
               active_pair_plans[pi].use_cell_rasterization ? "cells" : "splats",
               active_pair_plans[pi].area_boost,
               active_pair_plans[pi].reference_footprint_area_px2,
               active_pair_plans[pi].estimated_extent_px,
               active_pair_plans[pi].distortion_score,
               pair_time[pi],
               pair_hits[pi], pair_attempts[pi],
               pair_attempts[pi] > 0
                   ? 100.0 * pair_hits[pi] / pair_attempts[pi]
                   : 0.0);
    }

    // Reduce per-thread buffers into output (parallelised over pixels)
#pragma omp parallel for
    for (int i = 0; i < (int)num_px; ++i)
    {
        for (int t = 0; t < num_threads; ++t)
        {
            out_r[i] += tbuf_r[t][i];
            out_g[i] += tbuf_g[t][i];
            out_b[i] += tbuf_b[t][i];
        }
    }

    auto t1 = std::chrono::steady_clock::now();

    long long total_hits = 0, total_attempts = 0;
    for (size_t pi = 0; pi < active_pair_plans.size(); ++pi)
    {
        total_hits += pair_hits[pi];
        total_attempts += pair_attempts[pi];
    }
    printf("Ghost rendering total: %.1f s  (%lld/%lld rays, %.1f%%)\n",
           std::chrono::duration<double>(t1 - t0).count(),
           total_hits, total_attempts,
           total_attempts > 0 ? 100.0 * total_hits / total_attempts : 0.0);
}
