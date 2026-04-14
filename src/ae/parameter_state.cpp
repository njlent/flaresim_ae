#include "parameter_state.h"

FrameRenderSettings build_frame_render_settings(const AeParameterState& state)
{
    FrameRenderSettings settings {};
    settings.use_sensor_size = state.use_sensor_size;
    settings.sensor_preset_index = state.sensor_preset_index;
    settings.fov_h_deg = state.fov_h_deg;
    settings.fov_v_deg = state.fov_v_deg;
    settings.auto_fov_v = state.auto_fov_v;
    settings.sensor_width_mm = state.sensor_width_mm;
    settings.sensor_height_mm = state.sensor_height_mm;
    settings.focal_length_mm = state.focal_length_mm;
    settings.threshold = state.threshold;
    settings.downsample = state.downsample;
    settings.ray_grid = state.ray_grid;
    settings.max_sources = state.max_sources;
    settings.cluster_radius_px = state.cluster_radius_px;
    settings.aperture_blades = state.aperture_blades;
    settings.aperture_rotation_deg = state.aperture_rotation_deg;
    settings.min_ghost = state.min_ghost;
    settings.flare_gain = state.flare_gain;
    settings.sky_brightness = state.sky_brightness;
    settings.ghost_blur = state.ghost_blur;
    settings.ghost_blur_passes = state.ghost_blur_passes;
    settings.ghost_cleanup_mode = state.ghost_cleanup_mode;
    settings.haze_gain = state.haze_gain;
    settings.haze_radius = state.haze_radius;
    settings.haze_blur_passes = state.haze_blur_passes;
    settings.starburst_gain = state.starburst_gain;
    settings.starburst_scale = state.starburst_scale;
    settings.spectral_samples = state.spectral_samples;
    settings.ghost_normalize = state.ghost_normalize;
    settings.max_area_boost = state.max_area_boost;
    settings.adaptive_sampling_strength = state.adaptive_sampling_strength;
    settings.footprint_radius_bias = state.footprint_radius_bias;
    settings.footprint_clamp = state.footprint_clamp;
    settings.max_adaptive_pair_grid = state.max_adaptive_pair_grid;
    settings.projected_cells_mode = state.projected_cells_mode;
    settings.pupil_jitter_mode = state.pupil_jitter_mode;
    settings.pupil_jitter_seed = state.pupil_jitter_seed;
    settings.cell_coverage_bias = state.cell_coverage_bias;
    settings.cell_edge_inset = state.cell_edge_inset;
    settings.bloom = state.bloom;
    return settings;
}
