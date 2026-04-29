#include "render_frame.h"

#include "post_process.h"
#include "source_extract.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_append_bytes(std::uint64_t& hash, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
}

template <typename T>
void hash_append_value(std::uint64_t& hash, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    hash_append_bytes(hash, &value, sizeof(T));
}

void hash_append_string(std::uint64_t& hash, const std::string& value)
{
    const std::size_t size = value.size();
    hash_append_value(hash, size);
    if (!value.empty()) {
        hash_append_bytes(hash, value.data(), value.size());
    }
}

bool needs_source_stage(const FrameRenderPlan& plan)
{
    return plan.need_source_output || plan.need_flare || plan.need_haze || plan.need_starburst;
}

bool needs_scene_stage(const FrameRenderPlan& plan)
{
    return plan.need_scene_output || plan.need_bloom || needs_source_stage(plan);
}

void clear_outputs(FrameRenderOutputs& outputs)
{
    outputs.stats = {};
    outputs.scene_r.clear();
    outputs.scene_g.clear();
    outputs.scene_b.clear();
    outputs.flare_r.clear();
    outputs.flare_g.clear();
    outputs.flare_b.clear();
    outputs.bloom_r.clear();
    outputs.bloom_g.clear();
    outputs.bloom_b.clear();
    outputs.haze_r.clear();
    outputs.haze_g.clear();
    outputs.haze_b.clear();
    outputs.starburst_r.clear();
    outputs.starburst_g.clear();
    outputs.starburst_b.clear();
    outputs.detected_sources.clear();
    outputs.sources.clear();
}

RgbImageView make_scene_view(const FrameRenderOutputs& outputs, const RgbImageView& input)
{
    if (outputs.scene_r.empty() || outputs.scene_g.empty() || outputs.scene_b.empty()) {
        return input;
    }

    return {
        outputs.scene_r.data(),
        outputs.scene_g.data(),
        outputs.scene_b.data(),
        outputs.width,
        outputs.height,
    };
}

std::uint64_t hash_input_image(const RgbImageView& input)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, input.width);
    hash_append_value(hash, input.height);

    const std::size_t np = static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height);
    hash_append_bytes(hash, input.r, np * sizeof(float));
    hash_append_bytes(hash, input.g, np * sizeof(float));
    hash_append_bytes(hash, input.b, np * sizeof(float));
    return hash;
}

std::uint64_t hash_mask_image(const MonoImageView* mask)
{
    if (!mask || !mask->value || mask->width <= 0 || mask->height <= 0) {
        return 0;
    }

    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, mask->width);
    hash_append_value(hash, mask->height);

    const std::size_t np = static_cast<std::size_t>(mask->width) * static_cast<std::size_t>(mask->height);
    hash_append_bytes(hash, mask->value, np * sizeof(float));
    return hash;
}

std::uint64_t hash_lens(const LensSystem& lens)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_string(hash, lens.name);
    hash_append_value(hash, lens.focal_length);
    hash_append_value(hash, lens.sensor_z);

    const std::size_t count = lens.surfaces.size();
    hash_append_value(hash, count);
    for (const Surface& surface : lens.surfaces) {
        hash_append_value(hash, surface.radius);
        hash_append_value(hash, surface.thickness);
        hash_append_value(hash, surface.ior);
        hash_append_value(hash, surface.abbe_v);
        hash_append_value(hash, surface.semi_aperture);
        hash_append_value(hash, surface.coating);
        hash_append_value(hash, surface.is_stop);
        hash_append_value(hash, surface.z);
    }

    return hash;
}

std::uint64_t make_scene_key(const RgbImageView& input, const FrameRenderSettings& settings)
{
    std::uint64_t hash = hash_input_image(input);
    hash_append_value(hash, settings.threshold);
    hash_append_value(hash, settings.sky_brightness);
    return hash;
}

std::uint64_t make_source_key(std::uint64_t scene_key, const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, scene_key);
    hash_append_value(hash, settings.use_sensor_size);
    hash_append_value(hash, settings.fov_h_deg);
    hash_append_value(hash, settings.fov_v_deg);
    hash_append_value(hash, settings.auto_fov_v);
    hash_append_value(hash, settings.sensor_width_mm);
    hash_append_value(hash, settings.sensor_height_mm);
    hash_append_value(hash, settings.focal_length_mm);
    hash_append_value(hash, settings.downsample);
    hash_append_value(hash, settings.source_cap);
    hash_append_value(hash, settings.max_sources);
    hash_append_value(hash, settings.cluster_radius_px);
    return hash;
}

