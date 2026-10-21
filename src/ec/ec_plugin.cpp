// EffectCraft effect plug-in (plug-in API v1, WebAssembly ABI).
//
// EffectCraft hands us premultiplied RGBA f32 pixels in place, plus the parameters below
// flattened to f64s in declaration order. The parameters mirror the After Effects plugin's
// (same defaults, ranges and popups) and are mapped through AeUiParameterState so both hosts
// share apply_ui_parameter_state(). Differences: one Lens popup instead of Manufacturer + Lens,
// no Mask Layer (plug-ins can't read other layers), and Fit Render Budget (ec_budget.h).

#include "builtin_lenses.h"
#include "ec_budget.h"
#include "ec_lens_bank.h"
#include "output_view.h"
#include "param_schema.h"
#include "parameter_state.h"
#include "plugin_version.h"
#include "render_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#define EC_EXPORT(name) extern "C" __attribute__((export_name(name)))

extern "C" void __wasm_call_ctors(void);

namespace {

#define EC_STRINGIFY_INNER(x) #x
#define EC_STRINGIFY(x) EC_STRINGIFY_INNER(x)

constexpr int kPluginApiVersion = 1;
constexpr const char* kEffectId = "net.njlent.flaresim.ec";
constexpr const char* kEffectName = "FlareSim EC";
constexpr const char* kVersion = EC_STRINGIFY(FLARESIM_AE_VERSION_MAJOR) "." EC_STRINGIFY(
    FLARESIM_AE_VERSION_MINOR) "." EC_STRINGIFY(FLARESIM_AE_VERSION_PATCH);
constexpr double kManualFloatMax = 1.0e9;
constexpr double kManualIntMax = 1000000.0;

enum class ParamType
{
    Slider,
    Angle,
    Checkbox,
    Popup,
};

enum class PopupKind
{
    None,
    Lens,
    SensorPreset,
    ProjectedCells,
    SpectralSamples,
    SpectralJitter,
    GhostCleanup,
    PupilJitter,
    OutputView,
};

enum class Field
{
    Lens,
    UseSensorSize,
    SensorPreset,
    FovH,
    AutoFovV,
    FovV,
    SensorWidth,
    SensorHeight,
    FocalLength,
    AnamorphicSqueeze,
    ApertureBlades,
    ApertureRotation,
    AdaptiveSampling,
    AdaptiveQuality,
    FlareGain,
    SkyBrightness,
    Threshold,
    SourceCap,
    ManualSource,
    SourceX,
    SourceY,
    SourceIntensity,
    SourceRed,
    SourceGreen,
    SourceBlue,
    RayGrid,
    Downsample,
    MaxSources,
    ClusterRadius,
    PreviewMode,
    PreviewRayGrid,
    PreviewMaxSources,
    PreviewDownsample,
    PreviewSpectralSamples,
    GhostBlur,
    GhostBlurPasses,
    HazeGain,
    HazeRadius,
    HazeBlurPasses,
    StarburstGain,
    StarburstScale,
    SpectralSamples,
    SpectralJitter,
    SpectralSeed,
    GhostCleanup,
    AdaptiveStrength,
    FootprintBias,
    FootprintClamp,
    MaxPairGrid,
    PairStart,
    PairCount,
    SurfaceStart,
    SurfaceCount,
    SurfaceGain,
    PupilJitter,
    JitterSeed,
    AutoSeed,
    CellCoverage,
    CellEdgeInset,
    View,
    FitBudget,
};

struct ParamDescriptor
{
    Field field;
    const char* id;
    const char* name;
    ParamType type;
    double min = 0.0;
    double max = 0.0;
    double slider_min = 0.0;
    double slider_max = 0.0;
    int decimals = 2;
    PopupKind popup = PopupKind::None;
};

constexpr ParamDescriptor slider(Field field, const char* id, const char* name,
                                 double min, double max, double slider_min, double slider_max,
                                 int decimals)
{
    return {field, id, name, ParamType::Slider, min, max, slider_min, slider_max, decimals};
}

constexpr ParamDescriptor int_slider(Field field, const char* id, const char* name,
                                     double min, double max, double slider_min, double slider_max)
{
    return slider(field, id, name, min, max, slider_min, slider_max, 0);
}

constexpr ParamDescriptor checkbox(Field field, const char* id, const char* name)
{
    return {field, id, name, ParamType::Checkbox};
}

constexpr ParamDescriptor popup(Field field, const char* id, const char* name, PopupKind kind)
{
    return {field, id, name, ParamType::Popup, 0.0, 0.0, 0.0, 0.0, 0, kind};
}

constexpr ParamDescriptor angle(Field field, const char* id, const char* name)
{
    return {field, id, name, ParamType::Angle};
}

// Ids are saved in EffectCraft projects: never rename or reorder them. Add new parameters
// at the end. Ranges match src/ae/plugin_params.cpp.
constexpr ParamDescriptor kParams[] = {
    popup(Field::Lens, "lens", "Lens", PopupKind::Lens),

    checkbox(Field::UseSensorSize, "use_sensor_size", "Use Sensor Size"),
    popup(Field::SensorPreset, "sensor_preset", "Sensor Preset", PopupKind::SensorPreset),
    slider(Field::FovH, "fov_h", "FOV H (deg)", 0.001, kManualFloatMax, 1.0, 180.0, 2),
    checkbox(Field::AutoFovV, "auto_fov_v", "Auto FOV V"),
    slider(Field::FovV, "fov_v", "FOV V (deg)", 0.001, kManualFloatMax, 1.0, 180.0, 2),
    slider(Field::SensorWidth, "sensor_width", "Sensor Width (mm)", 0.001, kManualFloatMax, 1.0, 100.0, 2),
    slider(Field::SensorHeight, "sensor_height", "Sensor Height (mm)", 0.001, kManualFloatMax, 1.0, 100.0, 2),
    slider(Field::FocalLength, "focal_length", "Focal Length (mm)", 0.001, kManualFloatMax, 1.0, 200.0, 2),
    slider(Field::AnamorphicSqueeze, "anamorphic_squeeze", "Anamorphic Squeeze", 0.1, kManualFloatMax, 1.0, 4.0, 2),

    int_slider(Field::ApertureBlades, "aperture_blades", "Aperture Blades", 0, kManualIntMax, 0, 16),
    angle(Field::ApertureRotation, "aperture_rotation", "Aperture Rotation"),

    popup(Field::AdaptiveSampling, "adaptive_sampling", "Adaptive Sampling", PopupKind::ProjectedCells),
    slider(Field::AdaptiveQuality, "adaptive_quality", "Adaptive Quality", 0.25, kManualFloatMax, 0.25, 2.0, 2),
    slider(Field::FlareGain, "flare_gain", "Flare Gain", 0.0, kManualFloatMax, 0.0, 5000.0, 1),
    slider(Field::SkyBrightness, "sky_brightness", "Sky Brightness", 0.0, kManualFloatMax, 0.0, 4.0, 3),
    slider(Field::Threshold, "threshold", "Threshold", 0.0, kManualFloatMax, 0.0, 64.0, 4),
    slider(Field::SourceCap, "source_cap", "Source Cap", 0.0, kManualFloatMax, 0.0, 64.0, 4),
    checkbox(Field::ManualSource, "manual_source", "Manual Source"),
    slider(Field::SourceX, "source_x", "Source X", -kManualFloatMax, kManualFloatMax, -1.0, 2.0, 3),
    slider(Field::SourceY, "source_y", "Source Y", -kManualFloatMax, kManualFloatMax, -1.0, 2.0, 3),
    slider(Field::SourceIntensity, "source_intensity", "Source Intensity", 0.0, kManualFloatMax, 0.0, 256.0, 3),
    slider(Field::SourceRed, "source_red", "Source Red", 0.0, kManualFloatMax, 0.0, 4.0, 3),
    slider(Field::SourceGreen, "source_green", "Source Green", 0.0, kManualFloatMax, 0.0, 4.0, 3),
    slider(Field::SourceBlue, "source_blue", "Source Blue", 0.0, kManualFloatMax, 0.0, 4.0, 3),
    checkbox(Field::FitBudget, "fit_budget", "Fit Render Budget"),
    int_slider(Field::RayGrid, "ray_grid", "Ray Grid", 1, kManualIntMax, 1, 512),
    int_slider(Field::Downsample, "downsample", "Downsample", 1, kManualIntMax, 1, 12),
    int_slider(Field::MaxSources, "max_sources", "Max Sources", 0, kManualIntMax, 0, 512),
    int_slider(Field::ClusterRadius, "cluster_radius", "Cluster Radius", 0, kManualIntMax, 0, 256),
    checkbox(Field::PreviewMode, "preview_mode", "Preview Mode"),
    int_slider(Field::PreviewRayGrid, "preview_ray_grid", "Preview Ray Grid", 1, kManualIntMax, 1, 128),
    int_slider(Field::PreviewMaxSources, "preview_max_sources", "Preview Max Sources", 0, kManualIntMax, 0, 512),
    int_slider(Field::PreviewDownsample, "preview_downsample", "Preview Downsample", 1, kManualIntMax, 1, 24),
    popup(Field::PreviewSpectralSamples, "preview_spectral_samples", "Preview Spectral Samples", PopupKind::SpectralSamples),

    slider(Field::GhostBlur, "ghost_blur", "Ghost Blur", 0.0, kManualFloatMax, 0.0, 0.05, 4),
    int_slider(Field::GhostBlurPasses, "ghost_blur_passes", "Ghost Blur Passes", 0, kManualIntMax, 0, 8),
    slider(Field::HazeGain, "haze_gain", "Haze Gain", 0.0, kManualFloatMax, 0.0, 10.0, 3),
    slider(Field::HazeRadius, "haze_radius", "Haze Radius", 0.0, kManualFloatMax, 0.0, 0.5, 3),
    int_slider(Field::HazeBlurPasses, "haze_blur_passes", "Haze Blur Passes", 0, kManualIntMax, 0, 8),
    slider(Field::StarburstGain, "starburst_gain", "Starburst Gain", 0.0, kManualFloatMax, 0.0, 10.0, 3),
    slider(Field::StarburstScale, "starburst_scale", "Starburst Scale", 0.0, kManualFloatMax, 0.0, 0.5, 3),
    popup(Field::SpectralSamples, "spectral_samples", "Spectral Samples", PopupKind::SpectralSamples),
    popup(Field::SpectralJitter, "spectral_jitter", "Spectral Jitter", PopupKind::SpectralJitter),
    int_slider(Field::SpectralSeed, "spectral_seed", "Spectral Seed", 0, kManualIntMax, 0, 1000000),
    popup(Field::GhostCleanup, "ghost_cleanup", "Ghost Cleanup", PopupKind::GhostCleanup),

    slider(Field::AdaptiveStrength, "adaptive_strength", "Adaptive Strength", 0.0, kManualFloatMax, 0.0, 2.0, 2),
    slider(Field::FootprintBias, "footprint_bias", "Footprint Bias", 0.1, kManualFloatMax, 0.25, 2.0, 2),
    slider(Field::FootprintClamp, "footprint_clamp", "Footprint Clamp", 0.1, kManualFloatMax, 0.5, 4.0, 2),
    int_slider(Field::MaxPairGrid, "max_pair_grid", "Max Pair Grid", 0, kManualIntMax, 0, 512),
    int_slider(Field::PairStart, "pair_start", "Pair Start", 0, kManualIntMax, 0, 512),
    int_slider(Field::PairCount, "pair_count", "Pair Count", 0, kManualIntMax, 0, 512),
    int_slider(Field::SurfaceStart, "surface_start", "Surface Start", 0, kManualIntMax, 0, 512),
    int_slider(Field::SurfaceCount, "surface_count", "Surface Count", 0, kManualIntMax, 0, 512),
    slider(Field::SurfaceGain, "surface_gain", "Surface Gain", 0.0, kManualFloatMax, 0.0, 4.0, 2),
    popup(Field::PupilJitter, "pupil_jitter", "Pupil Jitter", PopupKind::PupilJitter),
    int_slider(Field::JitterSeed, "jitter_seed", "Jitter Seed", 0, kManualIntMax, 0, 1000000),
    checkbox(Field::AutoSeed, "auto_seed", "Auto Seed"),
    slider(Field::CellCoverage, "cell_coverage", "Cell Coverage", 0.1, kManualFloatMax, 0.5, 2.5, 2),
    slider(Field::CellEdgeInset, "cell_edge_inset", "Cell Edge Inset", 0.0, kManualFloatMax, 0.0, 0.45, 2),

    popup(Field::View, "view", "View", PopupKind::OutputView),
};

constexpr int kParamCount = static_cast<int>(std::size(kParams));

// EffectCraft caps a plug-in at 1 GiB of memory. The render cache keeps a second copy of every
// layer of the frame, so it is only used up to about 1080p; at 4K the frame alone needs ~650 MB.
constexpr std::size_t kMaxCachedPixels = 2200000;

// ---------------------------------------------------------------------------
// Lens popup: one entry per builtin lens, "Manufacturer / Lens", in builtin table order.

int default_lens_popup_index()
{
    const BuiltinLensDescriptor* lenses = builtin_lenses();
    const BuiltinLensDescriptor* fallback = find_builtin_lens(default_builtin_lens_id());
    return fallback ? static_cast<int>(fallback - lenses) : 0;
}

void lens_popup_to_ui(int popup_index, AeUiParameterState& ui)
{
    const int count = static_cast<int>(std::min(builtin_lens_count(), ec_lens_popup_option_count()));
    if (popup_index < 0 || popup_index >= count) {
        popup_index = default_lens_popup_index();
    }
    const BuiltinLensDescriptor& lens = builtin_lenses()[popup_index];
    const BuiltinLensManufacturerDescriptor* manufacturer =
        builtin_lens_manufacturer(static_cast<std::size_t>(lens.manufacturer_index));
    ui.lens_manufacturer_index = lens.manufacturer_index + 1;
    ui.lens_model_index = manufacturer ? (popup_index - manufacturer->first_lens_index + 1) : 1;
    // The hidden AE "Legacy Lens" popup wins when it isn't at its default, so park it there.
    ui.legacy_lens_preset_index = default_legacy_lens_popup_index();
}

// ---------------------------------------------------------------------------
// Manifest

void split_popup(const std::string& popup_string, std::vector<std::string>& out)
{
    out.clear();
    std::size_t start = 0;
    while (start <= popup_string.size()) {
        const std::size_t bar = popup_string.find('|', start);
        const std::size_t end = bar == std::string::npos ? popup_string.size() : bar;
        out.emplace_back(popup_string.substr(start, end - start));
        if (bar == std::string::npos) {
            break;
        }
        start = bar + 1;
    }
}

// The Lens popup is the exception: its ~1400 options come pre-escaped from the lens bank
// generator (see build_manifest).
void popup_options(PopupKind kind, std::vector<std::string>& out)
{
    switch (kind) {
    case PopupKind::SensorPreset:
        return split_popup(build_sensor_preset_popup_string(), out);
    case PopupKind::ProjectedCells:
        return split_popup(build_projected_cells_mode_popup_string(), out);
    case PopupKind::SpectralSamples:
        return split_popup(build_spectral_samples_popup_string(), out);
    case PopupKind::SpectralJitter:
        return split_popup(build_spectral_jitter_mode_popup_string(), out);
    case PopupKind::GhostCleanup:
        return split_popup(build_ghost_cleanup_mode_popup_string(), out);
    case PopupKind::PupilJitter:
        return split_popup(build_pupil_jitter_mode_popup_string(), out);
    case PopupKind::OutputView:
        return split_popup(build_output_view_popup_string(), out);
    case PopupKind::Lens:
    case PopupKind::None:
        out.clear();
        return;
    }
}

// Default value of a parameter, read from the After Effects UI defaults so the hosts agree.
// Popup indices are 0-based here (After Effects popups are 1-based).
double default_value(Field field)
{
    const AeUiParameterState d {};
    switch (field) {
    case Field::Lens: return default_lens_popup_index();
    case Field::UseSensorSize: return d.use_sensor_size;
    case Field::SensorPreset: return d.sensor_preset_index - 1;
    case Field::FovH: return d.fov_h_deg;
    case Field::AutoFovV: return d.auto_fov_v;
    case Field::FovV: return d.fov_v_deg;
    case Field::SensorWidth: return d.sensor_width_mm;
    case Field::SensorHeight: return d.sensor_height_mm;
    case Field::FocalLength: return d.focal_length_mm;
    case Field::AnamorphicSqueeze: return d.anamorphic_squeeze;
    case Field::ApertureBlades: return d.aperture_blades;
    case Field::ApertureRotation: return d.aperture_rotation_deg;
    case Field::AdaptiveSampling: return d.projected_cells_mode_index - 1;
    case Field::AdaptiveQuality: return d.adaptive_quality;
    case Field::FlareGain: return d.flare_gain;
    case Field::SkyBrightness: return d.sky_brightness;
    case Field::Threshold: return d.threshold;
    case Field::SourceCap: return d.source_cap;
    case Field::ManualSource: return d.manual_source_enabled;
    case Field::SourceX: return d.manual_source_x;
    case Field::SourceY: return d.manual_source_y;
    case Field::SourceIntensity: return d.manual_source_intensity;
    case Field::SourceRed: return d.manual_source_r;
    case Field::SourceGreen: return d.manual_source_g;
    case Field::SourceBlue: return d.manual_source_b;
    case Field::RayGrid: return d.ray_grid;
    case Field::Downsample: return d.downsample;
    case Field::MaxSources: return d.max_sources;
    case Field::ClusterRadius: return d.cluster_radius_px;
    case Field::PreviewMode: return d.preview_mode;
    case Field::PreviewRayGrid: return d.preview_ray_grid;
    case Field::PreviewMaxSources: return d.preview_max_sources;
    case Field::PreviewDownsample: return d.preview_downsample;
    case Field::PreviewSpectralSamples: return d.preview_spectral_samples_index - 1;
    case Field::GhostBlur: return d.ghost_blur;
    case Field::GhostBlurPasses: return d.ghost_blur_passes;
    case Field::HazeGain: return d.haze_gain;
    case Field::HazeRadius: return d.haze_radius;
    case Field::HazeBlurPasses: return d.haze_blur_passes;
    case Field::StarburstGain: return d.starburst_gain;
    case Field::StarburstScale: return d.starburst_scale;
    case Field::SpectralSamples: return d.spectral_samples_index - 1;
    case Field::SpectralJitter: return d.spectral_jitter_mode_index - 1;
    case Field::SpectralSeed: return d.spectral_jitter_seed;
    case Field::GhostCleanup: return d.ghost_cleanup_mode_index - 1;
    case Field::AdaptiveStrength: return d.adaptive_sampling_strength;
    case Field::FootprintBias: return d.footprint_radius_bias;
    case Field::FootprintClamp: return d.footprint_clamp;
    case Field::MaxPairGrid: return d.max_adaptive_pair_grid;
    case Field::PairStart: return d.pair_start;
    case Field::PairCount: return d.pair_count;
    case Field::SurfaceStart: return d.surface_art_start;
    case Field::SurfaceCount: return d.surface_art_count;
    case Field::SurfaceGain: return d.surface_art_gain;
    case Field::PupilJitter: return d.pupil_jitter_mode_index - 1;
    case Field::JitterSeed: return d.pupil_jitter_seed;
    case Field::AutoSeed: return d.pupil_jitter_auto_seed;
    case Field::CellCoverage: return d.cell_coverage_bias;
    case Field::CellEdgeInset: return d.cell_edge_inset;
    case Field::View: return d.view_mode_index - 1;
    case Field::FitBudget: return 1.0;
    }
    return 0.0;
}

// Appends runs of plain characters in one go: EffectCraft meters every instruction, and
// growing the string a character at a time made the 95 KB manifest overrun the load budget.
void append_json_string(std::string& out, std::string_view value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    std::size_t run_start = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto u = static_cast<unsigned char>(value[i]);
        if (u >= 0x20 && u != '"' && u != '\\') {
            continue;
        }
        out.append(value.data() + run_start, i - run_start);
        if (u == '"' || u == '\\') {
            const char escaped[2] = {'\\', static_cast<char>(u)};
            out.append(escaped, 2);
        } else {
            const char escaped[6] = {'\\', 'u', '0', '0', kHex[u >> 4], kHex[u & 0xF]};
            out.append(escaped, 6);
        }
        run_start = i + 1;
    }
    out.append(value.data() + run_start, value.size() - run_start);
    out += '"';
}

