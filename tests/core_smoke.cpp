#include "asset_root.h"
#include "bloom.h"
#include "builtin_lenses.h"
#include "frame_bridge.h"
#include "ghost_cuda.h"
#include "ghost.h"
#include "lens.h"
#include "lens_resolution.h"
#include "output_view.h"
#include "param_schema.h"
#include "parameter_state.h"
#include "pixel_convert.h"
#include "render_frame.h"
#include "source_extract.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string repo_path(const char* rel)
{
    return std::string(FLARESIM_REPO_ROOT) + "/" + rel;
}

void test_lens_load()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    const bool ok = lens.load(path.c_str());
    assert(ok);
    assert(lens.num_surfaces() > 0);
    const auto pairs = enumerate_ghost_pairs(lens);
    assert((int)pairs.size() == (lens.num_surfaces() * (lens.num_surfaces() - 1)) / 2);
}

void test_source_extract()
{
    std::vector<float> r(16, 0.0f);
    std::vector<float> g(16, 0.0f);
    std::vector<float> b(16, 0.0f);
    r[5] = 4.0f;
    g[5] = 2.0f;
    b[5] = 1.0f;

    const RgbImageView img {r.data(), g.data(), b.data(), 4, 4};
    const auto sources = extract_bright_pixels(img, 1.0f, 1, 1.0f, 1.0f);
    assert(sources.size() == 1);
    assert(sources[0].r > 0.0f);
    assert(std::abs(sources[0].angle_x) < 1.0f);

    std::vector<float> near_r(16, 0.0f);
    std::vector<float> near_g(16, 0.0f);
    std::vector<float> near_b(16, 0.0f);
    near_r[5] = 0.98f;
    near_g[5] = 0.98f;
    near_b[5] = 0.98f;

    const RgbImageView near_img {near_r.data(), near_g.data(), near_b.data(), 4, 4};
    const auto near_sources = extract_bright_pixels(near_img, 0.9f, 2, 1.0f, 1.0f);
    assert(near_sources.size() == 1);
    assert(std::abs(near_sources[0].r - 0.98f) < 1.0e-6f);
}

void test_source_limit()
{
    std::vector<BrightPixel> sources = {
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 1.0f, .g = 1.0f, .b = 1.0f},
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 2.0f, .g = 2.0f, .b = 2.0f},
        {.angle_x = 0.0f, .angle_y = 0.0f, .r = 10.0f, .g = 10.0f, .b = 10.0f},
    };

    limit_bright_pixels(sources, 2);

    assert(sources.size() == 2);
    assert(sources[0].r >= sources[1].r);
    assert(sources[0].r == 10.0f);
    assert(sources[1].r == 2.0f);

    limit_bright_pixels(sources, 0);
    assert(sources.size() == 2);
}

void test_bloom()
{
    std::vector<float> src_r(64, 0.0f);
    std::vector<float> src_g(64, 0.0f);
    std::vector<float> src_b(64, 0.0f);
    src_r[27] = 8.0f;
    src_g[27] = 8.0f;
    src_b[27] = 8.0f;

    std::vector<float> out_r(64, 0.0f);
    std::vector<float> out_g(64, 0.0f);
    std::vector<float> out_b(64, 0.0f);

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};
    const MutableRgbImageView output {out_r.data(), out_g.data(), out_b.data(), 8, 8};
    const BloomConfig config {
        .threshold = 1.0f,
        .strength = 1.0f,
        .radius = 0.08f,
        .passes = 1,
        .octaves = 1,
        .chromatic = false,
    };

    generate_bloom(input, output, config);

    float sum = 0.0f;
    for (float v : out_r) {
        sum += v;
    }
    assert(sum > 0.0f);
}