std::uint64_t make_source_key(std::uint64_t scene_key,
                              const FrameRenderSettings& settings,
                              const MonoImageView* detection_mask)
{
    std::uint64_t hash = make_source_key(scene_key, settings);
    const std::uint64_t mask_hash = hash_mask_image(detection_mask);
    hash_append_value(hash, mask_hash);
    return hash;
}

std::uint64_t make_ghost_key(std::uint64_t source_key,
                             std::uint64_t lens_key,
                             const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, source_key);
    hash_append_value(hash, lens_key);
    hash_append_value(hash, settings.ray_grid);
    hash_append_value(hash, settings.min_ghost);
    hash_append_value(hash, settings.flare_gain);
    hash_append_value(hash, settings.spectral_samples);
    hash_append_value(hash, settings.aperture_blades);
    hash_append_value(hash, settings.aperture_rotation_deg);
    hash_append_value(hash, settings.ghost_normalize);
    hash_append_value(hash, settings.max_area_boost);
    hash_append_value(hash, settings.ghost_cleanup_mode);
    hash_append_value(hash, settings.adaptive_quality);
    hash_append_value(hash, settings.adaptive_sampling_strength);
    hash_append_value(hash, settings.footprint_radius_bias);
    hash_append_value(hash, settings.footprint_clamp);
    hash_append_value(hash, settings.max_adaptive_pair_grid);
    hash_append_value(hash, settings.pair_start);
    hash_append_value(hash, settings.pair_count);
    hash_append_value(hash, settings.projected_cells_mode);
    hash_append_value(hash, settings.pupil_jitter_mode);
    hash_append_value(hash, settings.pupil_jitter_seed);
    hash_append_value(hash, settings.spectral_jitter_mode);
    hash_append_value(hash, settings.spectral_jitter_seed);
    hash_append_value(hash, settings.cell_coverage_bias);
    hash_append_value(hash, settings.cell_edge_inset);
    return hash;
}

std::uint64_t make_ghost_setup_key(std::uint64_t lens_key,
                                   float fov_h,
                                   float fov_v,
                                   int width,
                                   int height,
                                   const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, lens_key);
    hash_append_value(hash, fov_h);
    hash_append_value(hash, fov_v);
    hash_append_value(hash, width);
    hash_append_value(hash, height);
    hash_append_value(hash, settings.ray_grid);
    hash_append_value(hash, settings.min_ghost);
    hash_append_value(hash, settings.aperture_blades);
    hash_append_value(hash, settings.aperture_rotation_deg);
    hash_append_value(hash, settings.ghost_normalize);
    hash_append_value(hash, settings.max_area_boost);
    hash_append_value(hash, settings.ghost_cleanup_mode);
    hash_append_value(hash, settings.adaptive_quality);
    hash_append_value(hash, settings.adaptive_sampling_strength);
    hash_append_value(hash, settings.max_adaptive_pair_grid);
    hash_append_value(hash, settings.pair_start);
    hash_append_value(hash, settings.pair_count);
    hash_append_value(hash, settings.projected_cells_mode);
    hash_append_value(hash, settings.pupil_jitter_mode);
    hash_append_value(hash, settings.pupil_jitter_seed);
    hash_append_value(hash, settings.cell_edge_inset);
    return hash;
}

std::uint64_t make_bloom_key(std::uint64_t scene_key, const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, scene_key);
    hash_append_value(hash, settings.bloom.threshold);
    hash_append_value(hash, settings.bloom.strength);
    hash_append_value(hash, settings.bloom.radius);
    hash_append_value(hash, settings.bloom.passes);
    hash_append_value(hash, settings.bloom.octaves);
    hash_append_value(hash, settings.bloom.chromatic);
    return hash;
}

std::uint64_t make_haze_key(std::uint64_t source_key, const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, source_key);
    hash_append_value(hash, settings.haze_gain);
    hash_append_value(hash, settings.haze_radius);
    hash_append_value(hash, settings.haze_blur_passes);
    return hash;
}

std::uint64_t make_starburst_key(std::uint64_t source_key, const FrameRenderSettings& settings)
{
    std::uint64_t hash = kFnvOffset;
    hash_append_value(hash, source_key);
    hash_append_value(hash, settings.starburst_gain);
    hash_append_value(hash, settings.starburst_scale);
    hash_append_value(hash, settings.aperture_blades);
    hash_append_value(hash, settings.aperture_rotation_deg);
    return hash;
}

