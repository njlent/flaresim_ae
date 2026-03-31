#include "param_schema.h"

#include "builtin_lenses.h"

#include <array>
#include <string_view>

namespace {

struct OutputViewDescriptor
{
    AeOutputView view;
    std::string_view label;
};

struct SensorPresetDescriptor
{
    const char* label;
    float width_mm;
    float height_mm;
};

constexpr OutputViewDescriptor kOutputViews[] = {
    {AeOutputView::Composite, "Composite"},
    {AeOutputView::FlareOnly, "Flare Only"},
    {AeOutputView::BloomOnly, "Bloom Only"},
    {AeOutputView::Sources, "Sources"},
    {AeOutputView::Diagnostics, "Diagnostics"},
};

constexpr SensorPresetDescriptor kSensorPresets[] = {
    {"Custom", 0.0f, 0.0f},
    {"Full Frame", 36.0f, 24.0f},
    {"Super 35", 24.89f, 18.66f},
    {"APS-C Canon", 22.3f, 14.9f},
    {"APS-C Nikon/Sony", 23.5f, 15.6f},
    {"Micro Four Thirds", 17.3f, 13.0f},
};

constexpr int kSpectralSamples[] = {3, 5, 7, 9, 11};

struct GhostCleanupModeDescriptor
{
    GhostCleanupMode mode;
    std::string_view label;
};

constexpr GhostCleanupModeDescriptor kGhostCleanupModes[] = {
    {GhostCleanupMode::LegacyBlur, "Legacy Blur"},
    {GhostCleanupMode::SharpAdaptive, "Sharp Adaptive"},
    {GhostCleanupMode::SharpAdaptivePlusBlur, "Sharp + Blur"},
};

std::string build_popup_string_from_labels(const char* const* labels, std::size_t count)
{
    std::string popup;
    for (std::size_t i = 0; i < count; ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += labels[i];
    }
    return popup;
}

const BuiltinLensManufacturerDescriptor* legacy_lens_manufacturer()
{
    return builtin_lens_manufacturer(0);
}

bool is_default_grouped_lens_selection(const AeUiParameterState& ui_state)
{
    return ui_state.lens_manufacturer_index == default_lens_manufacturer_popup_index() &&
           ui_state.lens_model_index == default_lens_model_popup_index();
}

} // namespace

int lens_group_start_param(int manufacturer_index)
{
    return PARAM_LENS_MANUFACTURER + 1 + (manufacturer_index * LENS_PARAMS_PER_MANUFACTURER);
}

int lens_section_start_param()
{
    return PARAM_LENS_SECTION_START;
}

int lens_popup_param(int manufacturer_index)
{
    return lens_group_start_param(manufacturer_index) + 1;
}

int lens_group_end_param(int manufacturer_index)
{
    return lens_group_start_param(manufacturer_index) + 2;
}

int lens_section_end_param()
{
    return PARAM_LENS_MANUFACTURER + 1 +
           static_cast<int>(builtin_lens_manufacturer_count()) * LENS_PARAMS_PER_MANUFACTURER;
}

int camera_section_start_param() { return lens_section_end_param() + 1; }
int use_sensor_size_param() { return camera_section_start_param() + 1; }
int sensor_preset_param() { return use_sensor_size_param() + 1; }
int fov_h_param() { return sensor_preset_param() + 1; }
int auto_fov_v_param() { return fov_h_param() + 1; }
int fov_v_param() { return auto_fov_v_param() + 1; }
int sensor_width_param() { return fov_v_param() + 1; }
int sensor_height_param() { return sensor_width_param() + 1; }
int focal_length_param() { return sensor_height_param() + 1; }
int camera_section_end_param() { return focal_length_param() + 1; }

int aperture_section_start_param() { return camera_section_end_param() + 1; }
int aperture_blades_param() { return aperture_section_start_param() + 1; }
int aperture_rotation_param() { return aperture_blades_param() + 1; }
int aperture_section_end_param() { return aperture_rotation_param() + 1; }

