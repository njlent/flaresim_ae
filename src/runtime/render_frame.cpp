#include "render_frame.h"

#include "post_process.h"
#include "source_extract.h"

#include <algorithm>
#include <cmath>

namespace {

void rasterize_sources(const std::vector<BrightPixel>& sources,
                       const FrameRenderSettings& settings,
                       int width,
                       int height,
                       float fov_h,
                       float fov_v,
                       float* out_r,
                       float* out_g,
                       float* out_b)
{
    if (!out_r || !out_g || !out_b || width <= 0 || height <= 0) {
        return;
    }

    const float tan_half_h = std::tan(fov_h * 0.5f);
    const float tan_half_v = std::tan(fov_v * 0.5f);
    const int block = std::max(1, settings.downsample);

    for (const BrightPixel& source : sources) {
        const float ndc_x = std::tan(source.angle_x) / (2.0f * tan_half_h);
        const float ndc_y = std::tan(source.angle_y) / (2.0f * tan_half_v);
        const int px = static_cast<int>((ndc_x + 0.5f) * width);
        const int py = static_cast<int>((ndc_y + 0.5f) * height);

        const int x0 = std::max(0, px - block / 2);
        const int y0 = std::max(0, py - block / 2);
        const int x1 = std::min(width, x0 + block);
        const int y1 = std::min(height, y0 + block);

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                out_r[i] += source.r;
                out_g[i] += source.g;
                out_b[i] += source.b;
            }
        }
    }
}

} // namespace

