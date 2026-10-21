#include "ec_budget.h"

#include "ghost.h"
#include "source_extract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

// EffectCraft 0.13, crates/plugin/src/wasm.rs.
constexpr double kFuelBase = 50.0e6;
constexpr double kFuelPerPixel = 20.0e3;

// Aim below the budget: the estimate is a model, and a frame that overruns renders nothing.
constexpr double kBudgetHeadroom = 0.6;

// Fuel costs measured with tests/ec_host (wasmi 2, -O3), rounded towards the worst case seen
// across the bundled lenses: an overestimate only lowers quality, an underestimate renders
// nothing. A trace is costed by the surfaces a ghost path crosses when nothing vignettes, so
// the estimate is pessimistic for lenses whose rays vignette early (the ARRI Master Prime
// traces about 3x cheaper than estimated).
constexpr double kFuelPerFramePixel = 1500.0;       // unpack, scene prep, extraction, compose, pack
constexpr double kFuelPerBlurPixelPass = 170.0;     // box blur, all three channels
constexpr double kFuelPerPlannedPair = 1.2e6;       // pair planning probes; paid by the governor and the setup
constexpr double kFuelPerTraceSurface = 430.0;      // one ray, one wavelength, one surface crossed
constexpr double kFuelPerTrace = 300.0;             // per-ray overhead on top of the surfaces
constexpr double kFootprintTraces = 4.0;            // Sharp modes trace neighbours per sample
constexpr double kFuelPerTentPixel = 28.0;          // one channel of one tent splat pixel
constexpr double kFuelPerCellPixel = 60.0;          // one channel of one rasterized quad pixel

constexpr double kQuarterPi = 0.78539816339;

bool is_sharp(GhostCleanupMode mode)
{
    return mode != GhostCleanupMode::LegacyBlur;
}

int blur_passes(const FrameRenderSettings& settings, const FrameRenderPlan& plan, GhostCleanupMode cleanup)
{
    int passes = 0;
    const bool ghost_blur =
        plan.need_flare && settings.ghost_blur > 0.0f && settings.ghost_blur_passes > 0 &&
        cleanup != GhostCleanupMode::SharpAdaptive;
    if (ghost_blur) {
        passes += settings.ghost_blur_passes;
    }
    if (plan.need_haze && settings.haze_gain > 0.0f && settings.haze_radius > 0.0f) {
        passes += settings.haze_blur_passes;
    }
    if (plan.need_bloom && settings.bloom.strength > 0.0f) {
        passes += settings.bloom.passes * settings.bloom.octaves;
    }
    return passes;
}

double fixed_fuel(const FrameRenderSettings& settings,
                  const FrameRenderPlan& plan,
                  int width,
                  int height,
                  GhostCleanupMode cleanup)
{
    const double np = static_cast<double>(width) * static_cast<double>(height);
    return np * (kFuelPerFramePixel + kFuelPerBlurPixelPass * blur_passes(settings, plan, cleanup));
}

// One ray, one wavelength: forward to surf_b, back to surf_a, then out to the sensor.
double fuel_per_trace(const LensSystem& lens, const GhostPair& pair)
{
    const double crossed = lens.num_surfaces() + 2.0 * std::max(pair.surf_b - pair.surf_a, 0);
    return kFuelPerTrace + kFuelPerTraceSurface * crossed;
}

// Ghost tracing cost of one source, summed over the active pairs.
double fuel_per_source(const LensSystem& lens,
                       const std::vector<GhostPairPlan>& pairs,
                       const FrameRenderSettings& settings,
                       int base_ray_grid,
                       GhostCleanupMode cleanup)
{
    const double wavelengths = std::max(settings.spectral_samples, 1);
    const bool sharp = is_sharp(cleanup);
    double fuel = 0.0;
    for (const GhostPairPlan& pair : pairs) {
        const int grid = select_ghost_pair_ray_grid(base_ray_grid,
                                                    pair.estimated_extent_px,
                                                    pair.distortion_score,
                                                    settings.adaptive_quality,
                                                    settings.adaptive_sampling_strength,
                                                    settings.max_adaptive_pair_grid);
        const double samples = std::max(kQuarterPi * grid * grid, 1.0);
        const double extent = std::max(static_cast<double>(pair.estimated_extent_px), 1.0);
        const double trace = fuel_per_trace(lens, pair.pair);

        const bool cells = sharp && pair.use_cell_rasterization;
        if (cells) {
            // Four corner traces per cell, then the quads cover the ghost once per channel.
            const double covered = kQuarterPi * extent * extent;
            fuel += wavelengths * (samples * 4.0 * trace + 3.0 * covered * kFuelPerCellPixel);
            continue;
        }

        fuel += wavelengths * samples * trace;
        if (sharp) {
            // Every hit is tent-splatted over about the ray spacing (ghost.cpp, render_ghosts):
            // radius = clamp(1.2 * extent / sqrt(hits), 1.5, 80), so the total area stays near
            // the ghost's own area whatever the ray grid.
            const double radius = std::clamp(1.2 * extent / std::sqrt(samples), 1.5, 80.0);
            const double footprint = (2.0 * radius + 1.0) * (2.0 * radius + 1.0);
            fuel += samples * kFootprintTraces * trace;
            fuel += wavelengths * samples * 3.0 * footprint * kFuelPerTentPixel;
        }
    }
    return fuel;
}