int flare_section_start_param() { return aperture_section_end_param() + 1; }
int flare_gain_param() { return flare_section_start_param() + 1; }
int sky_brightness_param() { return flare_gain_param() + 1; }
int threshold_param() { return sky_brightness_param() + 1; }
int ray_grid_param() { return threshold_param() + 1; }
int downsample_param() { return ray_grid_param() + 1; }
int max_sources_param() { return downsample_param() + 1; }
int flare_section_end_param() { return max_sources_param() + 1; }
int post_section_start_param() { return flare_section_end_param() + 1; }
int ghost_blur_param() { return post_section_start_param() + 1; }
int ghost_blur_passes_param() { return ghost_blur_param() + 1; }
int haze_gain_param() { return ghost_blur_passes_param() + 1; }
int haze_radius_param() { return haze_gain_param() + 1; }
int haze_blur_passes_param() { return haze_radius_param() + 1; }
int starburst_gain_param() { return haze_blur_passes_param() + 1; }
int starburst_scale_param() { return starburst_gain_param() + 1; }
int spectral_samples_param() { return starburst_scale_param() + 1; }
int ghost_cleanup_mode_param() { return spectral_samples_param() + 1; }
int post_section_end_param() { return ghost_cleanup_mode_param() + 1; }
int view_mode_param() { return post_section_end_param() + 1; }
int mask_layer_param() { return view_mode_param() + 1; }
int parameter_count() { return mask_layer_param() + 1; }

int lens_group_start_param_id(int manufacturer_index)
{
    return PARAM_ID_LENS_GROUP_BASE + (manufacturer_index * LENS_PARAMS_PER_MANUFACTURER);
}

int lens_popup_param_id(int manufacturer_index)
{
    return lens_group_start_param_id(manufacturer_index) + 1;
}

int lens_group_end_param_id(int manufacturer_index)
{
    return lens_group_start_param_id(manufacturer_index) + 2;
}

const char* default_builtin_lens_id()
{
    return "double-gauss";
}

int default_legacy_lens_popup_index()
{
    return lens_popup_index_for_builtin(default_builtin_lens_id());
}

int default_lens_manufacturer_popup_index()
{
    return lens_manufacturer_popup_index_for_builtin(default_builtin_lens_id());
}

int default_lens_model_popup_index()
{
    return lens_model_popup_index_for_builtin(default_builtin_lens_id());
}

std::string build_lens_preset_popup_string()
{
    const auto* manufacturer = legacy_lens_manufacturer();
    if (!manufacturer) {
        return {};
    }

    const auto* lenses = builtin_lenses_for_manufacturer(0);
    std::string popup;
    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += lenses[i].label;
    }
    return popup;
}

std::string build_lens_manufacturer_popup_string()
{
    std::string popup;
    for (std::size_t i = 0; i < builtin_lens_manufacturer_count(); ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += builtin_lens_manufacturers()[i].label;
    }
    return popup;
}

std::string build_lens_popup_string_for_manufacturer(int manufacturer_index)
{
    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(manufacturer_index));
    if (!manufacturer || !lenses) {
        return {};
    }

    std::string popup;
    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += lenses[i].label;
    }
    return popup;
}

std::string build_sensor_preset_popup_string()
{
    const char* labels[std::size(kSensorPresets)] {};
    for (std::size_t i = 0; i < std::size(kSensorPresets); ++i) {
        labels[i] = kSensorPresets[i].label;
    }
    return build_popup_string_from_labels(labels, std::size(kSensorPresets));
}

std::string build_spectral_samples_popup_string()
{
    std::array<const char*, std::size(kSpectralSamples)> labels = {
        "3", "5", "7", "9", "11",
    };
    return build_popup_string_from_labels(labels.data(), labels.size());
}

std::string build_ghost_cleanup_mode_popup_string()
{
    const char* labels[std::size(kGhostCleanupModes)] {};
    for (std::size_t i = 0; i < std::size(kGhostCleanupModes); ++i) {
        labels[i] = kGhostCleanupModes[i].label.data();
    }
    return build_popup_string_from_labels(labels, std::size(kGhostCleanupModes));
}

std::string build_output_view_popup_string()
{
    const char* labels[std::size(kOutputViews)] {};
    for (std::size_t i = 0; i < std::size(kOutputViews); ++i) {
        labels[i] = kOutputViews[i].label.data();
    }
    return build_popup_string_from_labels(labels, std::size(kOutputViews));
}

