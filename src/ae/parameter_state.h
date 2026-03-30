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
    float fov_h_deg = 60.0f;
    float threshold = 8.0f;
    int downsample = 8;
    int ray_grid = 16;
    int max_sources = 64;
    float flare_gain = 1000.0f;
    float min_ghost = 1e-7f;
    bool ghost_normalize = true;
    float max_area_boost = 100.0f;
    BloomConfig bloom {};
};

FrameRenderSettings build_frame_render_settings(const AeParameterState& state);