// Distance between neighbouring rays on the sensor: the 75th percentile over the active pairs,
// so the large, sparsely sampled ghosts count.
float ray_spacing_px(const std::vector<GhostPairPlan>& pairs,
                            const FrameRenderSettings& settings,
                            int base_ray_grid)
{
    std::vector<float> spacing;
    spacing.reserve(pairs.size());
    for (const GhostPairPlan& pair : pairs) {
        const int grid = select_ghost_pair_ray_grid(base_ray_grid,
                                                    pair.estimated_extent_px,
                                                    pair.distortion_score,
                                                    settings.adaptive_quality,
                                                    settings.adaptive_sampling_strength,
                                                    settings.max_adaptive_pair_grid);
        spacing.push_back(pair.estimated_extent_px / static_cast<float>(std::max(grid, 1)));
    }
    if (spacing.empty()) {
        return 0.0f;
    }
    const auto middle = spacing.begin() + static_cast<std::ptrdiff_t>((spacing.size() * 3) / 4);
    std::nth_element(spacing.begin(), middle, spacing.end());
    return *middle;
}

// Planning probes every candidate pair, active or not.
std::size_t lens_pair_count(const LensSystem& lens)
{
    const std::size_t n = static_cast<std::size_t>(std::max(lens.num_surfaces(), 0));
    return n * (n > 0 ? n - 1 : 0) / 2;
}

} // namespace

double ec_frame_fuel_budget(int width, int height)
{
    return kFuelBase + kFuelPerPixel * static_cast<double>(width) * static_cast<double>(height);
}