int lens_popup_index_for_builtin(const char* builtin_id)
{
    if (!builtin_id) {
        return 0;
    }

    const auto* manufacturer = legacy_lens_manufacturer();
    const auto* lenses = builtin_lenses_for_manufacturer(0);
    if (!manufacturer || !lenses) {
        return 0;
    }

    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (std::string_view(lenses[i].id) == builtin_id) {
            return i + 1;
        }
    }
    return 0;
}

int lens_manufacturer_popup_index_for_builtin(const char* builtin_id)
{
    const auto* lens = find_builtin_lens(builtin_id);
    return lens ? (lens->manufacturer_index + 1) : 0;
}

int lens_model_popup_index_for_builtin(const char* builtin_id)
{
    const auto* lens = find_builtin_lens(builtin_id);
    if (!lens) {
        return 0;
    }

    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(lens->manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(lens->manufacturer_index));
    if (!manufacturer || !lenses) {
        return 0;
    }

    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (std::string_view(lenses[i].id) == builtin_id) {
            return i + 1;
        }
    }
    return 0;
}

bool legacy_lens_selection_from_popup(int popup_index, AeLensSelection& out_selection)
{
    const auto* manufacturer = legacy_lens_manufacturer();
    if (!manufacturer || popup_index < 1 || popup_index > manufacturer->lens_count) {
        return false;
    }

    const auto* lenses = builtin_lenses_for_manufacturer(0);
    const BuiltinLensDescriptor& lens = lenses[popup_index - 1];
    out_selection.source = AeLensSourceKind::Builtin;
    out_selection.builtin_id = lens.id;
    out_selection.external_path = nullptr;
    return true;
}

bool lens_selection_from_popup(int manufacturer_popup_index, int lens_popup_index, AeLensSelection& out_selection)
{
    if (manufacturer_popup_index < 1 ||
        manufacturer_popup_index > static_cast<int>(builtin_lens_manufacturer_count())) {
        return false;
    }

    const int manufacturer_index = manufacturer_popup_index - 1;
    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(manufacturer_index));
    if (!manufacturer || !lenses || lens_popup_index < 1 || lens_popup_index > manufacturer->lens_count) {
        return false;
    }

    const BuiltinLensDescriptor& lens = lenses[lens_popup_index - 1];
    out_selection.source = AeLensSourceKind::Builtin;
    out_selection.builtin_id = lens.id;
    out_selection.external_path = nullptr;
    return true;
}

int sensor_preset_popup_count()
{
    return static_cast<int>(std::size(kSensorPresets));
}

bool sensor_preset_dimensions_from_popup(int popup_index, float& out_width_mm, float& out_height_mm)
{
    if (popup_index < 1 || popup_index > static_cast<int>(std::size(kSensorPresets))) {
        return false;
    }

    out_width_mm = kSensorPresets[popup_index - 1].width_mm;
    out_height_mm = kSensorPresets[popup_index - 1].height_mm;
    return true;
}

int spectral_samples_popup_count()
{
    return static_cast<int>(std::size(kSpectralSamples));
}

int spectral_samples_popup_index(int spectral_samples)
{
    for (std::size_t i = 0; i < std::size(kSpectralSamples); ++i) {
        if (kSpectralSamples[i] == spectral_samples) {
            return static_cast<int>(i) + 1;
        }
    }
    return 1;
}

bool spectral_samples_from_popup(int popup_index, int& out_spectral_samples)
{
    if (popup_index < 1 || popup_index > static_cast<int>(std::size(kSpectralSamples))) {
        return false;
    }

    out_spectral_samples = kSpectralSamples[popup_index - 1];
    return true;
}

int ghost_cleanup_mode_popup_count()
{
    return static_cast<int>(std::size(kGhostCleanupModes));
}

int ghost_cleanup_mode_popup_index(GhostCleanupMode mode)
{
    for (std::size_t i = 0; i < std::size(kGhostCleanupModes); ++i) {
        if (kGhostCleanupModes[i].mode == mode) {
            return static_cast<int>(i) + 1;
        }
    }
    return 1;
}

bool ghost_cleanup_mode_from_popup(int popup_index, GhostCleanupMode& out_mode)
{
    if (popup_index < 1 || popup_index > static_cast<int>(std::size(kGhostCleanupModes))) {
        return false;
    }

    out_mode = kGhostCleanupModes[popup_index - 1].mode;
    return true;
}

