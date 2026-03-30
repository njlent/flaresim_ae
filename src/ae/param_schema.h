#pragma once

#include "parameter_state.h"

#include <string>

enum AeParamIndex
{
    PARAM_INPUT = 0,
    PARAM_LENS_PRESET,
    PARAM_FLARE_GAIN,
    PARAM_THRESHOLD,
    PARAM_RAY_GRID,
    PARAM_MAX_SOURCES,
    PARAM_DOWNSAMPLE,
    PARAM_VIEW_MODE,
    PARAM_MASK_LAYER,
    PARAM_COUNT
};

struct AeUiParameterState
{
    int lens_preset_index = 1;
    float flare_gain = 1000.0f;
    float threshold = 8.0f;
    int ray_grid = 16;
    int max_sources = 64;
    int downsample = 8;
    int view_mode_index = 1;
};

std::string build_lens_preset_popup_string();
std::string build_output_view_popup_string();

int lens_popup_index_for_builtin(const char* builtin_id);
bool lens_selection_from_popup(int popup_index, AeLensSelection& out_selection);

int output_view_popup_count();
int output_view_popup_index(AeOutputView view);
bool output_view_from_popup(int popup_index, AeOutputView& out_view);

bool apply_ui_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state);