// Integer-only formatting: printf's %g goes through software 128-bit floats on wasm32 and
// costs millions of instructions per number. Manifest numbers are short decimals (0.0005,
// 1.15, 1e9); 7 places drops float noise from defaults (0.15f prints as 0.15).
void append_json_number(std::string& out, double value)
{
    if (!std::isfinite(value)) {
        out += '0';
        return;
    }
    if (value < 0.0) {
        out += '-';
        value = -value;
    }
    constexpr int kPlaces = 7;
    constexpr long long kScale = 10000000LL;
    const auto scaled = static_cast<long long>(std::llround(value * static_cast<double>(kScale)));
    out += std::to_string(scaled / kScale);
    long long fraction = scaled % kScale;
    if (fraction == 0) {
        return;
    }
    char digits[kPlaces + 1] {};
    for (int i = kPlaces - 1; i >= 0; --i) {
        digits[i] = static_cast<char>('0' + fraction % 10);
        fraction /= 10;
    }
    int length = kPlaces;
    while (length > 0 && digits[length - 1] == '0') {
        --length;
    }
    out += '.';
    out.append(digits, static_cast<std::size_t>(length));
}

std::string build_manifest()
{
    std::string json;
    json.reserve(96 * 1024);
    json += "{\"api\":";
    append_json_number(json, kPluginApiVersion);
    json += ",\"id\":";
    append_json_string(json, kEffectId);
    json += ",\"name\":";
    append_json_string(json, kEffectName);
    json += ",\"category\":";
    append_json_string(json, FLARESIM_AE_EFFECT_CATEGORY);
    json += ",\"version\":";
    append_json_string(json, kVersion);
    json += ",\"author\":\"njlent\",\"description\":";
    append_json_string(json,
                       "Physically based lens flares: traces rays through a real lens prescription "
                       "to render ghosts, haze and starbursts from the bright parts of the layer.");
    json += ",\"params\":[";

    std::vector<std::string> options;
    for (int i = 0; i < kParamCount; ++i) {
        const ParamDescriptor& p = kParams[i];
        if (i > 0) {
            json += ',';
        }
        json += "{\"id\":";
        append_json_string(json, p.id);
        json += ",\"name\":";
        append_json_string(json, p.name);
        const double def = default_value(p.field);
        switch (p.type) {
        case ParamType::Slider:
            json += ",\"type\":\"slider\",\"default\":";
            append_json_number(json, def);
            json += ",\"min\":";
            append_json_number(json, p.min);
            json += ",\"max\":";
            append_json_number(json, p.max);
            json += ",\"sliderMin\":";
            append_json_number(json, p.slider_min);
            json += ",\"sliderMax\":";
            append_json_number(json, p.slider_max);
            json += ",\"decimals\":";
            append_json_number(json, p.decimals);
            break;
        case ParamType::Angle:
            json += ",\"type\":\"angle\",\"default\":";
            append_json_number(json, def);
            break;
        case ParamType::Checkbox:
            json += ",\"type\":\"checkbox\",\"default\":";
            json += def != 0.0 ? "true" : "false";
            break;
        case ParamType::Popup:
            json += ",\"type\":\"popup\",\"default\":";
            append_json_number(json, def);
            json += ",\"options\":[";
            if (p.popup == PopupKind::Lens) {
                json += ec_lens_popup_options_json();
                json += ']';
                break;
            }
            popup_options(p.popup, options);
            for (std::size_t o = 0; o < options.size(); ++o) {
                if (o > 0) {
                    json += ',';
                }
                append_json_string(json, options[o]);
            }
            json += ']';
            break;
        }
        json += '}';
    }
    json += "]}";
    return json;
}

