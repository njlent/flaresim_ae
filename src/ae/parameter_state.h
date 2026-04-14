#pragma once

#include "render_frame.h"

enum class AeLensSourceKind
{
    Builtin,
    External,
};

enum class AeOutputView
{
    Composite,
    FlareOnly,
    BloomOnly,
    Sources,
    Diagnostics,
};

struct AeLensSelection
{
    AeLensSourceKind source = AeLensSourceKind::Builtin;
    const char* builtin_id = "double-gauss";
    const char* external_path = nullptr;
};

struct AeParameterState
{
    AeLensSelection lens {};
    AeOutputView view = AeOutputView::Composite;
    bool use_sensor_size = false;
    int sensor_preset_index = 1;
    float fov_h_deg = 60.0f;
    float fov_v_deg = 24.0f;
    bool auto_fov_v = true;
    float sensor_width_mm = 36.0f;
    float sensor_height_mm = 24.0f;
    float focal_length_mm = 50.0f;
    float threshold = 8.0f;
    int downsample = 2;
    int ray_grid = 128;
    int max_sources = 256;
    int cluster_radius_px = 0;
    int aperture_blades = 0;
    float aperture_rotation_deg = 0.0f;
    float flare_gain = 8000.0f;
    float sky_brightness = 1.0f;
    float ghost_blur = 0.003f;
    int ghost_blur_passes = 3;
    GhostCleanupMode ghost_cleanup_mode = GhostCleanupMode::SharpAdaptive;
    float haze_gain = 0.0f;
    float haze_radius = 0.15f;
    int haze_blur_passes = 3;
    float starburst_gain = 0.0f;
    float starburst_scale = 0.15f;
    int spectral_samples = 3;
    float min_ghost = 1e-7f;
    bool ghost_normalize = true;
    float max_area_boost = 100.0f;
    float adaptive_sampling_strength = 0.0f;
    float footprint_radius_bias = 1.0f;
    float footprint_clamp = 1.15f;
    int max_adaptive_pair_grid = 0;
    ProjectedCellsMode projected_cells_mode = ProjectedCellsMode::Off;
    PupilJitterMode pupil_jitter_mode = PupilJitterMode::Off;
    int pupil_jitter_seed = 0;
    float cell_coverage_bias = 1.0f;
    float cell_edge_inset = 0.1f;
    BloomConfig bloom {};
};

FrameRenderSettings build_frame_render_settings(const AeParameterState& state);