int output_view_popup_index(AeOutputView view)
{
    for (std::size_t i = 0; i < std::size(kOutputViews); ++i) {
        if (kOutputViews[i].view == view) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

int output_view_popup_count()
{
    return static_cast<int>(std::size(kOutputViews));
}

bool output_view_from_popup(int popup_index, AeOutputView& out_view)
{
    if (popup_index < 1 || popup_index > static_cast<int>(std::size(kOutputViews))) {
        return false;
    }

    out_view = kOutputViews[popup_index - 1].view;
    return true;
}

bool apply_ui_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state)
{
    if (ui_state.ray_grid < 1 ||
        ui_state.downsample < 1 ||
        ui_state.max_sources < 0 ||
        ui_state.ghost_blur_passes < 0 ||
        ui_state.haze_blur_passes < 0 ||
        ui_state.aperture_blades < 0 ||
        ui_state.sensor_width_mm < 0.0f ||
        ui_state.sensor_height_mm < 0.0f ||
        ui_state.focal_length_mm <= 0.0f) {
        return false;
    }

    AeLensSelection grouped_lens {};
    AeLensSelection legacy_lens {};
    const bool selected_grouped_lens =
        lens_selection_from_popup(ui_state.lens_manufacturer_index, ui_state.lens_model_index, grouped_lens);
    const bool selected_legacy_lens =
        legacy_lens_selection_from_popup(ui_state.legacy_lens_preset_index, legacy_lens);

    if (!selected_grouped_lens && !selected_legacy_lens) {
        return false;
    }

    if (selected_legacy_lens &&
        ui_state.legacy_lens_preset_index != default_legacy_lens_popup_index() &&
        is_default_grouped_lens_selection(ui_state)) {
        out_state.lens = legacy_lens;
    } else if (selected_grouped_lens) {
        out_state.lens = grouped_lens;
    } else {
        return false;
    }

    AeOutputView view {};
    if (!output_view_from_popup(ui_state.view_mode_index, view)) {
        return false;
    }

    int spectral_samples = 3;
    if (!spectral_samples_from_popup(ui_state.spectral_samples_index, spectral_samples)) {
        return false;
    }

    GhostCleanupMode ghost_cleanup_mode = GhostCleanupMode::LegacyBlur;
    if (!ghost_cleanup_mode_from_popup(ui_state.ghost_cleanup_mode_index, ghost_cleanup_mode)) {
        return false;
    }

    out_state.view = view;
    out_state.use_sensor_size = ui_state.use_sensor_size;
    out_state.sensor_preset_index = ui_state.sensor_preset_index;
    out_state.fov_h_deg = ui_state.fov_h_deg;
    out_state.fov_v_deg = ui_state.fov_v_deg;
    out_state.auto_fov_v = ui_state.auto_fov_v;
    out_state.sensor_width_mm = ui_state.sensor_width_mm;
    out_state.sensor_height_mm = ui_state.sensor_height_mm;
    out_state.focal_length_mm = ui_state.focal_length_mm;
    out_state.aperture_blades = ui_state.aperture_blades;
    out_state.aperture_rotation_deg = ui_state.aperture_rotation_deg;
    out_state.flare_gain = ui_state.flare_gain;
    out_state.sky_brightness = ui_state.sky_brightness;
    out_state.threshold = ui_state.threshold;
    out_state.ray_grid = ui_state.ray_grid;
    out_state.downsample = ui_state.downsample;
    out_state.max_sources = ui_state.max_sources;
    out_state.ghost_blur = ui_state.ghost_blur;
    out_state.ghost_blur_passes = ui_state.ghost_blur_passes;
    out_state.ghost_cleanup_mode = ghost_cleanup_mode;
    out_state.haze_gain = ui_state.haze_gain;
    out_state.haze_radius = ui_state.haze_radius;
    out_state.haze_blur_passes = ui_state.haze_blur_passes;
    out_state.starburst_gain = ui_state.starburst_gain;
    out_state.starburst_scale = ui_state.starburst_scale;
    out_state.spectral_samples = spectral_samples;

    float preset_width = 0.0f;
    float preset_height = 0.0f;
    if (sensor_preset_dimensions_from_popup(ui_state.sensor_preset_index, preset_width, preset_height) &&
        ui_state.sensor_preset_index > 1) {
        out_state.sensor_width_mm = preset_width;
        out_state.sensor_height_mm = preset_height;
    }

    return true;
}