EcBudgetPlan ec_fit_render_budget(const LensSystem& lens,
                                  FrameRenderSettings& settings,
                                  const FrameRenderPlan& plan,
                                  int width,
                                  int height,
                                  const std::vector<BrightPixel>& detected_sources,
                                  bool fit)
{
    EcBudgetPlan result;
    result.budget_fuel = ec_frame_fuel_budget(width, height);
    result.requested_ray_grid = settings.ray_grid;
    result.ray_grid = settings.ray_grid;
    result.requested_cleanup = settings.ghost_cleanup_mode;
    result.cleanup = settings.ghost_cleanup_mode;
    result.requested_cluster_radius = settings.cluster_radius_px;
    result.cluster_radius = settings.cluster_radius_px;

    float fov_h = 0.0f;
    float fov_v = 0.0f;
    const bool have_fov = compute_camera_fov(settings, width, height, fov_h, fov_v);
    const float tan_half_fov_h = have_fov ? std::tan(fov_h * 0.5f) : 0.0f;

    // render_frame() appends the manual source after clustering and Max Sources.
    const int manual = settings.manual_source_enabled && !detected_sources.empty() ? 1 : 0;
    const auto detected_end = detected_sources.end() - manual;
    std::vector<BrightPixel> scratch;
    auto count_sources = [&](int cluster_radius, int max_sources) {
        scratch.assign(detected_sources.begin(), detected_end);
        cluster_bright_pixels(scratch, cluster_radius, width, tan_half_fov_h);
        limit_bright_pixels(scratch, static_cast<std::size_t>(std::max(max_sources, 0)));
        return static_cast<int>(scratch.size()) + manual;
    };

    int sources = count_sources(settings.cluster_radius_px, settings.max_sources);
    result.requested_sources = sources;
    result.sources = sources;

    const double requested_fixed = fixed_fuel(settings, plan, width, height, settings.ghost_cleanup_mode);
    result.requested_fuel = requested_fixed;
    result.estimated_fuel = requested_fixed;
    if (!plan.need_flare || !have_fov || sources <= 0 || settings.ray_grid <= 0) {
        return result;
    }

    const GhostConfig ghost = build_ghost_config(settings);
    const std::vector<GhostPairPlan> pairs = plan_active_ghost_pairs(lens, fov_h, fov_v, width, height, ghost);
    result.active_pairs = static_cast<int>(pairs.size());

    const double planning = 2.0 * kFuelPerPlannedPair * static_cast<double>(lens_pair_count(lens));
    auto estimate = [&](int n, int ray_grid, GhostCleanupMode cleanup, const std::vector<GhostPairPlan>& p) {
        return fixed_fuel(settings, plan, width, height, cleanup) + planning +
               n * fuel_per_source(lens, p, settings, ray_grid, cleanup);
    };

    result.requested_fuel = estimate(sources, settings.ray_grid, settings.ghost_cleanup_mode, pairs);
    result.estimated_fuel = result.requested_fuel;
    const double target = result.budget_fuel * kBudgetHeadroom;
    if (!fit || result.requested_fuel <= target) {
        return result;
    }

    int ray_grid = settings.ray_grid;
    int cluster_radius = settings.cluster_radius_px;
    int max_sources = settings.max_sources;
    GhostCleanupMode cleanup = settings.ghost_cleanup_mode;
    std::vector<GhostPairPlan> legacy_pairs;
    const std::vector<GhostPairPlan>* active = &pairs;
    auto fits = [&]() { return estimate(sources, ray_grid, cleanup, *active) <= target; };

    // 1. Sharp splatting costs about the ghosts' area for every source, whatever the ray grid.
    //    Keep it only if every source fits at a reasonable grid.
    if (is_sharp(cleanup) &&
        estimate(sources, std::min(ray_grid, kEcPreferredMinRayGrid), cleanup, pairs) > target) {
        cleanup = GhostCleanupMode::LegacyBlur;
        FrameRenderSettings legacy = settings;
        legacy.ghost_cleanup_mode = cleanup;
        legacy_pairs = plan_active_ghost_pairs(lens, fov_h, fov_v, width, height, build_ghost_config(legacy));
        active = &legacy_pairs;
    }

    // 2. Merge neighbouring sources: a highlight breaks into many source blocks, and merging
    //    them keeps its energy and position while tracing it once. Merging stays local here
    //    (about 20 px at 1080p); a sparse grid reads worse than merged highlights.
    auto cluster_up_to = [&](int limit) {
        while (!fits() && cluster_radius < limit) {
            cluster_radius = std::min(limit, std::max(cluster_radius * 2, 4));
            sources = count_sources(cluster_radius, max_sources);
        }
    };
    cluster_up_to(std::max(4, width / 96));

    // 3. Ray grid, down to a grid that still reads as a shape.
    while (!fits() && ray_grid > kEcPreferredMinRayGrid) {
        ray_grid = std::max(kEcPreferredMinRayGrid, static_cast<int>(std::floor(ray_grid * 0.85)));
    }

    // 4. Merge across larger distances.
    cluster_up_to(std::max(4, width / 8));

    // 5. Keep the brightest sources.
    if (!fits()) {
        const double fixed = fixed_fuel(settings, plan, width, height, cleanup) + planning;
        const double per_source = std::max(fuel_per_source(lens, *active, settings, ray_grid, cleanup), 1.0);
        const int affordable = std::max(1, static_cast<int>(std::floor((target - fixed) / per_source)));
        if (affordable < sources) {
            max_sources = std::max(1, affordable - manual);
            sources = count_sources(cluster_radius, max_sources);
        }
    }

    // 6. Last resort: the smallest grid.
    while (!fits() && ray_grid > kEcMinBudgetRayGrid) {
        ray_grid = std::max(kEcMinBudgetRayGrid, ray_grid - 2);
    }

    settings.ray_grid = ray_grid;
    settings.cluster_radius_px = cluster_radius;
    settings.max_sources = max_sources;
    settings.ghost_cleanup_mode = cleanup;

    // Legacy Blur splats each ray onto ~4 pixels, so a sparse grid leaves big ghosts as dot
    // lattices. Halton pupil jitter turns the lattice into irregular grain (deterministic, so
    // frames don't flicker), and the blur spreads it over about half the ray spacing of the
    // larger ghosts (box blur cost doesn't depend on the radius). Never lowers the user's blur.
    if (cleanup == GhostCleanupMode::LegacyBlur) {
        if (settings.pupil_jitter_mode == PupilJitterMode::Off) {
            settings.pupil_jitter_mode = PupilJitterMode::Halton;
        }
        const float fill_radius_px = 0.5f * ray_spacing_px(*active, settings, ray_grid);
        const float diag = std::sqrt(static_cast<float>(width) * width + static_cast<float>(height) * height);
        if (diag > 0.0f && fill_radius_px > settings.ghost_blur * diag) {
            settings.ghost_blur = fill_radius_px / diag;
            settings.ghost_blur_passes = std::max(settings.ghost_blur_passes, 2);
            result.ghost_blur = settings.ghost_blur;
        }
    }

    result.sources = sources;
    result.ray_grid = ray_grid;
    result.cleanup = cleanup;
    result.cluster_radius = cluster_radius;
    result.estimated_fuel = estimate(sources, ray_grid, cleanup, *active);
    result.reduced = true;
    return result;
}