RgbImageView prepare_scene_input(const RgbImageView& input,
                                 const FrameRenderSettings& settings,
                                 FrameRenderOutputs& outputs)
{
    if (std::abs(settings.sky_brightness - 1.0f) <= 1.0e-6f) {
        outputs.scene_r.clear();
        outputs.scene_g.clear();
        outputs.scene_b.clear();
        return input;
    }

    const size_t np = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);
    outputs.scene_r.assign(input.r, input.r + np);
    outputs.scene_g.assign(input.g, input.g + np);
    outputs.scene_b.assign(input.b, input.b + np);

    for (size_t i = 0; i < np; ++i) {
        const float lum =
            0.2126f * outputs.scene_r[i] +
            0.7152f * outputs.scene_g[i] +
            0.0722f * outputs.scene_b[i];
        if (lum <= settings.threshold) {
            outputs.scene_r[i] *= settings.sky_brightness;
            outputs.scene_g[i] *= settings.sky_brightness;
            outputs.scene_b[i] *= settings.sky_brightness;
        }
    }

    return {
        outputs.scene_r.data(),
        outputs.scene_g.data(),
        outputs.scene_b.data(),
        input.width,
        input.height,
    };
}

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

void FrameRenderCache::clear()
{
    scene_key = 0;
    has_scene = false;
    scene_r.clear();
    scene_g.clear();
    scene_b.clear();

    source_key = 0;
    has_sources = false;
    detected_sources.clear();
    sources.clear();

    ghost_setup_key = 0;
    has_ghost_setup = false;
    ghost_setup = {};

    ghost_key = 0;
    has_ghosts = false;
    ghost_backend = GhostRenderBackend::CPU;
    ghost_gpu_cache.release();
    flare_r.clear();
    flare_g.clear();
    flare_b.clear();

    bloom_key = 0;
    has_bloom = false;
    bloom_r.clear();
    bloom_g.clear();
    bloom_b.clear();

    haze_key = 0;
    has_haze = false;
    haze_r.clear();
    haze_g.clear();
    haze_b.clear();

    starburst_key = 0;
    has_starburst = false;
    starburst_r.clear();
    starburst_g.clear();
    starburst_b.clear();
}

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
    FrameRenderOutputs& outputs,
    const MonoImageView* detection_mask)
{
    const FrameRenderPlan full_plan {};
    return render_frame(lens, input, settings, outputs, full_plan, nullptr, detection_mask);
}

