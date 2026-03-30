#include "parameter_state.h"

FrameRenderSettings build_frame_render_settings(const AeParameterState& state)
{
    FrameRenderSettings settings {};
    settings.fov_h_deg = state.fov_h_deg;
    settings.threshold = state.threshold;
    settings.downsample = state.downsample;
    settings.ray_grid = state.ray_grid;
    settings.min_ghost = state.min_ghost;
    settings.flare_gain = state.flare_gain;
    settings.ghost_normalize = state.ghost_normalize;
    settings.max_area_boost = state.max_area_boost;
    settings.bloom = state.bloom;
    return settings;
}
