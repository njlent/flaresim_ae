#pragma once

#include "parameter_state.h"

#include <string>

inline constexpr int PARAM_INPUT = 0;
inline constexpr int PARAM_LEGACY_LENS_PRESET = 1;
inline constexpr int PARAM_LENS_SECTION_START = 2;
inline constexpr int PARAM_LENS_MANUFACTURER = 3;
inline constexpr int LENS_PARAMS_PER_MANUFACTURER = 3;

inline constexpr int PARAM_ID_LEGACY_LENS_PRESET = 1;
inline constexpr int PARAM_ID_LENS_MANUFACTURER = 100;
inline constexpr int PARAM_ID_LENS_GROUP_BASE = 1000;
inline constexpr int PARAM_ID_LENS_SECTION_START = 9000;
inline constexpr int PARAM_ID_LENS_SECTION_END = 9001;
inline constexpr int PARAM_ID_CAMERA_SECTION_START = 9002;
inline constexpr int PARAM_ID_CAMERA_SECTION_END = 9003;
inline constexpr int PARAM_ID_APERTURE_SECTION_START = 9004;
inline constexpr int PARAM_ID_APERTURE_SECTION_END = 9005;
inline constexpr int PARAM_ID_FLARE_SECTION_START = 9006;
inline constexpr int PARAM_ID_FLARE_SECTION_END = 9007;
inline constexpr int PARAM_ID_POST_SECTION_START = 9008;
inline constexpr int PARAM_ID_POST_SECTION_END = 9009;
inline constexpr int PARAM_ID_USE_SENSOR_SIZE = 2;
inline constexpr int PARAM_ID_SENSOR_PRESET = 3;
inline constexpr int PARAM_ID_FOV_H = 4;
inline constexpr int PARAM_ID_AUTO_FOV_V = 5;
inline constexpr int PARAM_ID_FOV_V = 6;
inline constexpr int PARAM_ID_SENSOR_WIDTH = 7;
inline constexpr int PARAM_ID_SENSOR_HEIGHT = 8;
inline constexpr int PARAM_ID_FOCAL_LENGTH = 9;
inline constexpr int PARAM_ID_APERTURE_BLADES = 10;
inline constexpr int PARAM_ID_APERTURE_ROTATION = 11;
inline constexpr int PARAM_ID_FLARE_GAIN = 12;
inline constexpr int PARAM_ID_THRESHOLD = 13;
inline constexpr int PARAM_ID_RAY_GRID = 14;
inline constexpr int PARAM_ID_DOWNSAMPLE = 15;
inline constexpr int PARAM_ID_MAX_SOURCES = 16;
inline constexpr int PARAM_ID_GHOST_BLUR = 17;
inline constexpr int PARAM_ID_GHOST_BLUR_PASSES = 18;
inline constexpr int PARAM_ID_HAZE_GAIN = 19;
inline constexpr int PARAM_ID_HAZE_RADIUS = 20;
inline constexpr int PARAM_ID_HAZE_BLUR_PASSES = 21;
inline constexpr int PARAM_ID_STARBURST_GAIN = 22;
inline constexpr int PARAM_ID_STARBURST_SCALE = 23;
inline constexpr int PARAM_ID_SPECTRAL_SAMPLES = 24;
inline constexpr int PARAM_ID_VIEW_MODE = 25;
inline constexpr int PARAM_ID_MASK_LAYER = 26;
inline constexpr int PARAM_ID_GHOST_CLEANUP_MODE = 27;

struct AeUiParameterState
{
    int legacy_lens_preset_index = 1;
    int lens_manufacturer_index = 1;
    int lens_model_index = 1;
    bool use_sensor_size = false;
    int sensor_preset_index = 1;
    float fov_h_deg = 40.0f;
    bool auto_fov_v = true;
    float fov_v_deg = 24.0f;
    float sensor_width_mm = 36.0f;
    float sensor_height_mm = 24.0f;
    float focal_length_mm = 50.0f;
    int aperture_blades = 0;
    float aperture_rotation_deg = 0.0f;
    float flare_gain = 1000.0f;
    float threshold = 8.0f;
    int ray_grid = 16;
    int downsample = 8;
    int max_sources = 64;
    float ghost_blur = 0.003f;
    int ghost_blur_passes = 3;
    int ghost_cleanup_mode_index = 1;
    float haze_gain = 0.0f;
    float haze_radius = 0.15f;
    int haze_blur_passes = 3;
    float starburst_gain = 0.0f;
    float starburst_scale = 0.15f;
    int spectral_samples_index = 1;
    int view_mode_index = 1;
};

int lens_section_start_param();
int lens_section_end_param();
int lens_group_start_param(int manufacturer_index);
int lens_popup_param(int manufacturer_index);
int lens_group_end_param(int manufacturer_index);

int camera_section_start_param();
int use_sensor_size_param();
int sensor_preset_param();
int fov_h_param();
int auto_fov_v_param();
int fov_v_param();
int sensor_width_param();
int sensor_height_param();
int focal_length_param();
int camera_section_end_param();

int aperture_section_start_param();
int aperture_blades_param();
int aperture_rotation_param();
int aperture_section_end_param();

int flare_section_start_param();
int flare_gain_param();
int threshold_param();
int ray_grid_param();
int downsample_param();
int max_sources_param();
int post_section_start_param();
int ghost_blur_param();
int ghost_blur_passes_param();
int haze_gain_param();
int haze_radius_param();
int haze_blur_passes_param();
int starburst_gain_param();
int starburst_scale_param();
int spectral_samples_param();
int ghost_cleanup_mode_param();
int post_section_end_param();
int flare_section_end_param();
int view_mode_param();
int mask_layer_param();
int parameter_count();

int lens_group_start_param_id(int manufacturer_index);
int lens_popup_param_id(int manufacturer_index);
int lens_group_end_param_id(int manufacturer_index);

const char* default_builtin_lens_id();
int default_legacy_lens_popup_index();
int default_lens_manufacturer_popup_index();
int default_lens_model_popup_index();

std::string build_lens_preset_popup_string();
std::string build_lens_manufacturer_popup_string();
std::string build_lens_popup_string_for_manufacturer(int manufacturer_index);
std::string build_sensor_preset_popup_string();
std::string build_spectral_samples_popup_string();
std::string build_ghost_cleanup_mode_popup_string();
std::string build_output_view_popup_string();

int lens_popup_index_for_builtin(const char* builtin_id);
int lens_manufacturer_popup_index_for_builtin(const char* builtin_id);
int lens_model_popup_index_for_builtin(const char* builtin_id);
bool legacy_lens_selection_from_popup(int popup_index, AeLensSelection& out_selection);
bool lens_selection_from_popup(int manufacturer_popup_index, int lens_popup_index, AeLensSelection& out_selection);

int sensor_preset_popup_count();
bool sensor_preset_dimensions_from_popup(int popup_index, float& out_width_mm, float& out_height_mm);

int spectral_samples_popup_count();
int spectral_samples_popup_index(int spectral_samples);
bool spectral_samples_from_popup(int popup_index, int& out_spectral_samples);

int ghost_cleanup_mode_popup_count();
int ghost_cleanup_mode_popup_index(GhostCleanupMode mode);
bool ghost_cleanup_mode_from_popup(int popup_index, GhostCleanupMode& out_mode);

int output_view_popup_count();
int output_view_popup_index(AeOutputView view);
bool output_view_from_popup(int popup_index, AeOutputView& out_view);

bool apply_ui_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state);