bool render_frame(
    const LensSystem& lens,
    const RgbImageView& input,
    const FrameRenderSettings& settings,
    FrameRenderOutputs& outputs,
    const FrameRenderPlan& plan,
    FrameRenderCache* cache,
    const MonoImageView* detection_mask)
{
    if (!input.r || !input.g || !input.b || input.width <= 0 || input.height <= 0) {
        return false;
    }
    if (lens.num_surfaces() <= 0 || lens.focal_length <= 0.0f) {
        return false;
    }

    outputs.width = input.width;
    outputs.height = input.height;
    outputs.ghost_backend = GhostRenderBackend::CPU;
    clear_outputs(outputs);

    const std::size_t np = static_cast<std::size_t>(input.width) * static_cast<std::size_t>(input.height);
    const bool run_scene_stage = needs_scene_stage(plan);
    const bool run_source_stage = needs_source_stage(plan);

    float fov_h = 0.0f;
    float fov_v = 0.0f;
    if (run_source_stage && !compute_camera_fov(settings, input.width, input.height, fov_h, fov_v)) {
        return false;
    }

    std::uint64_t scene_key = 0;
    if (run_scene_stage) {
        scene_key = make_scene_key(input, settings);
        const bool scene_cache_hit = cache && cache->has_scene && cache->scene_key == scene_key;
        if (scene_cache_hit) {
            outputs.scene_r = cache->scene_r;
            outputs.scene_g = cache->scene_g;
            outputs.scene_b = cache->scene_b;
        } else {
            prepare_scene_input(input, settings, outputs);
            outputs.stats.recomputed_scene = true;

            if (cache) {
                cache->scene_key = scene_key;
                cache->has_scene = true;
                cache->scene_r = outputs.scene_r;
                cache->scene_g = outputs.scene_g;
                cache->scene_b = outputs.scene_b;
            }
        }
    }

    const RgbImageView scene_input = make_scene_view(outputs, input);

    std::uint64_t source_key = 0;
    if (run_source_stage) {
        const bool use_detection_mask = detection_mask &&
                                        detection_mask->value &&
                                        detection_mask->width == input.width &&
                                        detection_mask->height == input.height;
        const MonoImageView* active_detection_mask = use_detection_mask ? detection_mask : nullptr;

        source_key = make_source_key(scene_key, settings, active_detection_mask);
        const bool source_cache_hit = cache && cache->has_sources && cache->source_key == source_key;
        if (source_cache_hit) {
            outputs.detected_sources = cache->detected_sources;
            outputs.sources = cache->sources;
        } else {
            outputs.detected_sources = extract_bright_pixels(
                scene_input,
                settings.threshold,
                settings.downsample,
                fov_h,
                fov_v,
                settings.source_cap,
                active_detection_mask);

            outputs.sources = outputs.detected_sources;
            cluster_bright_pixels(outputs.sources,
                                  settings.cluster_radius_px,
                                  input.width,
                                  std::tan(fov_h * 0.5f));
            limit_bright_pixels(outputs.sources, static_cast<std::size_t>(settings.max_sources));
            outputs.stats.recomputed_sources = true;

            if (cache) {
                cache->source_key = source_key;
                cache->has_sources = true;
                cache->detected_sources = outputs.detected_sources;
                cache->sources = outputs.sources;
            }
        }
    }

    const std::uint64_t lens_key = hash_lens(lens);

    if (plan.need_flare) {
        const std::uint64_t ghost_key = make_ghost_key(source_key, lens_key, settings);
        const bool ghost_cache_hit = cache && cache->has_ghosts && cache->ghost_key == ghost_key;

        if (ghost_cache_hit) {
            outputs.flare_r = cache->flare_r;
            outputs.flare_g = cache->flare_g;
            outputs.flare_b = cache->flare_b;
            outputs.ghost_backend = cache->ghost_backend;
        } else {
            outputs.flare_r.assign(np, 0.0f);
            outputs.flare_g.assign(np, 0.0f);
            outputs.flare_b.assign(np, 0.0f);

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
                ghost.cleanup_mode = settings.ghost_cleanup_mode;
                ghost.adaptive_quality = settings.adaptive_quality;
                ghost.adaptive_sampling_strength = settings.adaptive_sampling_strength;
                ghost.footprint_radius_bias = settings.footprint_radius_bias;
                ghost.footprint_clamp = settings.footprint_clamp;
                ghost.max_adaptive_pair_grid = settings.max_adaptive_pair_grid;
                ghost.pair_start_index = settings.pair_start;
                ghost.pair_count = settings.pair_count;
                ghost.projected_cells_mode = settings.projected_cells_mode;
                ghost.pupil_jitter = settings.pupil_jitter_mode;
                ghost.pupil_jitter_seed = settings.pupil_jitter_seed;
                ghost.spectral_jitter = settings.spectral_jitter_mode;
                ghost.spectral_jitter_seed = settings.spectral_jitter_seed;
                ghost.cell_coverage_bias = settings.cell_coverage_bias;
                ghost.cell_edge_inset = settings.cell_edge_inset;

                const std::uint64_t ghost_setup_key = make_ghost_setup_key(
                    lens_key,
                    fov_h,
                    fov_v,
                    input.width,
                    input.height,
                    settings);

                GhostRenderSetup local_ghost_setup;
                const bool ghost_setup_cache_hit =
                    cache && cache->has_ghost_setup && cache->ghost_setup_key == ghost_setup_key;
                const GhostRenderSetup* ghost_setup = nullptr;
                if (ghost_setup_cache_hit) {
                    ghost_setup = &cache->ghost_setup;
                } else {
                    build_ghost_render_setup(lens,
                                             fov_h,
                                             fov_v,
                                             input.width,
                                             input.height,
                                             ghost,
                                             local_ghost_setup);
                    outputs.stats.recomputed_ghost_setup = true;

                    if (cache) {
                        cache->ghost_setup_key = ghost_setup_key;
                        cache->has_ghost_setup = true;
                        cache->ghost_setup = std::move(local_ghost_setup);
                        ghost_setup = &cache->ghost_setup;
                    } else {
                        ghost_setup = &local_ghost_setup;
                    }
                }

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
                    ghost_setup,
                    cache ? &cache->ghost_gpu_cache : nullptr,
                    &outputs.ghost_backend);
            }

            outputs.stats.recomputed_ghosts = true;

            if (cache) {
                cache->ghost_key = ghost_key;
                cache->has_ghosts = true;
                cache->ghost_backend = outputs.ghost_backend;
                cache->flare_r = outputs.flare_r;
                cache->flare_g = outputs.flare_g;
                cache->flare_b = outputs.flare_b;
            }
        }

        const bool apply_ghost_blur =
            settings.ghost_blur > 0.0f &&
            settings.ghost_blur_passes > 0 &&
            (settings.ghost_cleanup_mode == GhostCleanupMode::LegacyBlur ||
             settings.ghost_cleanup_mode == GhostCleanupMode::SharpAdaptivePlusBlur);

        if (apply_ghost_blur) {
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
    }

    if (plan.need_haze) {
        const std::uint64_t haze_key = make_haze_key(source_key, settings);
        const bool haze_cache_hit = cache && cache->has_haze && cache->haze_key == haze_key;

        if (haze_cache_hit) {
            outputs.haze_r = cache->haze_r;
            outputs.haze_g = cache->haze_g;
            outputs.haze_b = cache->haze_b;
        } else {
            outputs.haze_r.assign(np, 0.0f);
            outputs.haze_g.assign(np, 0.0f);
            outputs.haze_b.assign(np, 0.0f);

            if (!outputs.detected_sources.empty() && settings.haze_gain > 0.0f) {
                std::vector<BrightPixel> haze_sources = outputs.detected_sources;
                limit_bright_pixels(haze_sources, static_cast<std::size_t>(settings.max_sources));
                rasterize_sources(haze_sources,
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

                for (std::size_t i = 0; i < np; ++i) {
                    outputs.haze_r[i] *= settings.haze_gain;
                    outputs.haze_g[i] *= settings.haze_gain;
                    outputs.haze_b[i] *= settings.haze_gain;
                }
            }

            outputs.stats.recomputed_haze = true;

            if (cache) {
                cache->haze_key = haze_key;
                cache->has_haze = true;
                cache->haze_r = outputs.haze_r;
                cache->haze_g = outputs.haze_g;
                cache->haze_b = outputs.haze_b;
            }
        }
    }

    if (plan.need_starburst) {
        const std::uint64_t starburst_key = make_starburst_key(source_key, settings);
        const bool starburst_cache_hit = cache && cache->has_starburst && cache->starburst_key == starburst_key;

        if (starburst_cache_hit) {
            outputs.starburst_r = cache->starburst_r;
            outputs.starburst_g = cache->starburst_g;
            outputs.starburst_b = cache->starburst_b;
        } else {
            outputs.starburst_r.assign(np, 0.0f);
            outputs.starburst_g.assign(np, 0.0f);
            outputs.starburst_b.assign(np, 0.0f);

            if (!outputs.sources.empty() && settings.starburst_gain > 0.0f) {
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

            outputs.stats.recomputed_starburst = true;

            if (cache) {
                cache->starburst_key = starburst_key;
                cache->has_starburst = true;
                cache->starburst_r = outputs.starburst_r;
                cache->starburst_g = outputs.starburst_g;
                cache->starburst_b = outputs.starburst_b;
            }
        }
    }

    if (plan.need_bloom) {
        const std::uint64_t bloom_key = make_bloom_key(scene_key, settings);
        const bool bloom_cache_hit = cache && cache->has_bloom && cache->bloom_key == bloom_key;

        if (bloom_cache_hit) {
            outputs.bloom_r = cache->bloom_r;
            outputs.bloom_g = cache->bloom_g;
            outputs.bloom_b = cache->bloom_b;
        } else {
            outputs.bloom_r.assign(np, 0.0f);
            outputs.bloom_g.assign(np, 0.0f);
            outputs.bloom_b.assign(np, 0.0f);

            if (settings.bloom.strength > 0.0f) {
                const MutableRgbImageView bloom_out {
                    outputs.bloom_r.data(),
                    outputs.bloom_g.data(),
                    outputs.bloom_b.data(),
                    input.width,
                    input.height,
                };
                generate_bloom(scene_input, bloom_out, settings.bloom);
            }

            outputs.stats.recomputed_bloom = true;

            if (cache) {
                cache->bloom_key = bloom_key;
                cache->has_bloom = true;
                cache->bloom_r = outputs.bloom_r;
                cache->bloom_g = outputs.bloom_g;
                cache->bloom_b = outputs.bloom_b;
            }
        }
    }

    return true;
}
