#pragma once

#include "parameter_state.h"

#include <string>

inline constexpr int PARAM_INPUT = 0;
inline constexpr int PARAM_LEGACY_LENS_PRESET = 1;
inline constexpr int PARAM_LENS_SECTION_START = 2;
inline constexpr int PARAM_LENS_MANUFACTURER = 3;
inline constexpr int LENS_PARAMS_PER_MANUFACTURER = 3;

inline constexpr int PARAM_ID_LEGACY_LENS_PRESET = 1;
inline constexpr int PARAM_ID_FLARE_GAIN = 2;
inline constexpr int PARAM_ID_THRESHOLD = 3;
inline constexpr int PARAM_ID_RAY_GRID = 4;
inline constexpr int PARAM_ID_DOWNSAMPLE = 5;
inline constexpr int PARAM_ID_VIEW_MODE = 6;
inline constexpr int PARAM_ID_MASK_LAYER = 7;
inline constexpr int PARAM_ID_LENS_MANUFACTURER = 100;
inline constexpr int PARAM_ID_LENS_GROUP_BASE = 1000;
inline constexpr int PARAM_ID_LENS_SECTION_START = 9000;
inline constexpr int PARAM_ID_LENS_SECTION_END = 9001;

int lens_section_start_param();
int lens_section_end_param();

int lens_group_start_param(int manufacturer_index);
int lens_popup_param(int manufacturer_index);
int lens_group_end_param(int manufacturer_index);

int flare_gain_param();
int threshold_param();
int ray_grid_param();
int downsample_param();
int view_mode_param();
int mask_layer_param();
int parameter_count();

int lens_group_start_param_id(int manufacturer_index);
int lens_popup_param_id(int manufacturer_index);
int lens_group_end_param_id(int manufacturer_index);

struct AeUiParameterState
{
    int legacy_lens_preset_index = 1;
    int lens_manufacturer_index = 1;
    int lens_model_index = 1;
    float flare_gain = 1000.0f;
    float threshold = 8.0f;
    int ray_grid = 16;
    int downsample = 8;
    int view_mode_index = 1;
};

const char* default_builtin_lens_id();
int default_legacy_lens_popup_index();
int default_lens_manufacturer_popup_index();
int default_lens_model_popup_index();

std::string build_lens_preset_popup_string();
std::string build_lens_manufacturer_popup_string();
std::string build_lens_popup_string_for_manufacturer(int manufacturer_index);
std::string build_output_view_popup_string();

int lens_popup_index_for_builtin(const char* builtin_id);
int lens_manufacturer_popup_index_for_builtin(const char* builtin_id);
int lens_model_popup_index_for_builtin(const char* builtin_id);
bool legacy_lens_selection_from_popup(int popup_index, AeLensSelection& out_selection);
bool lens_selection_from_popup(int manufacturer_popup_index, int lens_popup_index, AeLensSelection& out_selection);

int output_view_popup_count();
int output_view_popup_index(AeOutputView view);
bool output_view_from_popup(int popup_index, AeOutputView& out_view);

bool apply_ui_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state);