// ---------------------------------------------------------------------------
// Parameter decoding

int as_int(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::lround(std::clamp(value, -2.0e9, 2.0e9)));
}

// 0-based EffectCraft popup index -> 1-based After Effects popup index.
int as_popup(double value)
{
    return std::max(0, as_int(value)) + 1;
}

float as_float(double value)
{
    return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
}

// Settings that only exist in the EffectCraft build.
struct EcOptions
{
    bool fit_budget = true;
};

void apply_param(Field field, double v, AeUiParameterState& ui, EcOptions& options)
{
    switch (field) {
    case Field::Lens: lens_popup_to_ui(as_int(v), ui); break;
    case Field::UseSensorSize: ui.use_sensor_size = v != 0.0; break;
    case Field::SensorPreset: ui.sensor_preset_index = as_popup(v); break;
    case Field::FovH: ui.fov_h_deg = as_float(v); break;
    case Field::AutoFovV: ui.auto_fov_v = v != 0.0; break;
    case Field::FovV: ui.fov_v_deg = as_float(v); break;
    case Field::SensorWidth: ui.sensor_width_mm = as_float(v); break;
    case Field::SensorHeight: ui.sensor_height_mm = as_float(v); break;
    case Field::FocalLength: ui.focal_length_mm = as_float(v); break;
    case Field::AnamorphicSqueeze: ui.anamorphic_squeeze = as_float(v); break;
    case Field::ApertureBlades: ui.aperture_blades = as_int(v); break;
    case Field::ApertureRotation: ui.aperture_rotation_deg = as_float(v); break;
    case Field::AdaptiveSampling: ui.projected_cells_mode_index = as_popup(v); break;
    case Field::AdaptiveQuality: ui.adaptive_quality = as_float(v); break;
    case Field::FlareGain: ui.flare_gain = as_float(v); break;
    case Field::SkyBrightness: ui.sky_brightness = as_float(v); break;
    case Field::Threshold: ui.threshold = as_float(v); break;
    case Field::SourceCap: ui.source_cap = as_float(v); break;
    case Field::ManualSource: ui.manual_source_enabled = v != 0.0; break;
    case Field::SourceX: ui.manual_source_x = as_float(v); break;
    case Field::SourceY: ui.manual_source_y = as_float(v); break;
    case Field::SourceIntensity: ui.manual_source_intensity = as_float(v); break;
    case Field::SourceRed: ui.manual_source_r = as_float(v); break;
    case Field::SourceGreen: ui.manual_source_g = as_float(v); break;
    case Field::SourceBlue: ui.manual_source_b = as_float(v); break;
    case Field::RayGrid: ui.ray_grid = as_int(v); break;
    case Field::Downsample: ui.downsample = as_int(v); break;
    case Field::MaxSources: ui.max_sources = as_int(v); break;
    case Field::ClusterRadius: ui.cluster_radius_px = as_int(v); break;
    case Field::PreviewMode: ui.preview_mode = v != 0.0; break;
    case Field::PreviewRayGrid: ui.preview_ray_grid = as_int(v); break;
    case Field::PreviewMaxSources: ui.preview_max_sources = as_int(v); break;
    case Field::PreviewDownsample: ui.preview_downsample = as_int(v); break;
    case Field::PreviewSpectralSamples: ui.preview_spectral_samples_index = as_popup(v); break;
    case Field::GhostBlur: ui.ghost_blur = as_float(v); break;
    case Field::GhostBlurPasses: ui.ghost_blur_passes = as_int(v); break;
    case Field::HazeGain: ui.haze_gain = as_float(v); break;
    case Field::HazeRadius: ui.haze_radius = as_float(v); break;
    case Field::HazeBlurPasses: ui.haze_blur_passes = as_int(v); break;
    case Field::StarburstGain: ui.starburst_gain = as_float(v); break;
    case Field::StarburstScale: ui.starburst_scale = as_float(v); break;
    case Field::SpectralSamples: ui.spectral_samples_index = as_popup(v); break;
    case Field::SpectralJitter: ui.spectral_jitter_mode_index = as_popup(v); break;
    case Field::SpectralSeed: ui.spectral_jitter_seed = as_int(v); break;
    case Field::GhostCleanup: ui.ghost_cleanup_mode_index = as_popup(v); break;
    case Field::AdaptiveStrength: ui.adaptive_sampling_strength = as_float(v); break;
    case Field::FootprintBias: ui.footprint_radius_bias = as_float(v); break;
    case Field::FootprintClamp: ui.footprint_clamp = as_float(v); break;
    case Field::MaxPairGrid: ui.max_adaptive_pair_grid = as_int(v); break;
    case Field::PairStart: ui.pair_start = as_int(v); break;
    case Field::PairCount: ui.pair_count = as_int(v); break;
    case Field::SurfaceStart: ui.surface_art_start = as_int(v); break;
    case Field::SurfaceCount: ui.surface_art_count = as_int(v); break;
    case Field::SurfaceGain: ui.surface_art_gain = as_float(v); break;
    case Field::PupilJitter: ui.pupil_jitter_mode_index = as_popup(v); break;
    case Field::JitterSeed: ui.pupil_jitter_seed = as_int(v); break;
    case Field::AutoSeed: ui.pupil_jitter_auto_seed = v != 0.0; break;
    case Field::CellCoverage: ui.cell_coverage_bias = as_float(v); break;
    case Field::CellEdgeInset: ui.cell_edge_inset = as_float(v); break;
    case Field::View: ui.view_mode_index = as_popup(v); break;
    case Field::FitBudget: options.fit_budget = v != 0.0; break;
    }
}