void test_ghost_pair_planning()
{
    assert(select_ghost_pair_ray_grid(16, 4.0f, 0.0f, 1.0f, 0) == 8);
    assert(select_ghost_pair_ray_grid(16, 64.0f, 0.0f, 1.0f, 0) == 32);
    assert(select_ghost_pair_ray_grid(16, 12.0f, 0.2f, 1.0f, 0) == 32);
    assert(select_ghost_pair_ray_grid(16, 64.0f, 0.0f, 1.0f, 24) == 24);
    assert(select_ghost_pair_ray_grid(16, 64.0f, 0.0f, 0.0f, 0) == 16);
    assert(select_ghost_footprint_radius(4.0f, 1.0f, 1.0f, 1.0f, 1.15f) <= 2.0f);
    assert(select_ghost_footprint_radius(4.0f, 49.0f, 1.0f, 1.0f, 1.15f) >= 4.0f);
    assert(select_ghost_footprint_radius(4.0f, 49.0f, 4.0f, 1.0f, 1.15f) <
           select_ghost_footprint_radius(4.0f, 49.0f, 1.0f, 1.0f, 1.15f));
    assert(select_ghost_footprint_radius(4.0f, 49.0f, 1.0f, 0.5f, 1.15f) <
           select_ghost_footprint_radius(4.0f, 49.0f, 1.0f, 1.0f, 1.15f));
    assert(std::abs(select_ghost_density_boost(3.0f, 4.0f, 4.0f) - 3.0f) < 1.0e-6f);
    assert(select_ghost_density_boost(3.0f, 4.0f, 16.0f) > 3.0f);
    assert(select_ghost_density_boost(3.0f, 4.0f, 1.0f) < 3.0f);
    assert(!select_ghost_cell_rasterization(8.0f, 0.02f));
    assert(select_ghost_cell_rasterization(32.0f, 0.02f));
    assert(select_ghost_cell_rasterization(8.0f, 0.10f));

    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    GhostConfig config {};
    config.ray_grid = 8;
    config.cleanup_mode = GhostCleanupMode::SharpAdaptive;
    config.projected_cells_mode = ProjectedCellsMode::Force;

    const auto plans = plan_active_ghost_pairs(
        lens,
        60.0f * 3.14159265358979323846f / 180.0f,
        40.0f * 3.14159265358979323846f / 180.0f,
        1920,
        1080,
        config);

    assert(!plans.empty());

    int min_grid = plans.front().ray_grid;
    int max_grid = plans.front().ray_grid;
    int cell_pairs = 0;
    for (const GhostPairPlan& plan : plans) {
        assert(plan.ray_grid >= 4);
        assert(plan.ray_grid <= config.ray_grid * 2);
        assert(plan.area_boost >= 1.0f);
        assert(plan.estimated_extent_px >= 1.0f);
        assert(plan.reference_footprint_area_px2 > 0.0f);
        assert(plan.distortion_score >= 0.0f);
        assert(plan.distortion_score <= 1.0f);
        if (plan.use_cell_rasterization) {
            ++cell_pairs;
        }
        min_grid = std::min(min_grid, plan.ray_grid);
        max_grid = std::max(max_grid, plan.ray_grid);
    }

    assert(max_grid >= min_grid);
    assert(cell_pairs >= 0);

    GhostConfig off_config = config;
    off_config.projected_cells_mode = ProjectedCellsMode::Off;
    const auto off_plans = plan_active_ghost_pairs(
        lens,
        60.0f * 3.14159265358979323846f / 180.0f,
        40.0f * 3.14159265358979323846f / 180.0f,
        1920,
        1080,
        off_config);
    for (const GhostPairPlan& plan : off_plans) {
        assert(!plan.use_cell_rasterization);
    }

    GhostConfig force_config = config;
    force_config.projected_cells_mode = ProjectedCellsMode::Force;
    const auto force_plans = plan_active_ghost_pairs(
        lens,
        60.0f * 3.14159265358979323846f / 180.0f,
        40.0f * 3.14159265358979323846f / 180.0f,
        1920,
        1080,
        force_config);
    assert(force_plans.size() == plans.size());
    for (const GhostPairPlan& plan : force_plans) {
        assert(plan.use_cell_rasterization);
    }
}

void test_render_frame()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.0f);
    std::vector<float> src_g(64, 0.0f);
    std::vector<float> src_b(64, 0.0f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 100.0f;
    settings.haze_gain = 0.25f;
    settings.haze_radius = 0.05f;
    settings.haze_blur_passes = 1;
    settings.starburst_gain = 0.1f;
    settings.starburst_scale = 0.05f;
    settings.aperture_blades = 6;
    settings.bloom.threshold = 1.0f;
    settings.bloom.strength = 0.5f;
    settings.bloom.radius = 0.08f;
    settings.bloom.passes = 1;
    settings.bloom.octaves = 1;
    settings.bloom.chromatic = false;

    FrameRenderOutputs outputs;
    assert(render_frame(lens, input, settings, outputs));
    assert(outputs.sources.size() == 1);
    assert(outputs.ghost_backend == GhostRenderBackend::CPU ||
           outputs.ghost_backend == GhostRenderBackend::CUDA);
    assert(std::string(ghost_render_backend_name(outputs.ghost_backend)).size() > 0);

    float flare_sum = 0.0f;
    float bloom_sum = 0.0f;
    float haze_sum = 0.0f;
    float starburst_sum = 0.0f;
    for (float v : outputs.flare_r) {
        flare_sum += v;
    }
    for (float v : outputs.bloom_r) {
        bloom_sum += v;
    }
    for (float v : outputs.haze_r) {
        haze_sum += v;
    }
    for (float v : outputs.starburst_r) {
        starburst_sum += v;
    }
    assert(flare_sum > 0.0f);
    assert(bloom_sum > 0.0f);
    assert(haze_sum > 0.0f);
    assert(starburst_sum > 0.0f);

    FrameRenderSettings legacy_cleanup = settings;
    legacy_cleanup.haze_gain = 0.0f;
    legacy_cleanup.starburst_gain = 0.0f;
    legacy_cleanup.bloom.strength = 0.0f;
    legacy_cleanup.ghost_blur = 0.01f;
    legacy_cleanup.ghost_blur_passes = 1;
    legacy_cleanup.ghost_cleanup_mode = GhostCleanupMode::LegacyBlur;

    FrameRenderSettings sharp_cleanup = legacy_cleanup;
    sharp_cleanup.ghost_cleanup_mode = GhostCleanupMode::SharpAdaptive;

    FrameRenderOutputs legacy_outputs;
    FrameRenderOutputs sharp_outputs;
    assert(render_frame(lens, input, legacy_cleanup, legacy_outputs));
    assert(render_frame(lens, input, sharp_cleanup, sharp_outputs));

    float legacy_peak = 0.0f;
    float sharp_peak = 0.0f;
    for (float v : legacy_outputs.flare_r) {
        legacy_peak = std::max(legacy_peak, v);
    }
    for (float v : sharp_outputs.flare_r) {
        sharp_peak = std::max(sharp_peak, v);
    }
    assert(sharp_peak >= legacy_peak);

    std::vector<float> multi_r(64, 0.0f);
    std::vector<float> multi_g(64, 0.0f);
    std::vector<float> multi_b(64, 0.0f);
    multi_r[9] = 2.0f;
    multi_g[9] = 2.0f;
    multi_b[9] = 2.0f;
    multi_r[27] = 1.5f;
    multi_g[27] = 1.5f;
    multi_b[27] = 1.5f;
    multi_r[45] = 1.25f;
    multi_g[45] = 1.25f;
    multi_b[45] = 1.25f;

    const RgbImageView multi_input {multi_r.data(), multi_g.data(), multi_b.data(), 8, 8};
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.max_sources = 1;

    FrameRenderOutputs limited_outputs;
    assert(render_frame(lens, multi_input, settings, limited_outputs));
    assert(limited_outputs.detected_sources.size() == 3);
    assert(limited_outputs.sources.size() == 1);

    std::vector<float> preview_r(64, 0.0f);
    std::vector<float> preview_g(64, 0.0f);
    std::vector<float> preview_b(64, 0.0f);
    MutableRgbImageView preview {preview_r.data(), preview_g.data(), preview_b.data(), 8, 8};
    assert(compose_output_view(AeOutputView::Sources, multi_input, settings, limited_outputs, preview));

    int preview_hits = 0;
    for (float v : preview_r) {
        if (v > 0.0f) {
            ++preview_hits;
        }
    }
    assert(preview_hits > 0);
    assert(preview_hits <= 9);
}

