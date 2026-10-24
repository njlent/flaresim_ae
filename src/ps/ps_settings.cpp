#include "ps_settings.h"

#include "builtin_lenses.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string_view>

namespace {

constexpr const char* kSettingsHeader = "flaresim-ps";
constexpr int kSettingsVersion = 1;
constexpr float kFloatMax = 1.0e9f;
constexpr float kIntMax = 1000000.0f;

using S = AeUiParameterState;

constexpr PsParamDesc make_float(const char* key,
                                 const char* label,
                                 const char* section,
                                 float S::*member,
                                 float slider_min,
                                 float slider_max,
                                 float hard_min,
                                 float hard_max,
                                 int decimals)
{
    return {key, label, section, PsParamKind::Float, member, nullptr, nullptr,
            slider_min, slider_max, hard_min, hard_max, decimals, nullptr};
}

constexpr PsParamDesc make_int(const char* key,
                               const char* label,
                               const char* section,
                               int S::*member,
                               float slider_min,
                               float slider_max,
                               float hard_min = 0.0f,
                               float hard_max = kIntMax)
{
    return {key, label, section, PsParamKind::Int, nullptr, member, nullptr,
            slider_min, slider_max, hard_min, hard_max, 0, nullptr};
}

constexpr PsParamDesc make_bool(const char* key, const char* label, const char* section, bool S::*member)
{
    return {key, label, section, PsParamKind::Bool, nullptr, nullptr, member,
            0.0f, 1.0f, 0.0f, 1.0f, 0, nullptr};
}

constexpr PsParamDesc make_choice(const char* key,
                                  const char* label,
                                  const char* section,
                                  int S::*member,
                                  std::string (*labels)())
{
    return {key, label, section, PsParamKind::Choice, nullptr, member, nullptr,
            1.0f, 1.0f, 1.0f, 1.0f, 0, labels};
}

constexpr const char* kCamera = "Camera";
constexpr const char* kAperture = "Aperture";
constexpr const char* kFlare = "Flare Settings";
constexpr const char* kPreview = "Preview Quality";
constexpr const char* kPost = "Post-processing";
constexpr const char* kAdvanced = "Advanced Ghosts";
constexpr const char* kOutput = "Output";

// Ranges mirror src/ae/plugin_params.cpp: slider range for the trackbar,
// hard range for typed values.
const PsParamDesc kParams[] = {
    make_bool("use_sensor_size", "Use Sensor Size", kCamera, &S::use_sensor_size),
    make_choice("sensor_preset", "Sensor Preset", kCamera, &S::sensor_preset_index, build_sensor_preset_popup_string),
    make_float("fov_h", "FOV H (deg)", kCamera, &S::fov_h_deg, 1.0f, 180.0f, 0.001f, kFloatMax, 2),
    make_bool("auto_fov_v", "Auto FOV V", kCamera, &S::auto_fov_v),
    make_float("fov_v", "FOV V (deg)", kCamera, &S::fov_v_deg, 1.0f, 180.0f, 0.001f, kFloatMax, 2),
    make_float("sensor_width", "Sensor Width (mm)", kCamera, &S::sensor_width_mm, 1.0f, 100.0f, 0.001f, kFloatMax, 2),
    make_float("sensor_height", "Sensor Height (mm)", kCamera, &S::sensor_height_mm, 1.0f, 100.0f, 0.001f, kFloatMax, 2),
    make_float("focal_length", "Focal Length (mm)", kCamera, &S::focal_length_mm, 1.0f, 200.0f, 0.001f, kFloatMax, 2),
    make_float("anamorphic_squeeze", "Anamorphic Squeeze", kCamera, &S::anamorphic_squeeze, 1.0f, 4.0f, 0.1f, kFloatMax, 2),

    make_int("aperture_blades", "Aperture Blades", kAperture, &S::aperture_blades, 0.0f, 16.0f),
    make_float("aperture_rotation", "Aperture Rotation", kAperture, &S::aperture_rotation_deg, -180.0f, 180.0f, -kFloatMax, kFloatMax, 2),

    make_choice("adaptive_sampling", "Adaptive Sampling", kFlare, &S::projected_cells_mode_index, build_projected_cells_mode_popup_string),
    make_float("adaptive_quality", "Adaptive Quality", kFlare, &S::adaptive_quality, 0.25f, 2.0f, 0.25f, kFloatMax, 2),
    make_float("flare_gain", "Flare Gain", kFlare, &S::flare_gain, 0.0f, 5000.0f, 0.0f, kFloatMax, 1),
    make_float("sky_brightness", "Sky Brightness", kFlare, &S::sky_brightness, 0.0f, 4.0f, 0.0f, kFloatMax, 3),
    make_float("threshold", "Threshold", kFlare, &S::threshold, 0.0f, 64.0f, 0.0f, kFloatMax, 4),
    make_float("source_cap", "Source Cap", kFlare, &S::source_cap, 0.0f, 64.0f, 0.0f, kFloatMax, 4),
    make_bool("manual_source", "Manual Source", kFlare, &S::manual_source_enabled),
    make_float("source_x", "Source X", kFlare, &S::manual_source_x, -1.0f, 2.0f, -kFloatMax, kFloatMax, 3),
    make_float("source_y", "Source Y", kFlare, &S::manual_source_y, -1.0f, 2.0f, -kFloatMax, kFloatMax, 3),
    make_float("source_intensity", "Source Intensity", kFlare, &S::manual_source_intensity, 0.0f, 256.0f, 0.0f, kFloatMax, 3),
    make_float("source_red", "Source Red", kFlare, &S::manual_source_r, 0.0f, 4.0f, 0.0f, kFloatMax, 3),
    make_float("source_green", "Source Green", kFlare, &S::manual_source_g, 0.0f, 4.0f, 0.0f, kFloatMax, 3),
    make_float("source_blue", "Source Blue", kFlare, &S::manual_source_b, 0.0f, 4.0f, 0.0f, kFloatMax, 3),
    make_int("ray_grid", "Ray Grid", kFlare, &S::ray_grid, 1.0f, 512.0f, 1.0f),
    make_int("downsample", "Downsample", kFlare, &S::downsample, 1.0f, 12.0f, 1.0f),
    make_int("max_sources", "Max Sources", kFlare, &S::max_sources, 0.0f, 512.0f),
    make_int("cluster_radius", "Cluster Radius", kFlare, &S::cluster_radius_px, 0.0f, 256.0f),

    make_bool("preview_mode", "Preview Mode", kPreview, &S::preview_mode),
    make_int("preview_ray_grid", "Preview Ray Grid", kPreview, &S::preview_ray_grid, 1.0f, 128.0f, 1.0f),
    make_int("preview_max_sources", "Preview Max Sources", kPreview, &S::preview_max_sources, 0.0f, 512.0f),
    make_int("preview_downsample", "Preview Downsample", kPreview, &S::preview_downsample, 1.0f, 24.0f, 1.0f),
    make_choice("preview_spectral_samples", "Preview Spectral Samples", kPreview, &S::preview_spectral_samples_index, build_spectral_samples_popup_string),

    make_float("ghost_blur", "Ghost Blur", kPost, &S::ghost_blur, 0.0f, 0.05f, 0.0f, kFloatMax, 4),
    make_int("ghost_blur_passes", "Ghost Blur Passes", kPost, &S::ghost_blur_passes, 0.0f, 8.0f),
    make_float("haze_gain", "Haze Gain", kPost, &S::haze_gain, 0.0f, 10.0f, 0.0f, kFloatMax, 3),
    make_float("haze_radius", "Haze Radius", kPost, &S::haze_radius, 0.0f, 0.5f, 0.0f, kFloatMax, 3),
    make_int("haze_blur_passes", "Haze Blur Passes", kPost, &S::haze_blur_passes, 0.0f, 8.0f),
    make_float("starburst_gain", "Starburst Gain", kPost, &S::starburst_gain, 0.0f, 10.0f, 0.0f, kFloatMax, 3),
    make_float("starburst_scale", "Starburst Scale", kPost, &S::starburst_scale, 0.0f, 0.5f, 0.0f, kFloatMax, 3),
    make_choice("spectral_samples", "Spectral Samples", kPost, &S::spectral_samples_index, build_spectral_samples_popup_string),
    make_choice("spectral_jitter", "Spectral Jitter", kPost, &S::spectral_jitter_mode_index, build_spectral_jitter_mode_popup_string),
    make_int("spectral_seed", "Spectral Seed", kPost, &S::spectral_jitter_seed, 0.0f, 1000000.0f),
    make_choice("ghost_cleanup", "Ghost Cleanup", kPost, &S::ghost_cleanup_mode_index, build_ghost_cleanup_mode_popup_string),

    make_float("adaptive_strength", "Adaptive Strength", kAdvanced, &S::adaptive_sampling_strength, 0.0f, 2.0f, 0.0f, kFloatMax, 2),
    make_float("footprint_bias", "Footprint Bias", kAdvanced, &S::footprint_radius_bias, 0.25f, 2.0f, 0.1f, kFloatMax, 2),
    make_float("footprint_clamp", "Footprint Clamp", kAdvanced, &S::footprint_clamp, 0.5f, 4.0f, 0.1f, kFloatMax, 2),
    make_int("max_pair_grid", "Max Pair Grid", kAdvanced, &S::max_adaptive_pair_grid, 0.0f, 512.0f),
    make_int("pair_start", "Pair Start", kAdvanced, &S::pair_start, 0.0f, 512.0f),
    make_int("pair_count", "Pair Count", kAdvanced, &S::pair_count, 0.0f, 512.0f),
    make_int("surface_start", "Surface Start", kAdvanced, &S::surface_art_start, 0.0f, 512.0f),
    make_int("surface_count", "Surface Count", kAdvanced, &S::surface_art_count, 0.0f, 512.0f),
    make_float("surface_gain", "Surface Gain", kAdvanced, &S::surface_art_gain, 0.0f, 4.0f, 0.0f, kFloatMax, 2),
    make_choice("pupil_jitter", "Pupil Jitter", kAdvanced, &S::pupil_jitter_mode_index, build_pupil_jitter_mode_popup_string),
    make_int("jitter_seed", "Jitter Seed", kAdvanced, &S::pupil_jitter_seed, 0.0f, 1000000.0f),
    make_float("cell_coverage", "Cell Coverage", kAdvanced, &S::cell_coverage_bias, 0.5f, 2.5f, 0.1f, kFloatMax, 2),
    make_float("cell_edge_inset", "Cell Edge Inset", kAdvanced, &S::cell_edge_inset, 0.0f, 0.45f, 0.0f, kFloatMax, 2),

    make_choice("color_space", "Color Space", kOutput, &S::color_space_index, build_color_space_popup_string),
    make_choice("view", "View", kOutput, &S::view_mode_index, build_output_view_popup_string),
};

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool equals_ignore_case(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool parse_double(std::string_view text, double& out_value)
{
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    if (*begin == '+') {
        ++begin;
    }
    const auto result = std::from_chars(begin, end, out_value);
    return result.ec == std::errc() && result.ptr == end && std::isfinite(out_value);
}

std::string format_float(float value)
{
    char buffer[64] {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
    return std::string(buffer, result.ptr);
}

int choice_count(const PsParamDesc& desc)
{
    return desc.choice_labels
               ? static_cast<int>(ps_split_choice_labels(desc.choice_labels()).size())
               : 0;
}

bool parse_choice(const PsParamDesc& desc, std::string_view text, int& out_index)
{
    text = trim(text);
    const std::vector<std::string> labels = ps_split_choice_labels(desc.choice_labels());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (equals_ignore_case(labels[i], text)) {
            out_index = static_cast<int>(i) + 1;
            return true;
        }
    }

    // Fall back to a 1-based popup index so hand-written scripts can use numbers.
    double index = 0.0;
    if (parse_double(text, index)) {
        const int rounded = static_cast<int>(std::lround(index));
        if (rounded >= 1 && rounded <= static_cast<int>(labels.size())) {
            out_index = rounded;
            return true;
        }
    }
    return false;
}

} // namespace

const PsParamDesc* ps_param_table()
{
    return kParams;
}

std::size_t ps_param_count()
{
    return sizeof(kParams) / sizeof(kParams[0]);
}

const PsParamDesc* ps_find_param(const char* key)
{
    if (!key) {
        return nullptr;
    }
    for (const PsParamDesc& desc : kParams) {
        if (std::strcmp(desc.key, key) == 0) {
            return &desc;
        }
    }
    return nullptr;
}

std::vector<std::string> ps_split_choice_labels(const std::string& popup)
{
    std::vector<std::string> labels;
    std::size_t start = 0;
    while (start <= popup.size()) {
        const std::size_t bar = popup.find('|', start);
        const std::size_t end = bar == std::string::npos ? popup.size() : bar;
        if (end > start) {
            labels.emplace_back(popup.substr(start, end - start));
        }
        if (bar == std::string::npos) {
            break;
        }
        start = bar + 1;
    }
    return labels;
}

double ps_get_param_value(const PsParamDesc& desc, const AeUiParameterState& state)
{
    switch (desc.kind) {
        case PsParamKind::Float:
            return state.*desc.float_member;
        case PsParamKind::Int:
        case PsParamKind::Choice:
            return state.*desc.int_member;
        case PsParamKind::Bool:
            return state.*desc.bool_member ? 1.0 : 0.0;
    }
    return 0.0;
}

void ps_set_param_value(const PsParamDesc& desc, AeUiParameterState& state, double value)
{
    if (!std::isfinite(value)) {
        return;
    }

    switch (desc.kind) {
        case PsParamKind::Float:
            state.*desc.float_member =
                static_cast<float>(std::clamp(value, static_cast<double>(desc.hard_min), static_cast<double>(desc.hard_max)));
            return;
        case PsParamKind::Int: {
            const double clamped =
                std::clamp(value, static_cast<double>(desc.hard_min), static_cast<double>(desc.hard_max));
            state.*desc.int_member = static_cast<int>(std::lround(clamped));
            return;
        }
        case PsParamKind::Choice: {
            const int count = std::max(choice_count(desc), 1);
            state.*desc.int_member = std::clamp(static_cast<int>(std::lround(value)), 1, count);
            return;
        }
        case PsParamKind::Bool:
            state.*desc.bool_member = value >= 0.5;
            return;
    }
}

AeUiParameterState ps_default_ui_state()
{
    AeUiParameterState state {};
    state.legacy_lens_preset_index = default_legacy_lens_popup_index();
    state.lens_manufacturer_index = default_lens_manufacturer_popup_index();
    state.lens_model_index = default_lens_model_popup_index();
    state.pupil_jitter_auto_seed = false;
    return state;
}

std::string ps_lens_id(const AeUiParameterState& state)
{
    AeLensSelection selection {};
    if (lens_selection_from_popup(state.lens_manufacturer_index, state.lens_model_index, selection) &&
        selection.builtin_id) {
        return selection.builtin_id;
    }
    return default_builtin_lens_id();
}

bool ps_set_lens_id(AeUiParameterState& state, const std::string& builtin_id)
{
    if (!find_builtin_lens(builtin_id.c_str())) {
        return false;
    }
    const int manufacturer = lens_manufacturer_popup_index_for_builtin(builtin_id.c_str());
    const int model = lens_model_popup_index_for_builtin(builtin_id.c_str());
    if (manufacturer < 1 || model < 1) {
        return false;
    }
    state.lens_manufacturer_index = manufacturer;
    state.lens_model_index = model;
    // The grouped popups are authoritative; keep the legacy popup at its default
    // so apply_ui_parameter_state never prefers it.
    state.legacy_lens_preset_index = default_legacy_lens_popup_index();
    return true;
}

std::string ps_serialize_settings(const AeUiParameterState& state)
{
    std::string text;
    text += kSettingsHeader;
    text += ' ';
    text += std::to_string(kSettingsVersion);
    text += '\n';
    text += "lens=" + ps_lens_id(state) + '\n';

    for (const PsParamDesc& desc : kParams) {
        text += desc.key;
        text += '=';
        switch (desc.kind) {
            case PsParamKind::Float:
                text += format_float(state.*desc.float_member);
                break;
            case PsParamKind::Int:
                text += std::to_string(state.*desc.int_member);
                break;
            case PsParamKind::Bool:
                text += state.*desc.bool_member ? "on" : "off";
                break;
            case PsParamKind::Choice: {
                const std::vector<std::string> labels = ps_split_choice_labels(desc.choice_labels());
                const int index = state.*desc.int_member;
                if (index >= 1 && index <= static_cast<int>(labels.size())) {
                    text += labels[static_cast<std::size_t>(index - 1)];
                } else {
                    text += std::to_string(index);
                }
                break;
            }
        }
        text += '\n';
    }
    return text;
}

bool ps_deserialize_settings(const std::string& text, AeUiParameterState& out_state)
{
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line)) {
        return false;
    }

    const std::string_view header = trim(line);
    const std::string_view expected = kSettingsHeader;
    if (header.substr(0, expected.size()) != expected) {
        return false;
    }

    AeUiParameterState state = out_state;
    while (std::getline(stream, line)) {
        const std::string_view entry = trim(line);
        if (entry.empty() || entry.front() == '#') {
            continue;
        }
        const std::size_t equals = entry.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string key(trim(entry.substr(0, equals)));
        const std::string_view value = trim(entry.substr(equals + 1));

        if (key == "lens") {
            ps_set_lens_id(state, std::string(value));
            continue;
        }

        const PsParamDesc* desc = ps_find_param(key.c_str());
        if (!desc) {
            continue;
        }

        switch (desc->kind) {
            case PsParamKind::Float:
            case PsParamKind::Int: {
                double number = 0.0;
                if (parse_double(value, number)) {
                    ps_set_param_value(*desc, state, number);
                }
                break;
            }
            case PsParamKind::Bool:
                if (equals_ignore_case(value, "on") || equals_ignore_case(value, "true") || value == "1") {
                    state.*desc->bool_member = true;
                } else if (equals_ignore_case(value, "off") || equals_ignore_case(value, "false") || value == "0") {
                    state.*desc->bool_member = false;
                }
                break;
            case PsParamKind::Choice: {
                int index = 0;
                if (parse_choice(*desc, value, index)) {
                    state.*desc->int_member = index;
                }
                break;
            }
        }
    }

    state.pupil_jitter_auto_seed = false;
    out_state = state;
    return true;
}

bool ps_build_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state)
{
    AeUiParameterState adjusted = ui_state;
    adjusted.pupil_jitter_auto_seed = false;
    out_state = AeParameterState {};
    return apply_ui_parameter_state(adjusted, out_state);
}