// EffectCraft passes layer time in seconds, not a frame number. Milliseconds give every
// frame at any common frame rate its own Stratified seed, like the frame index in AE.
int time_seed(double time_seconds)
{
    if (!std::isfinite(time_seconds)) {
        return 0;
    }
    return as_int(time_seconds * 1000.0);
}

// ---------------------------------------------------------------------------
// Render state. EffectCraft gives each render thread its own module instance, so these
// globals are per thread.

struct RenderContext
{
    std::string manifest;
    std::vector<std::uint8_t> io_buffer;

    std::string lens_id;
    LensSystem lens;
    bool lens_loaded = false;

    FrameRenderCache cache;
    FrameRenderOutputs outputs;
    EcBudgetPlan budget;
    double budget_report[14] {};
    std::vector<float> in_r, in_g, in_b;
    std::vector<float> out_r, out_g, out_b;
};

RenderContext& context()
{
    static RenderContext* ctx = nullptr;
    if (!ctx) {
        __wasm_call_ctors();
        ctx = new RenderContext();
    }
    return *ctx;
}

const LensSystem* resolve_lens(RenderContext& ctx, const char* builtin_id)
{
    if (!builtin_id) {
        return nullptr;
    }
    if (ctx.lens_loaded && ctx.lens_id == builtin_id) {
        return &ctx.lens;
    }
    ctx.lens_loaded = false;
    if (!ec_load_builtin_lens(builtin_id, ctx.lens)) {
        return nullptr;
    }
    ctx.lens_id = builtin_id;
    ctx.lens_loaded = true;
    ctx.cache.clear();
    return &ctx.lens;
}