void test_sky_brightness()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.5f);
    std::vector<float> src_g(64, 0.5f);
    std::vector<float> src_b(64, 0.5f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 0.0f;
    settings.sky_brightness = 0.5f;

    FrameRenderOutputs outputs;
    assert(render_frame(lens, input, settings, outputs));
    assert(outputs.detected_sources.size() == 1);
    assert(outputs.scene_r.size() == 64);
    assert(std::abs(outputs.scene_r[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(outputs.scene_g[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(outputs.scene_b[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(outputs.scene_r[27] - 12.0f) < 1.0e-6f);
    assert(std::abs(outputs.scene_g[27] - 8.0f) < 1.0e-6f);
    assert(std::abs(outputs.scene_b[27] - 4.0f) < 1.0e-6f);

    std::vector<float> composite_r(64, 0.0f);
    std::vector<float> composite_g(64, 0.0f);
    std::vector<float> composite_b(64, 0.0f);
    MutableRgbImageView composite {
        composite_r.data(),
        composite_g.data(),
        composite_b.data(),
        8,
        8,
    };
    assert(compose_output_view(AeOutputView::Composite, input, settings, outputs, composite));
    assert(std::abs(composite_r[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(composite_g[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(composite_b[0] - 0.25f) < 1.0e-6f);

    std::vector<float> promoted_r(64, 0.75f);
    std::vector<float> promoted_g(64, 0.75f);
    std::vector<float> promoted_b(64, 0.75f);
    const RgbImageView promoted_input {
        promoted_r.data(),
        promoted_g.data(),
        promoted_b.data(),
        8,
        8,
    };

    settings.sky_brightness = 2.0f;
    FrameRenderOutputs promoted_outputs;
    assert(render_frame(lens, promoted_input, settings, promoted_outputs));
    assert(!promoted_outputs.detected_sources.empty());
}

void test_cuda_backend_api()
{
    assert(std::string(ghost_render_backend_name(GhostRenderBackend::CPU)) == "CPU");
    assert(std::string(ghost_render_backend_name(GhostRenderBackend::CUDA)) == "CUDA");

    std::string reason;
    const bool available = cuda_ghost_renderer_available(&reason);
    if (!cuda_ghost_renderer_compiled()) {
        assert(!available);
        assert(!reason.empty());
    }
}

void test_cuda_cell_rasterization_launch()
{
    if (!cuda_ghost_renderer_compiled()) {
        return;
    }

    std::string reason;
    if (!cuda_ghost_renderer_available(&reason)) {
        return;
    }

    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    GhostConfig config {};
    config.ray_grid = 4;
    config.cleanup_mode = GhostCleanupMode::SharpAdaptive;
    config.gain = 100.0f;
    config.aperture_blades = 6;

    const float fov_h = 60.0f * 3.14159265358979323846f / 180.0f;
    const float fov_v = 40.0f * 3.14159265358979323846f / 180.0f;
    auto plans = plan_active_ghost_pairs(lens, fov_h, fov_v, 1920, 1080, config);
    assert(!plans.empty());
    plans.resize(1);
    plans[0].use_cell_rasterization = true;

    const float sensor_half_w = lens.focal_length * std::tan(fov_h * 0.5f);
    const float sensor_half_h = lens.focal_length * std::tan(fov_v * 0.5f);
    std::vector<BrightPixel> sources {
        {0.0f, 0.0f, 8.0f, 8.0f, 8.0f},
    };
    std::vector<float> out_r(32 * 32, 0.0f);
    std::vector<float> out_g(32 * 32, 0.0f);
    std::vector<float> out_b(32 * 32, 0.0f);
    GpuBufferCache cache;
    std::string error;
    assert(launch_ghost_cuda(lens,
                             plans,
                             sources,
                             sensor_half_w,
                             sensor_half_h,
                             out_r.data(),
                             out_g.data(),
                             out_b.data(),
                             32,
                             32,
                             config,
                             cache,
                             &error));
}

void test_ae_adapter_bits()
{
    assert(builtin_lens_count() >= 1375);
    assert(builtin_lens_manufacturer_count() >= 50);
    const auto* lens = find_builtin_lens("double-gauss");
    assert(lens);
    assert(lens->manufacturer_index == 0);
    assert(std::string(lens->relative_path).find("doublegauss.lens") != std::string::npos);

    AeParameterState state {};
    state.fov_h_deg = 42.0f;
    state.fov_v_deg = 20.0f;
    state.auto_fov_v = false;
    state.use_sensor_size = true;
    state.sensor_preset_index = 2;
    state.sensor_width_mm = 36.0f;
    state.sensor_height_mm = 24.0f;
    state.focal_length_mm = 50.0f;
    state.threshold = 2.5f;
    state.downsample = 2;
    state.ray_grid = 8;
    state.max_sources = 123;
    state.aperture_blades = 7;
    state.aperture_rotation_deg = 15.0f;
    state.flare_gain = 250.0f;
    state.sky_brightness = 0.25f;
    state.ghost_blur = 0.01f;
    state.ghost_blur_passes = 2;
    state.ghost_cleanup_mode = GhostCleanupMode::SharpAdaptivePlusBlur;
    state.haze_gain = 0.2f;
    state.haze_radius = 0.1f;
    state.haze_blur_passes = 2;
    state.starburst_gain = 0.3f;
    state.starburst_scale = 0.2f;
    state.spectral_samples = 9;
    state.adaptive_sampling_strength = 1.25f;
    state.footprint_radius_bias = 0.9f;
    state.footprint_clamp = 1.4f;
    state.max_adaptive_pair_grid = 48;
    state.projected_cells_mode = ProjectedCellsMode::Off;
    state.cell_coverage_bias = 1.2f;
    state.cell_edge_inset = 0.15f;
    state.bloom.strength = 0.75f;

    const auto settings = build_frame_render_settings(state);
    assert(settings.use_sensor_size);
    assert(settings.sensor_preset_index == 2);
    assert(std::abs(settings.fov_h_deg - 42.0f) < 1e-6f);
    assert(std::abs(settings.fov_v_deg - 20.0f) < 1e-6f);
    assert(!settings.auto_fov_v);
    assert(std::abs(settings.sensor_width_mm - 36.0f) < 1e-6f);
    assert(std::abs(settings.sensor_height_mm - 24.0f) < 1e-6f);
    assert(std::abs(settings.focal_length_mm - 50.0f) < 1e-6f);
    assert(std::abs(settings.threshold - 2.5f) < 1e-6f);
    assert(settings.downsample == 2);
    assert(settings.ray_grid == 8);
    assert(settings.max_sources == 123);
    assert(settings.aperture_blades == 7);
    assert(std::abs(settings.aperture_rotation_deg - 15.0f) < 1e-6f);
    assert(std::abs(settings.flare_gain - 250.0f) < 1e-6f);
    assert(std::abs(settings.sky_brightness - 0.25f) < 1e-6f);
    assert(std::abs(settings.ghost_blur - 0.01f) < 1e-6f);
    assert(settings.ghost_blur_passes == 2);
    assert(settings.ghost_cleanup_mode == GhostCleanupMode::SharpAdaptivePlusBlur);
    assert(std::abs(settings.haze_gain - 0.2f) < 1e-6f);
    assert(std::abs(settings.haze_radius - 0.1f) < 1e-6f);
    assert(settings.haze_blur_passes == 2);
    assert(std::abs(settings.starburst_gain - 0.3f) < 1e-6f);
    assert(std::abs(settings.starburst_scale - 0.2f) < 1e-6f);
    assert(settings.spectral_samples == 9);
    assert(std::abs(settings.adaptive_sampling_strength - 1.25f) < 1e-6f);
    assert(std::abs(settings.footprint_radius_bias - 0.9f) < 1e-6f);
    assert(std::abs(settings.footprint_clamp - 1.4f) < 1e-6f);
    assert(settings.max_adaptive_pair_grid == 48);
    assert(settings.projected_cells_mode == ProjectedCellsMode::Off);
    assert(std::abs(settings.cell_coverage_bias - 1.2f) < 1e-6f);
    assert(std::abs(settings.cell_edge_inset - 0.15f) < 1e-6f);
    assert(std::abs(settings.bloom.strength - 0.75f) < 1e-6f);

    state.ray_grid = 2048;
    state.max_sources = 4096;
    const auto unclamped = build_frame_render_settings(state);
    assert(unclamped.ray_grid == 2048);
    assert(unclamped.max_sources == 4096);

    state.max_sources = 0;
    const auto unlimited = build_frame_render_settings(state);
    assert(unlimited.max_sources == 0);

    std::string asset_root;
    assert(find_flaresim_asset_root(repo_path("src/ae"), asset_root));
    assert(asset_root == std::string(FLARESIM_REPO_ROOT));
    assert(is_flaresim_asset_root(asset_root));

    std::string resolved;
    assert(resolve_lens_path(state.lens, asset_root, resolved));
    assert(resolved.find("doublegauss.lens") != std::string::npos);

    LensSystem loaded;
    assert(load_selected_lens(state.lens, asset_root, loaded));
    assert(loaded.num_surfaces() > 0);
}

void test_output_views()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.1f);
    std::vector<float> src_g(64, 0.1f);
    std::vector<float> src_b(64, 0.1f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 100.0f;
    settings.bloom.threshold = 1.0f;
    settings.bloom.strength = 0.5f;
    settings.bloom.radius = 0.08f;
    settings.bloom.passes = 1;
    settings.bloom.octaves = 1;
    settings.bloom.chromatic = false;

    FrameRenderOutputs outputs;
    assert(render_frame(lens, input, settings, outputs));

    std::vector<float> out_r(64, 0.0f);
    std::vector<float> out_g(64, 0.0f);
    std::vector<float> out_b(64, 0.0f);
    MutableRgbImageView output {out_r.data(), out_g.data(), out_b.data(), 8, 8};

    assert(compose_output_view(AeOutputView::FlareOnly, input, settings, outputs, output));
    float flare_sum = 0.0f;
    for (float v : out_r) flare_sum += v;
    assert(flare_sum > 0.0f);

    std::fill(out_r.begin(), out_r.end(), 0.0f);
    std::fill(out_g.begin(), out_g.end(), 0.0f);
    std::fill(out_b.begin(), out_b.end(), 0.0f);
    assert(compose_output_view(AeOutputView::BloomOnly, input, settings, outputs, output));
    float bloom_sum = 0.0f;
    for (float v : out_r) bloom_sum += v;
    assert(bloom_sum > 0.0f);

    std::fill(out_r.begin(), out_r.end(), 0.0f);
    std::fill(out_g.begin(), out_g.end(), 0.0f);
    std::fill(out_b.begin(), out_b.end(), 0.0f);
    assert(compose_output_view(AeOutputView::Sources, input, settings, outputs, output));
    float source_sum = 0.0f;
    for (float v : out_r) source_sum += v;
    assert(source_sum > 0.0f);
}

void test_render_plan_cache()
{
    LensSystem lens;
    const std::string path = repo_path("assets/lenses/space55/doublegauss.lens");
    assert(lens.load(path.c_str()));

    std::vector<float> src_r(64, 0.0f);
    std::vector<float> src_g(64, 0.0f);
    std::vector<float> src_b(64, 0.0f);
    src_r[27] = 12.0f;
    src_g[27] = 8.0f;
    src_b[27] = 4.0f;

    const RgbImageView input {src_r.data(), src_g.data(), src_b.data(), 8, 8};

    FrameRenderSettings settings {};
    settings.fov_h_deg = 60.0f;
    settings.threshold = 1.0f;
    settings.downsample = 1;
    settings.ray_grid = 4;
    settings.flare_gain = 100.0f;
    settings.ghost_blur = 0.0f;
    settings.haze_gain = 0.25f;
    settings.haze_radius = 0.05f;
    settings.haze_blur_passes = 1;
    settings.starburst_gain = 0.1f;
    settings.starburst_scale = 0.05f;
    settings.aperture_blades = 6;
    settings.bloom.threshold = 1.0f;
    settings.bloom.strength = 0.5f;
    settings.bloom.radius = 0.08f;
    settings.bloom.passes = 1;
    settings.bloom.octaves = 1;
    settings.bloom.chromatic = false;

    const FrameRenderPlan composite_plan = build_output_view_render_plan(AeOutputView::Composite);
    const FrameRenderPlan sources_plan = build_output_view_render_plan(AeOutputView::Sources);

    FrameRenderCache cache;

    FrameRenderOutputs first_outputs;
    assert(render_frame(lens, input, settings, first_outputs, composite_plan, &cache));
    assert(first_outputs.stats.recomputed_scene);
    assert(first_outputs.stats.recomputed_sources);
    assert(first_outputs.stats.recomputed_ghosts);
    assert(first_outputs.stats.recomputed_bloom);
    assert(first_outputs.stats.recomputed_haze);
    assert(first_outputs.stats.recomputed_starburst);

    float first_peak = 0.0f;
    for (float v : first_outputs.flare_r) {
        first_peak = std::max(first_peak, v);
    }
    assert(first_peak > 0.0f);

    FrameRenderSettings blurred_settings = settings;
    blurred_settings.ghost_blur = 0.02f;
    blurred_settings.ghost_blur_passes = 1;

    FrameRenderOutputs blurred_outputs;
    assert(render_frame(lens, input, blurred_settings, blurred_outputs, composite_plan, &cache));
    assert(!blurred_outputs.stats.recomputed_scene);
    assert(!blurred_outputs.stats.recomputed_sources);
    assert(!blurred_outputs.stats.recomputed_ghosts);
    assert(!blurred_outputs.stats.recomputed_bloom);
    assert(!blurred_outputs.stats.recomputed_haze);
    assert(!blurred_outputs.stats.recomputed_starburst);

    float blurred_peak = 0.0f;
    for (float v : blurred_outputs.flare_r) {
        blurred_peak = std::max(blurred_peak, v);
    }
    assert(blurred_peak > 0.0f);
    assert(blurred_peak <= first_peak);

    FrameRenderOutputs source_outputs;
    assert(render_frame(lens, input, settings, source_outputs, sources_plan, nullptr));
    assert(source_outputs.stats.recomputed_sources);
    assert(!source_outputs.stats.recomputed_ghosts);
    assert(!source_outputs.stats.recomputed_bloom);
    assert(!source_outputs.stats.recomputed_haze);
    assert(!source_outputs.stats.recomputed_starburst);
    assert(source_outputs.flare_r.empty());
    assert(source_outputs.bloom_r.empty());
    assert(source_outputs.haze_r.empty());
    assert(source_outputs.starburst_r.empty());
    assert(!source_outputs.sources.empty());
}

void test_pixel_convert()
{
    const FloatPixel hdr {1.0f, 1.5f, 0.5f, 2.25f};

    const auto p8 = pack_pixel8(hdr);
    const auto p16 = pack_pixel16(hdr);
    const auto p32 = pack_pixel32(hdr);

    assert(p8.red == 255);
    assert(p16.red == 32768);
    assert(std::abs(p32.red - 1.5f) < 1e-6f);
    assert(std::abs(p32.blue - 2.25f) < 1e-6f);

    const auto f8 = unpack_pixel(p8);
    const auto f16 = unpack_pixel(p16);
    const auto f32 = unpack_pixel(p32);

    assert(f8.red <= 1.0f);
    assert(f16.red <= 1.0f);
    assert(std::abs(f32.red - 1.5f) < 1e-6f);
    assert(std::abs(f32.blue - 2.25f) < 1e-6f);
}

void test_frame_bridge()
{
    std::string asset_root;
    assert(find_flaresim_asset_root(FLARESIM_REPO_ROOT, asset_root));

    std::vector<AePixel8Like> src8(64);
    std::vector<AePixel8Like> dst8(64);
    std::vector<AePixel16Like> src16(64);
    std::vector<AePixel16Like> dst16(64);
    std::vector<AePixel32Like> src32(64);
    std::vector<AePixel32Like> dst32(64);

    src8[27] = pack_pixel8(FloatPixel {1.0f, 0.9f, 0.7f, 0.4f});
    src16[27] = pack_pixel16(FloatPixel {1.0f, 0.9f, 0.7f, 0.4f});
    src32[27] = pack_pixel32(FloatPixel {1.0f, 12.0f, 8.0f, 4.0f});

    AeParameterState state {};
    state.threshold = 0.25f;
    state.downsample = 1;
    state.ray_grid = 4;
    state.flare_gain = 100.0f;
    state.sky_brightness = 0.5f;
    state.bloom.threshold = 0.25f;
    state.bloom.strength = 0.5f;
    state.bloom.radius = 0.08f;
    state.bloom.passes = 1;
    state.bloom.octaves = 1;
    state.bloom.chromatic = false;

    assert(render_frame_to_pixels(asset_root, state, src8.data(), dst8.data(), 8, 8));
    assert(render_frame_to_pixels(asset_root, state, src16.data(), dst16.data(), 8, 8));
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));

    FloatImageBuffer out8;
    FloatImageBuffer out16;
    FloatImageBuffer out32;
    assert(unpack_image(dst8.data(), 8, 8, out8));
    assert(unpack_image(dst16.data(), 8, 8, out16));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float sum8 = 0.0f;
    float sum16 = 0.0f;
    float sum32 = 0.0f;
    for (float v : out8.r) sum8 += v;
    for (float v : out16.r) sum16 += v;
    for (float v : out32.r) sum32 += v;

    assert(sum8 > 0.0f);
    assert(sum16 > 0.0f);
    assert(sum32 > 0.0f);
    assert(std::abs(out32.alpha[27] - 1.0f) < 1e-6f);

    state.view = AeOutputView::FlareOnly;
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float max32 = 0.0f;
    float sum_flare32 = 0.0f;
    for (float v : out32.r) {
        max32 = std::max(max32, v);
        sum_flare32 += v;
    }
    assert(max32 > 0.0f);
    assert(sum_flare32 > 0.0f);

    std::fill(src32.begin(), src32.end(), pack_pixel32(FloatPixel {1.0f, 0.0f, 0.0f, 0.0f}));
    src32[27] = pack_pixel32(FloatPixel {1.0f, 0.98f, 0.98f, 0.98f});

    state.threshold = 0.9f;
    state.downsample = 8;
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float near_white_sum = 0.0f;
    for (float v : out32.r) {
        near_white_sum += v;
    }
    assert(near_white_sum > 0.0f);

    state.downsample = 1;
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float near_white_sum_ds1 = 0.0f;
    for (float v : out32.r) {
        near_white_sum_ds1 += v;
    }
    assert(near_white_sum_ds1 > 0.0f);

    std::fill(src32.begin(), src32.end(), pack_pixel32(FloatPixel {1.0f, 0.5f, 0.5f, 0.5f}));
    state.view = AeOutputView::Composite;
    state.threshold = 1.0f;
    state.flare_gain = 0.0f;
    state.bloom.strength = 0.0f;
    state.haze_gain = 0.0f;
    state.starburst_gain = 0.0f;
    state.sky_brightness = 0.5f;

    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));
    assert(std::abs(out32.r[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(out32.g[0] - 0.25f) < 1.0e-6f);
    assert(std::abs(out32.b[0] - 0.25f) < 1.0e-6f);

    std::fill(src32.begin(), src32.end(), pack_pixel32(FloatPixel {1.0f, 0.0f, 0.0f, 0.0f}));
    src32[27] = pack_pixel32(FloatPixel {1.0f, 12.0f, 8.0f, 4.0f});

    state.sky_brightness = 1.0f;
    state.threshold = 1.0f;
    state.flare_gain = 100.0f;
    state.haze_gain = 0.0f;
    state.starburst_gain = 0.0f;
    state.bloom.strength = 0.0f;
    state.view = AeOutputView::Composite;

    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    float composite_sum = 0.0f;
    for (float v : out32.r) {
        composite_sum += v;
    }
    assert(composite_sum > 12.0f);

    state.view = AeOutputView::Sources;
    assert(render_frame_to_pixels(asset_root, state, src32.data(), dst32.data(), 8, 8));
    assert(unpack_image(dst32.data(), 8, 8, out32));

    int source_hits = 0;
    for (float v : out32.r) {
        if (v > 0.0f) {
            ++source_hits;
        }
    }
    assert(source_hits > 0);
    assert(source_hits <= 9);
}

void test_param_schema()
{
    assert(parameter_count() > 100);
    assert(lens_section_start_param() == PARAM_LENS_SECTION_START);
    assert(lens_section_end_param() + 1 == camera_section_start_param());
    assert(camera_section_end_param() + 1 == aperture_section_start_param());
    assert(aperture_section_end_param() + 1 == flare_section_start_param());
    assert(flare_section_start_param() + 1 == projected_cells_mode_param());
    assert(projected_cells_mode_param() + 1 == flare_gain_param());
    assert(flare_gain_param() + 1 == sky_brightness_param());
    assert(sky_brightness_param() + 1 == threshold_param());
    assert(flare_section_end_param() + 1 == post_section_start_param());
    assert(spectral_samples_param() + 1 == ghost_cleanup_mode_param());
    assert(ghost_cleanup_mode_param() + 1 == advanced_ghosts_section_start_param());
    assert(advanced_ghosts_section_start_param() + 1 == adaptive_sampling_strength_param());
    assert(adaptive_sampling_strength_param() + 1 == footprint_radius_bias_param());
    assert(footprint_radius_bias_param() + 1 == footprint_clamp_param());
    assert(footprint_clamp_param() + 1 == max_adaptive_pair_grid_param());
    assert(max_adaptive_pair_grid_param() + 1 == cell_coverage_bias_param());
    assert(cell_coverage_bias_param() + 1 == cell_edge_inset_param());
    assert(cell_edge_inset_param() + 1 == advanced_ghosts_section_end_param());
    assert(advanced_ghosts_section_end_param() + 1 == post_section_end_param());
    assert(post_section_end_param() + 1 == view_mode_param());
    assert(mask_layer_param() + 1 == parameter_count());
    assert(PARAM_ID_HAZE_GAIN == 19);
    assert(PARAM_ID_SPECTRAL_SAMPLES == 24);
    assert(PARAM_ID_VIEW_MODE == 25);
    assert(PARAM_ID_MASK_LAYER == 26);
    assert(PARAM_ID_GHOST_CLEANUP_MODE == 27);
    assert(PARAM_ID_SKY_BRIGHTNESS == 28);
    assert(PARAM_ID_ADAPTIVE_SAMPLING_STRENGTH == 29);
    assert(PARAM_ID_FOOTPRINT_RADIUS_BIAS == 30);
    assert(PARAM_ID_FOOTPRINT_CLAMP == 31);
    assert(PARAM_ID_MAX_ADAPTIVE_PAIR_GRID == 32);
    assert(PARAM_ID_PROJECTED_CELLS_MODE == 33);
    assert(PARAM_ID_CELL_COVERAGE_BIAS == 34);
    assert(PARAM_ID_CELL_EDGE_INSET == 35);

    const std::string legacy_lens_popup = build_lens_preset_popup_string();
    const std::string manufacturer_popup = build_lens_manufacturer_popup_string();
    const std::string grouped_lens_popup = build_lens_popup_string_for_manufacturer(0);
    const std::string sensor_preset_popup = build_sensor_preset_popup_string();
    const std::string spectral_popup = build_spectral_samples_popup_string();
    const std::string cleanup_popup = build_ghost_cleanup_mode_popup_string();
    const std::string projected_cells_popup = build_projected_cells_mode_popup_string();
    const std::string view_popup = build_output_view_popup_string();
    assert(legacy_lens_popup.find("Double Gauss") != std::string::npos);
    assert(manufacturer_popup.find("Canon") != std::string::npos);
    assert(grouped_lens_popup.find("Double Gauss") != std::string::npos);
    assert(sensor_preset_popup.find("Full Frame") != std::string::npos);
    assert(spectral_popup.find("11") != std::string::npos);
    assert(cleanup_popup.find("Sharp Adaptive") != std::string::npos);
    assert(projected_cells_popup.find("Disabled") != std::string::npos);
    assert(projected_cells_popup.find("Enabled") != std::string::npos);
    assert(view_popup.find("Flare Only") != std::string::npos);

    const char* canon_lens_id = "canon-1-4x-tc-canon-extender-ef1-4x-iii";
    AeUiParameterState ui {};
    ui.legacy_lens_preset_index = lens_popup_index_for_builtin("double-gauss");
    ui.lens_manufacturer_index = lens_manufacturer_popup_index_for_builtin(canon_lens_id);
    ui.lens_model_index = lens_model_popup_index_for_builtin(canon_lens_id);
    ui.use_sensor_size = true;
    ui.sensor_preset_index = 2;
    ui.fov_h_deg = 45.0f;
    ui.auto_fov_v = false;
    ui.fov_v_deg = 18.0f;
    ui.sensor_width_mm = 12.0f;
    ui.sensor_height_mm = 8.0f;
    ui.focal_length_mm = 75.0f;
    ui.aperture_blades = 8;
    ui.aperture_rotation_deg = 12.0f;
    ui.view_mode_index = output_view_popup_index(AeOutputView::Diagnostics);
    ui.flare_gain = 250.0f;
    ui.sky_brightness = 0.25f;
    ui.threshold = 1.5f;
    ui.ray_grid = 8;
    ui.downsample = 2;
    ui.max_sources = 222;
    ui.ghost_blur = 0.02f;
    ui.ghost_blur_passes = 2;
    ui.ghost_cleanup_mode_index = ghost_cleanup_mode_popup_index(GhostCleanupMode::SharpAdaptive);
    ui.haze_gain = 0.1f;
    ui.haze_radius = 0.05f;
    ui.haze_blur_passes = 2;
    ui.starburst_gain = 0.2f;
    ui.starburst_scale = 0.1f;
    ui.spectral_samples_index = spectral_samples_popup_index(11);
    ui.adaptive_sampling_strength = 1.5f;
    ui.footprint_radius_bias = 0.85f;
    ui.footprint_clamp = 1.8f;
    ui.max_adaptive_pair_grid = 40;
    ui.projected_cells_mode_index = projected_cells_mode_popup_index(ProjectedCellsMode::Off);
    ui.cell_coverage_bias = 1.35f;
    ui.cell_edge_inset = 0.2f;

    AeParameterState state {};
    assert(apply_ui_parameter_state(ui, state));
    assert(std::string(state.lens.builtin_id) == canon_lens_id);
    assert(state.view == AeOutputView::Diagnostics);
    assert(state.use_sensor_size);
    assert(state.sensor_preset_index == 2);
    assert(std::abs(state.fov_h_deg - 45.0f) < 1e-6f);
    assert(!state.auto_fov_v);
    assert(std::abs(state.fov_v_deg - 18.0f) < 1e-6f);
    assert(std::abs(state.sensor_width_mm - 36.0f) < 1e-6f);
    assert(std::abs(state.sensor_height_mm - 24.0f) < 1e-6f);
    assert(std::abs(state.focal_length_mm - 75.0f) < 1e-6f);
    assert(state.aperture_blades == 8);
    assert(std::abs(state.aperture_rotation_deg - 12.0f) < 1e-6f);
    assert(std::abs(state.flare_gain - 250.0f) < 1e-6f);
    assert(std::abs(state.sky_brightness - 0.25f) < 1e-6f);
    assert(std::abs(state.threshold - 1.5f) < 1e-6f);
    assert(state.ray_grid == 8);
    assert(state.downsample == 2);
    assert(state.max_sources == 222);
    assert(std::abs(state.ghost_blur - 0.02f) < 1e-6f);
    assert(state.ghost_blur_passes == 2);
    assert(state.ghost_cleanup_mode == GhostCleanupMode::SharpAdaptive);
    assert(std::abs(state.haze_gain - 0.1f) < 1e-6f);
    assert(std::abs(state.haze_radius - 0.05f) < 1e-6f);
    assert(state.haze_blur_passes == 2);
    assert(std::abs(state.starburst_gain - 0.2f) < 1e-6f);
    assert(std::abs(state.starburst_scale - 0.1f) < 1e-6f);
    assert(state.spectral_samples == 11);
    assert(std::abs(state.adaptive_sampling_strength - 1.5f) < 1e-6f);
    assert(std::abs(state.footprint_radius_bias - 0.85f) < 1e-6f);
    assert(std::abs(state.footprint_clamp - 1.8f) < 1e-6f);
    assert(state.max_adaptive_pair_grid == 40);
    assert(state.projected_cells_mode == ProjectedCellsMode::Off);
    assert(std::abs(state.cell_coverage_bias - 1.35f) < 1e-6f);
    assert(std::abs(state.cell_edge_inset - 0.2f) < 1e-6f);

    AeUiParameterState legacy_ui {};
    legacy_ui.legacy_lens_preset_index = lens_popup_index_for_builtin("cooke-triplet");
    legacy_ui.lens_manufacturer_index = default_lens_manufacturer_popup_index();
    legacy_ui.lens_model_index = default_lens_model_popup_index();
    legacy_ui.view_mode_index = output_view_popup_index(AeOutputView::Composite);

    AeParameterState legacy_state {};
    assert(apply_ui_parameter_state(legacy_ui, legacy_state));
    assert(std::string(legacy_state.lens.builtin_id) == "cooke-triplet");
}

} // namespace

int main()
{
    test_lens_load();
    test_source_extract();
    test_source_limit();
    test_bloom();
    test_ghost_pair_planning();
    test_render_frame();
    test_sky_brightness();
    test_cuda_backend_api();
    test_cuda_cell_rasterization_launch();
    test_ae_adapter_bits();
    test_output_views();
    test_render_plan_cache();
    test_pixel_convert();
    test_frame_bridge();
    test_param_schema();
    std::cout << "flaresim_core_smoke: ok\n";
    return 0;
}