bool compute_camera_fov(const FrameRenderSettings& settings,
                        int width,
                        int height,
                        float& out_fov_h_rad,
                        float& out_fov_v_rad)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (settings.use_sensor_size) {
        const float focal_length_mm = std::max(settings.focal_length_mm, 1.0e-4f);
        const float sensor_width_mm = std::max(settings.sensor_width_mm, 1.0e-4f);
        const float sensor_height_mm = std::max(settings.sensor_height_mm, 1.0e-4f);

        out_fov_h_rad = 2.0f * std::atan(sensor_width_mm / (2.0f * focal_length_mm));
        out_fov_v_rad = 2.0f * std::atan(sensor_height_mm / (2.0f * focal_length_mm));
        return true;
    }

    out_fov_h_rad = settings.fov_h_deg * 3.14159265358979323846f / 180.0f;
    if (settings.auto_fov_v) {
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        out_fov_v_rad = 2.0f * std::atan(std::tan(out_fov_h_rad * 0.5f) / aspect);
    } else {
        out_fov_v_rad = settings.fov_v_deg * 3.14159265358979323846f / 180.0f;
    }

    return true;
}

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs)
{
    if (!input.r || !input.g || !input.b || input.width <= 0 || input.height <= 0) {
        return false;
    }
    if (lens.num_surfaces() <= 0 || lens.focal_length <= 0.0f) {
        return false;
    }

    outputs.width = input.width;
    outputs.height = input.height;

    const size_t np = (size_t)input.width * input.height;
    outputs.flare_r.assign(np, 0.0f);
    outputs.flare_g.assign(np, 0.0f);
    outputs.flare_b.assign(np, 0.0f);
    outputs.bloom_r.assign(np, 0.0f);
    outputs.bloom_g.assign(np, 0.0f);
    outputs.bloom_b.assign(np, 0.0f);
    outputs.haze_r.assign(np, 0.0f);
    outputs.haze_g.assign(np, 0.0f);
    outputs.haze_b.assign(np, 0.0f);
    outputs.starburst_r.assign(np, 0.0f);
    outputs.starburst_g.assign(np, 0.0f);
    outputs.starburst_b.assign(np, 0.0f);
    outputs.ghost_backend = GhostRenderBackend::CPU;

    float fov_h = 0.0f;
    float fov_v = 0.0f;
    if (!compute_camera_fov(settings, input.width, input.height, fov_h, fov_v)) {
        return false;
    }

    outputs.detected_sources = extract_bright_pixels(
        input,
        settings.threshold,
        settings.downsample,
        fov_h,
        fov_v);

    outputs.sources = outputs.detected_sources;
    limit_bright_pixels(outputs.sources, static_cast<size_t>(settings.max_sources));

    float sensor_half_w = lens.focal_length * std::tan(fov_h * 0.5f);
    float sensor_half_h = lens.focal_length * std::tan(fov_v * 0.5f);
    if (settings.use_sensor_size) {
        sensor_half_w = settings.sensor_width_mm * 0.5f;
        sensor_half_h = settings.sensor_height_mm * 0.5f;
    }

    if (!outputs.sources.empty()) {
        GhostConfig ghost {};
        ghost.ray_grid = settings.ray_grid;
        ghost.min_intensity = settings.min_ghost;
        ghost.gain = settings.flare_gain;
        ghost.spectral_samples = settings.spectral_samples;
        ghost.aperture_blades = settings.aperture_blades;
        ghost.aperture_rotation_deg = settings.aperture_rotation_deg;
        ghost.ghost_normalize = settings.ghost_normalize;
        ghost.max_area_boost = settings.max_area_boost;

        render_ghosts(
            lens,
            outputs.sources,
            fov_h,
            fov_v,
            outputs.flare_r.data(),
            outputs.flare_g.data(),
            outputs.flare_b.data(),
            input.width,
            input.height,
            ghost,
            &outputs.ghost_backend);

        if (settings.ghost_blur > 0.0f && settings.ghost_blur_passes > 0) {
            const float diag = std::sqrt(static_cast<float>(input.width * input.width + input.height * input.height));
            const int radius = std::max(1, static_cast<int>(std::round(settings.ghost_blur * diag)));
            box_blur_rgb(outputs.flare_r.data(),
                         outputs.flare_g.data(),
                         outputs.flare_b.data(),
                         input.width,
                         input.height,
                         radius,
                         settings.ghost_blur_passes);
        }

        if (settings.haze_gain > 0.0f) {
            rasterize_sources(outputs.sources,
                              settings,
                              input.width,
                              input.height,
                              fov_h,
                              fov_v,
                              outputs.haze_r.data(),
                              outputs.haze_g.data(),
                              outputs.haze_b.data());

            if (settings.haze_radius > 0.0f && settings.haze_blur_passes > 0) {
                const float diag = std::sqrt(static_cast<float>(input.width * input.width + input.height * input.height));
                const int radius = std::max(1, static_cast<int>(std::round(settings.haze_radius * diag)));
                box_blur_rgb(outputs.haze_r.data(),
                             outputs.haze_g.data(),
                             outputs.haze_b.data(),
                             input.width,
                             input.height,
                             radius,
                             settings.haze_blur_passes);
            }

            for (size_t i = 0; i < np; ++i) {
                outputs.haze_r[i] *= settings.haze_gain;
                outputs.haze_g[i] *= settings.haze_gain;
                outputs.haze_b[i] *= settings.haze_gain;
            }
        }

        if (settings.starburst_gain > 0.0f) {
            thread_local StarburstPSF psf;
            thread_local int last_blades = -9999;
            thread_local float last_rotation = 1.0e9f;

            if (psf.empty() ||
                last_blades != settings.aperture_blades ||
                std::abs(last_rotation - settings.aperture_rotation_deg) > 1.0e-6f) {
                StarburstConfig sb_probe {};
                sb_probe.aperture_blades = settings.aperture_blades;
                sb_probe.aperture_rotation_deg = settings.aperture_rotation_deg;
                compute_starburst_psf(sb_probe, psf);
                last_blades = settings.aperture_blades;
                last_rotation = settings.aperture_rotation_deg;
            }

            StarburstConfig sb {};
            sb.gain = settings.starburst_gain;
            sb.scale = settings.starburst_scale;
            sb.aperture_blades = settings.aperture_blades;
            sb.aperture_rotation_deg = settings.aperture_rotation_deg;

            render_starburst(psf,
                             sb,
                             outputs.sources,
                             std::tan(fov_h * 0.5f),
                             std::tan(fov_v * 0.5f),
                             outputs.starburst_r.data(),
                             outputs.starburst_g.data(),
                             outputs.starburst_b.data(),
                             input.width,
                             input.height,
                             input.width,
                             input.height,
                             0,
                             0);
        }
    }

    if (settings.bloom.strength > 0.0f) {
        const MutableRgbImageView bloom_out {
            outputs.bloom_r.data(),
            outputs.bloom_g.data(),
            outputs.bloom_b.data(),
            input.width,
            input.height,
        };
        generate_bloom(input, bloom_out, settings.bloom);
    }

    return true;
}