enum RenderError : int
{
    kRenderOk = 0,
    kRenderBadArguments = 1,
    kRenderBadParams = 2,
    kRenderBadLens = 3,
    kRenderFailed = 4,
};

int render(float* pixels, int width, int height, const double* params, int nparams, double time)
{
    if (!pixels || width <= 0 || height <= 0 || !params) {
        return kRenderBadArguments;
    }
    if (nparams != kParamCount) {
        return kRenderBadParams;
    }

    AeUiParameterState ui {};
    EcOptions options {};
    for (int i = 0; i < kParamCount; ++i) {
        apply_param(kParams[i].field, params[i], ui, options);
    }

    AeParameterState state {};
    if (!apply_ui_parameter_state(ui, state)) {
        return kRenderBadParams;
    }
    if (state.pupil_jitter_auto_seed && state.pupil_jitter_mode == PupilJitterMode::Stratified) {
        state.pupil_jitter_seed = time_seed(time);
    }

    RenderContext& ctx = context();
    const LensSystem* lens = resolve_lens(ctx, state.lens.builtin_id);
    if (!lens) {
        return kRenderBadLens;
    }

    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    ctx.in_r.resize(np);
    ctx.in_g.resize(np);
    ctx.in_b.resize(np);
    for (std::size_t i = 0; i < np; ++i) {
        const float* px = pixels + i * 4u;
        ctx.in_r[i] = px[0];
        ctx.in_g[i] = px[1];
        ctx.in_b[i] = px[2];
    }

    FrameRenderCache* cache = np <= kMaxCachedPixels ? &ctx.cache : nullptr;
    if (!cache) {
        ctx.cache.clear();
    }

    const RgbImageView input {ctx.in_r.data(), ctx.in_g.data(), ctx.in_b.data(), width, height};
    FrameRenderSettings settings = build_frame_render_settings(state);
    const FrameRenderPlan plan = build_output_view_render_plan(state.view);
    ctx.budget = {};
    if (plan.need_flare) {
        // Find the sources first (the cache keeps them for the full render) so the governor
        // knows what the ghosts will cost.
        const FrameRenderPlan sources_only = build_output_view_render_plan(AeOutputView::Sources);
        if (!render_frame(*lens, input, settings, ctx.outputs, sources_only, cache, nullptr)) {
            return kRenderFailed;
        }
        ctx.budget = ec_fit_render_budget(*lens,
                                          settings,
                                          plan,
                                          width,
                                          height,
                                          ctx.outputs.detected_sources,
                                          options.fit_budget);
    }
    if (!render_frame(*lens, input, settings, ctx.outputs, plan, cache, nullptr)) {
        return kRenderFailed;
    }

    ctx.out_r.assign(np, 0.0f);
    ctx.out_g.assign(np, 0.0f);
    ctx.out_b.assign(np, 0.0f);
    const MutableRgbImageView output {ctx.out_r.data(), ctx.out_g.data(), ctx.out_b.data(), width, height};
    if (!compose_output_view(state.view, input, settings, ctx.outputs, output)) {
        return kRenderFailed;
    }

    // Same alpha rule as the AE bridge: the flare adds coverage where it is brighter than
    // the layer, so it shows over transparent areas. px[3] still holds the input alpha.
    for (std::size_t i = 0; i < np; ++i) {
        float* px = pixels + i * 4u;
        const float rendered_alpha =
            std::clamp(std::max({ctx.out_r[i], ctx.out_g[i], ctx.out_b[i], 0.0f}), 0.0f, 1.0f);
        px[0] = ctx.out_r[i];
        px[1] = ctx.out_g[i];
        px[2] = ctx.out_b[i];
        px[3] = std::max(px[3], rendered_alpha);
    }
    return kRenderOk;
}

} // namespace

EC_EXPORT("ec_api_version") int ec_api_version()
{
    return kPluginApiVersion;
}

EC_EXPORT("ec_manifest_ptr") int ec_manifest_ptr()
{
    RenderContext& ctx = context();
    if (ctx.manifest.empty()) {
        ctx.manifest = build_manifest();
    }
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(ctx.manifest.data()));
}

EC_EXPORT("ec_manifest_len") int ec_manifest_len()
{
    RenderContext& ctx = context();
    if (ctx.manifest.empty()) {
        ctx.manifest = build_manifest();
    }
    return static_cast<int>(ctx.manifest.size());
}

EC_EXPORT("ec_alloc") int ec_alloc(int bytes)
{
    if (bytes < 0) {
        return 0;
    }
    RenderContext& ctx = context();
    if (ctx.io_buffer.size() < static_cast<std::size_t>(bytes)) {
        std::vector<std::uint8_t>().swap(ctx.io_buffer);
        ctx.io_buffer.resize(static_cast<std::size_t>(bytes));
    }
    // operator new in wasi-libc returns 16-byte aligned blocks; the ABI needs 8.
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(ctx.io_buffer.data()));
}

// Not part of the plug-in API: what the render budget governor decided for the last frame,
// for tests/ec_host. 14 doubles: budget, requested estimate, estimate, active pairs,
// requested sources, sources, requested ray grid, ray grid, requested cleanup, cleanup,
// reduced (0/1), pixels, requested cluster radius, cluster radius.
EC_EXPORT("flaresim_ec_budget_report") int flaresim_ec_budget_report()
{
    RenderContext& ctx = context();
    const EcBudgetPlan& b = ctx.budget;
    double* r = ctx.budget_report;
    r[0] = b.budget_fuel;
    r[1] = b.requested_fuel;
    r[2] = b.estimated_fuel;
    r[3] = b.active_pairs;
    r[4] = b.requested_sources;
    r[5] = b.sources;
    r[6] = b.requested_ray_grid;
    r[7] = b.ray_grid;
    r[8] = static_cast<double>(b.requested_cleanup);
    r[9] = static_cast<double>(b.cleanup);
    r[10] = b.reduced ? 1.0 : 0.0;
    r[11] = static_cast<double>(ctx.in_r.size());
    r[12] = b.requested_cluster_radius;
    r[13] = b.cluster_radius;
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(r));
}

EC_EXPORT("ec_render") int ec_render(int pixels, int width, int height, int params, int nparams,
                                     double time, double scale)
{
    (void)scale;
    return render(reinterpret_cast<float*>(static_cast<std::uintptr_t>(pixels)),
                  width,
                  height,
                  reinterpret_cast<const double*>(static_cast<std::uintptr_t>(params)),
                  nparams,
                  time);
}
