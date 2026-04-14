#include "plugin_entry.h"

#include "AEFX_SuiteHelper.h"
#include "AE_EffectCBSuites.h"

#include "asset_root.h"
#include "builtin_lenses.h"
#include "frame_bridge.h"
#include "param_schema.h"

#ifdef AE_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr A_long kSmartInputCheckoutId = 1;
constexpr A_long kSmartMaskCheckoutId = 2;

struct SmartRenderLayerRects
{
    PF_LRect input_rect {};
    PF_LRect mask_rect {};
    PF_Boolean has_mask = FALSE;
};

void delete_smart_render_layer_rects(void* data)
{
    auto* rects = reinterpret_cast<SmartRenderLayerRects*>(data);
    delete rects;
}

bool read_ui_state_from_params(PF_ParamDef* params[], AeUiParameterState& out_state)
{
    if (!params) {
        return false;
    }

    out_state.legacy_lens_preset_index = params[PARAM_LEGACY_LENS_PRESET]->u.pd.value;
    out_state.lens_manufacturer_index = params[PARAM_LENS_MANUFACTURER]->u.pd.value;

    const int manufacturer_index = out_state.lens_manufacturer_index - 1;
    if (manufacturer_index < 0 ||
        manufacturer_index >= static_cast<int>(builtin_lens_manufacturer_count())) {
        return false;
    }

    out_state.lens_model_index = params[lens_popup_param(manufacturer_index)]->u.pd.value;
    out_state.use_sensor_size = params[use_sensor_size_param()]->u.bd.value != 0;
    out_state.sensor_preset_index = params[sensor_preset_param()]->u.pd.value;
    out_state.fov_h_deg = params[fov_h_param()]->u.fs_d.value;
    out_state.auto_fov_v = params[auto_fov_v_param()]->u.bd.value != 0;
    out_state.fov_v_deg = params[fov_v_param()]->u.fs_d.value;
    out_state.sensor_width_mm = params[sensor_width_param()]->u.fs_d.value;
    out_state.sensor_height_mm = params[sensor_height_param()]->u.fs_d.value;
    out_state.focal_length_mm = params[focal_length_param()]->u.fs_d.value;
    out_state.aperture_blades = params[aperture_blades_param()]->u.sd.value;
    out_state.aperture_rotation_deg = params[aperture_rotation_param()]->u.fs_d.value;
    out_state.flare_gain = params[flare_gain_param()]->u.fs_d.value;
    out_state.sky_brightness = params[sky_brightness_param()]->u.fs_d.value;
    out_state.threshold = params[threshold_param()]->u.fs_d.value;
    out_state.ray_grid = params[ray_grid_param()]->u.sd.value;
    out_state.downsample = params[downsample_param()]->u.sd.value;
    out_state.max_sources = params[max_sources_param()]->u.sd.value;
    out_state.cluster_radius_px = params[cluster_radius_param()]->u.sd.value;
    out_state.ghost_blur = params[ghost_blur_param()]->u.fs_d.value;
    out_state.ghost_blur_passes = params[ghost_blur_passes_param()]->u.sd.value;
    out_state.ghost_cleanup_mode_index = params[ghost_cleanup_mode_param()]->u.pd.value;
    out_state.haze_gain = params[haze_gain_param()]->u.fs_d.value;
    out_state.haze_radius = params[haze_radius_param()]->u.fs_d.value;
    out_state.haze_blur_passes = params[haze_blur_passes_param()]->u.sd.value;
    out_state.starburst_gain = params[starburst_gain_param()]->u.fs_d.value;
    out_state.starburst_scale = params[starburst_scale_param()]->u.fs_d.value;
    out_state.spectral_samples_index = params[spectral_samples_param()]->u.pd.value;
    out_state.adaptive_sampling_strength = params[adaptive_sampling_strength_param()]->u.fs_d.value;
    out_state.footprint_radius_bias = params[footprint_radius_bias_param()]->u.fs_d.value;
    out_state.footprint_clamp = params[footprint_clamp_param()]->u.fs_d.value;
    out_state.max_adaptive_pair_grid = params[max_adaptive_pair_grid_param()]->u.sd.value;
    out_state.pupil_jitter_mode_index = params[pupil_jitter_mode_param()]->u.pd.value;
    out_state.pupil_jitter_seed = params[pupil_jitter_seed_param()]->u.sd.value;
    out_state.projected_cells_mode_index = params[projected_cells_mode_param()]->u.pd.value;
    out_state.cell_coverage_bias = params[cell_coverage_bias_param()]->u.fs_d.value;
    out_state.cell_edge_inset = params[cell_edge_inset_param()]->u.fs_d.value;
    out_state.view_mode_index = params[view_mode_param()]->u.pd.value;
    return true;
}

template <typename RectT>
void union_rects(const RectT& src, RectT& dst)
{
    if (dst.left == 0 && dst.top == 0 && dst.right == 0 && dst.bottom == 0) {
        dst = src;
        return;
    }

    dst.left = std::min(dst.left, src.left);
    dst.top = std::min(dst.top, src.top);
    dst.right = std::max(dst.right, src.right);
    dst.bottom = std::max(dst.bottom, src.bottom);
}

bool resolve_asset_root(std::string& out_asset_root)
{
    out_asset_root.clear();

#ifdef FLARESIM_REPO_ROOT
    if (is_flaresim_asset_root(FLARESIM_REPO_ROOT)) {
        out_asset_root = FLARESIM_REPO_ROOT;
        return true;
    }
#endif

#ifdef AE_OS_WIN
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&PluginHandleSmartRender),
                           &module)) {
        char module_path[MAX_PATH] {};
        const DWORD module_path_len = GetModuleFileNameA(module, module_path, MAX_PATH);
        if (module_path_len > 0 &&
            find_flaresim_asset_root(std::string(module_path, module_path_len), out_asset_root)) {
            return true;
        }
    }
#endif

    return false;
}

template <typename PixelT>
void copy_world_to_linear(const PF_EffectWorld& world, std::vector<PixelT>& out_pixels)
{
    const int width = world.width;
    const int height = world.height;

    out_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    const char* row = reinterpret_cast<const char*>(world.data);
    for (int y = 0; y < height; ++y) {
        const auto* src = reinterpret_cast<const PixelT*>(row);
        auto* dst = out_pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width));
        std::copy_n(src, width, dst);
        row += world.rowbytes;
    }
}

template <typename PixelT>
void copy_linear_to_world(const std::vector<PixelT>& pixels, PF_EffectWorld& world)
{
    const int width = world.width;
    const int height = world.height;

    char* row = reinterpret_cast<char*>(world.data);
    for (int y = 0; y < height; ++y) {
        const auto* src = pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width));
        auto* dst = reinterpret_cast<PixelT*>(row);
        std::copy_n(src, width, dst);
        row += world.rowbytes;
    }
}

template <typename HostPixelT, typename BridgePixelT>
bool expand_mask_world_to_bridge_pixels(const PF_EffectWorld& world,
                                        int target_width,
                                        int target_height,
                                        const PF_LRect* layer_rect,
                                        std::vector<BridgePixelT>& out_pixels)
{
    if (!world.data || target_width <= 0 || target_height <= 0) {
        return false;
    }

    out_pixels.assign(static_cast<size_t>(target_width) * static_cast<size_t>(target_height), {});

    int dst_left = 0;
    int dst_top = 0;
    int copy_width = std::min(world.width, target_width);
    int copy_height = std::min(world.height, target_height);

    if (world.width == target_width && world.height == target_height) {
        dst_left = 0;
        dst_top = 0;
    } else if (layer_rect) {
        const int rect_width = layer_rect->right - layer_rect->left;
        const int rect_height = layer_rect->bottom - layer_rect->top;
        const bool rect_matches_buffer =
            layer_rect->left >= 0 &&
            layer_rect->top >= 0 &&
            layer_rect->right <= target_width &&
            layer_rect->bottom <= target_height &&
            rect_width == world.width &&
            rect_height == world.height;

        if (rect_matches_buffer) {
            dst_left = layer_rect->left;
            dst_top = layer_rect->top;
            copy_width = std::min(copy_width, target_width - dst_left);
            copy_height = std::min(copy_height, target_height - dst_top);
        }
    }

    const char* row = reinterpret_cast<const char*>(world.data);
    for (int y = 0; y < copy_height; ++y) {
        const auto* src = reinterpret_cast<const HostPixelT*>(row);
        auto* dst = out_pixels.data() +
                    (static_cast<size_t>(dst_top + y) * static_cast<size_t>(target_width)) +
                    static_cast<size_t>(dst_left);
        std::memcpy(dst, src, static_cast<size_t>(copy_width) * sizeof(BridgePixelT));
        row += world.rowbytes;
    }

    return true;
}

template <typename HostPixelT, typename BridgePixelT>
bool render_world_pixels(const std::string& asset_root,
                         const AeParameterState& state,
                         const PF_EffectWorld& input_world,
                         const PF_EffectWorld* mask_world,
                         const PF_LRect* mask_rect,
                         PF_EffectWorld& output_world)
{
    if (!input_world.data || !output_world.data ||
        input_world.width != output_world.width ||
        input_world.height != output_world.height) {
        return false;
    }

    std::vector<HostPixelT> host_input;
    copy_world_to_linear(input_world, host_input);

    std::vector<BridgePixelT> bridge_input(host_input.size());
    std::memcpy(bridge_input.data(),
                host_input.data(),
                bridge_input.size() * sizeof(BridgePixelT));

    const BridgePixelT* bridge_mask_pixels = nullptr;
    std::vector<BridgePixelT> bridge_mask;
    if (mask_world &&
        mask_world->data &&
        expand_mask_world_to_bridge_pixels<HostPixelT, BridgePixelT>(
            *mask_world, input_world.width, input_world.height, mask_rect, bridge_mask)) {
        bridge_mask_pixels = bridge_mask.data();
    }

    std::vector<BridgePixelT> bridge_output(
        static_cast<size_t>(input_world.width) * static_cast<size_t>(input_world.height));

    if (!render_frame_to_pixels(asset_root,
                                state,
                                bridge_input.data(),
                                bridge_output.data(),
                                input_world.width,
                                input_world.height,
                                bridge_mask_pixels)) {
        return false;
    }

    std::vector<HostPixelT> host_output(bridge_output.size());
    std::memcpy(host_output.data(),
                bridge_output.data(),
                host_output.size() * sizeof(HostPixelT));

    copy_linear_to_world(host_output, output_world);
    return true;
}

PF_Err render_checked_out_worlds(PF_InData* in_data,
                                 PF_OutData* out_data,
                                 const AeParameterState& state,
                                 PF_EffectWorld* input_world,
                                 PF_EffectWorld* mask_world,
                                 const PF_LRect* mask_rect,
                                 PF_EffectWorld* output_world)
{
    if (!input_world || !output_world) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    std::string asset_root;
    if (!resolve_asset_root(asset_root)) {
        return PF_COPY(input_world, output_world, nullptr, nullptr);
    }

    AEFX_SuiteScoper<PF_WorldSuite2> world_suite(
        in_data,
        kPFWorldSuite,
        kPFWorldSuiteVersion2,
        out_data);

    PF_PixelFormat format = PF_PixelFormat_INVALID;
    PF_Err err = world_suite->PF_GetPixelFormat(input_world, &format);
    if (err != PF_Err_NONE) {
        return err;
    }

    bool rendered = false;
    switch (format) {
        case PF_PixelFormat_ARGB128:
            rendered = render_world_pixels<PF_PixelFloat, AePixel32Like>(
                asset_root, state, *input_world, mask_world, mask_rect, *output_world);
            break;
        case PF_PixelFormat_ARGB64:
            rendered = render_world_pixels<PF_Pixel16, AePixel16Like>(
                asset_root, state, *input_world, mask_world, mask_rect, *output_world);
            break;
        case PF_PixelFormat_ARGB32:
            rendered = render_world_pixels<PF_Pixel8, AePixel8Like>(
                asset_root, state, *input_world, mask_world, mask_rect, *output_world);
            break;
        default:
            return PF_COPY(input_world, output_world, nullptr, nullptr);
    }

    if (!rendered) {
        return PF_COPY(input_world, output_world, nullptr, nullptr);
    }

    return PF_Err_NONE;
}

PF_Err build_render_state_from_checked_out_params(PF_InData* in_data,
                                                  AeParameterState& out_state)
{
    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    PF_ParamDef legacy_lens_param;
    PF_ParamDef manufacturer_param;
    PF_ParamDef lens_model_param;
    PF_ParamDef use_sensor_param_def;
    PF_ParamDef sensor_preset_param_def;
    PF_ParamDef fov_h_param_def;
    PF_ParamDef auto_fov_v_param_def;
    PF_ParamDef fov_v_param_def;
    PF_ParamDef sensor_width_param_def;
    PF_ParamDef sensor_height_param_def;
    PF_ParamDef focal_length_param_def;
    PF_ParamDef aperture_blades_param_def;
    PF_ParamDef aperture_rotation_param_def;
    PF_ParamDef flare_gain_param_def;
    PF_ParamDef sky_brightness_param_def;
    PF_ParamDef threshold_param_def;
    PF_ParamDef ray_grid_param_def;
    PF_ParamDef downsample_param_def;
    PF_ParamDef max_sources_param_def;
    PF_ParamDef cluster_radius_param_def;
    PF_ParamDef ghost_blur_param_def;
    PF_ParamDef ghost_blur_passes_param_def;
    PF_ParamDef ghost_cleanup_mode_param_def;
    PF_ParamDef haze_gain_param_def;
    PF_ParamDef haze_radius_param_def;
    PF_ParamDef haze_blur_passes_param_def;
    PF_ParamDef starburst_gain_param_def;
    PF_ParamDef starburst_scale_param_def;
    PF_ParamDef spectral_samples_param_def;
    PF_ParamDef adaptive_sampling_strength_param_def;
    PF_ParamDef footprint_radius_bias_param_def;
    PF_ParamDef footprint_clamp_param_def;
    PF_ParamDef max_adaptive_pair_grid_param_def;
    PF_ParamDef pupil_jitter_mode_param_def;
    PF_ParamDef pupil_jitter_seed_param_def;
    PF_ParamDef projected_cells_mode_param_def;
    PF_ParamDef cell_coverage_bias_param_def;
    PF_ParamDef cell_edge_inset_param_def;
    PF_ParamDef view_param_def;
    PF_ParamDef mask_layer_param_def;
    AEFX_CLR_STRUCT(legacy_lens_param);
    AEFX_CLR_STRUCT(manufacturer_param);
    AEFX_CLR_STRUCT(lens_model_param);
    AEFX_CLR_STRUCT(use_sensor_param_def);
    AEFX_CLR_STRUCT(sensor_preset_param_def);
    AEFX_CLR_STRUCT(fov_h_param_def);
    AEFX_CLR_STRUCT(auto_fov_v_param_def);
    AEFX_CLR_STRUCT(fov_v_param_def);
    AEFX_CLR_STRUCT(sensor_width_param_def);
    AEFX_CLR_STRUCT(sensor_height_param_def);
    AEFX_CLR_STRUCT(focal_length_param_def);
    AEFX_CLR_STRUCT(aperture_blades_param_def);
    AEFX_CLR_STRUCT(aperture_rotation_param_def);
    AEFX_CLR_STRUCT(flare_gain_param_def);
    AEFX_CLR_STRUCT(sky_brightness_param_def);
    AEFX_CLR_STRUCT(threshold_param_def);
    AEFX_CLR_STRUCT(ray_grid_param_def);
    AEFX_CLR_STRUCT(downsample_param_def);
    AEFX_CLR_STRUCT(max_sources_param_def);
    AEFX_CLR_STRUCT(cluster_radius_param_def);
    AEFX_CLR_STRUCT(ghost_blur_param_def);
    AEFX_CLR_STRUCT(ghost_blur_passes_param_def);
    AEFX_CLR_STRUCT(ghost_cleanup_mode_param_def);
    AEFX_CLR_STRUCT(haze_gain_param_def);
    AEFX_CLR_STRUCT(haze_radius_param_def);
    AEFX_CLR_STRUCT(haze_blur_passes_param_def);
    AEFX_CLR_STRUCT(starburst_gain_param_def);
    AEFX_CLR_STRUCT(starburst_scale_param_def);
    AEFX_CLR_STRUCT(spectral_samples_param_def);
    AEFX_CLR_STRUCT(adaptive_sampling_strength_param_def);
    AEFX_CLR_STRUCT(footprint_radius_bias_param_def);
    AEFX_CLR_STRUCT(footprint_clamp_param_def);
    AEFX_CLR_STRUCT(max_adaptive_pair_grid_param_def);
    AEFX_CLR_STRUCT(pupil_jitter_mode_param_def);
    AEFX_CLR_STRUCT(pupil_jitter_seed_param_def);
    AEFX_CLR_STRUCT(projected_cells_mode_param_def);
    AEFX_CLR_STRUCT(cell_coverage_bias_param_def);
    AEFX_CLR_STRUCT(cell_edge_inset_param_def);
    AEFX_CLR_STRUCT(view_param_def);
    AEFX_CLR_STRUCT(mask_layer_param_def);

    bool legacy_lens_checked_out = false;
    bool manufacturer_checked_out = false;
    bool lens_model_checked_out = false;
    bool use_sensor_checked_out = false;
    bool sensor_preset_checked_out = false;
    bool fov_h_checked_out = false;
    bool auto_fov_v_checked_out = false;
    bool fov_v_checked_out = false;
    bool sensor_width_checked_out = false;
    bool sensor_height_checked_out = false;
    bool focal_length_checked_out = false;
    bool aperture_blades_checked_out = false;
    bool aperture_rotation_checked_out = false;
    bool flare_gain_checked_out = false;
    bool sky_brightness_checked_out = false;
    bool threshold_checked_out = false;
    bool ray_grid_checked_out = false;
    bool downsample_checked_out = false;
    bool max_sources_checked_out = false;
    bool cluster_radius_checked_out = false;
    bool ghost_blur_checked_out = false;
    bool ghost_blur_passes_checked_out = false;
    bool ghost_cleanup_mode_checked_out = false;
    bool haze_gain_checked_out = false;
    bool haze_radius_checked_out = false;
    bool haze_blur_passes_checked_out = false;
    bool starburst_gain_checked_out = false;
    bool starburst_scale_checked_out = false;
    bool spectral_samples_checked_out = false;
    bool adaptive_sampling_strength_checked_out = false;
    bool footprint_radius_bias_checked_out = false;
    bool footprint_clamp_checked_out = false;
    bool max_adaptive_pair_grid_checked_out = false;
    bool pupil_jitter_mode_checked_out = false;
    bool pupil_jitter_seed_checked_out = false;
    bool projected_cells_mode_checked_out = false;
    bool cell_coverage_bias_checked_out = false;
    bool cell_edge_inset_checked_out = false;
    bool view_checked_out = false;
    bool mask_layer_checked_out = false;

    ERR(PF_CHECKOUT_PARAM(in_data,
                          PARAM_LEGACY_LENS_PRESET,
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &legacy_lens_param));
    legacy_lens_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          PARAM_LENS_MANUFACTURER,
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &manufacturer_param));
    manufacturer_checked_out = (err == PF_Err_NONE);

    int lens_model_param_index = -1;
    if (!err) {
        lens_model_param_index = lens_popup_param(manufacturer_param.u.pd.value - 1);
        ERR(PF_CHECKOUT_PARAM(in_data,
                              lens_model_param_index,
                              in_data->current_time,
                              in_data->time_step,
                              in_data->time_scale,
                              &lens_model_param));
        lens_model_checked_out = (err == PF_Err_NONE);
    }

    if (!err) {
        ERR(PF_CHECKOUT_PARAM(in_data,
                              use_sensor_size_param(),
                              in_data->current_time,
                              in_data->time_step,
                              in_data->time_scale,
                              &use_sensor_param_def));
        use_sensor_checked_out = (err == PF_Err_NONE);
    }

    ERR(PF_CHECKOUT_PARAM(in_data,
                          sensor_preset_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &sensor_preset_param_def));
    sensor_preset_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          fov_h_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &fov_h_param_def));
    fov_h_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          auto_fov_v_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &auto_fov_v_param_def));
    auto_fov_v_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          fov_v_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &fov_v_param_def));
    fov_v_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          sensor_width_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &sensor_width_param_def));
    sensor_width_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          sensor_height_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &sensor_height_param_def));
    sensor_height_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          focal_length_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &focal_length_param_def));
    focal_length_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          aperture_blades_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &aperture_blades_param_def));
    aperture_blades_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          aperture_rotation_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &aperture_rotation_param_def));
    aperture_rotation_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          flare_gain_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &flare_gain_param_def));
    flare_gain_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          sky_brightness_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &sky_brightness_param_def));
    sky_brightness_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          threshold_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &threshold_param_def));
    threshold_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          ray_grid_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &ray_grid_param_def));
    ray_grid_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          downsample_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &downsample_param_def));
    downsample_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          max_sources_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &max_sources_param_def));
    max_sources_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          cluster_radius_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &cluster_radius_param_def));
    cluster_radius_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          ghost_blur_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &ghost_blur_param_def));
    ghost_blur_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          ghost_blur_passes_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &ghost_blur_passes_param_def));
    ghost_blur_passes_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          ghost_cleanup_mode_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &ghost_cleanup_mode_param_def));
    ghost_cleanup_mode_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          haze_gain_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &haze_gain_param_def));
    haze_gain_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          haze_radius_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &haze_radius_param_def));
    haze_radius_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          haze_blur_passes_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &haze_blur_passes_param_def));
    haze_blur_passes_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          starburst_gain_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &starburst_gain_param_def));
    starburst_gain_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          starburst_scale_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &starburst_scale_param_def));
    starburst_scale_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          spectral_samples_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &spectral_samples_param_def));
    spectral_samples_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          adaptive_sampling_strength_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &adaptive_sampling_strength_param_def));
    adaptive_sampling_strength_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          footprint_radius_bias_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &footprint_radius_bias_param_def));
    footprint_radius_bias_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          footprint_clamp_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &footprint_clamp_param_def));
    footprint_clamp_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          max_adaptive_pair_grid_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &max_adaptive_pair_grid_param_def));
    max_adaptive_pair_grid_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          pupil_jitter_mode_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &pupil_jitter_mode_param_def));
    pupil_jitter_mode_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          pupil_jitter_seed_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &pupil_jitter_seed_param_def));
    pupil_jitter_seed_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          projected_cells_mode_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &projected_cells_mode_param_def));
    projected_cells_mode_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          cell_coverage_bias_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &cell_coverage_bias_param_def));
    cell_coverage_bias_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          cell_edge_inset_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &cell_edge_inset_param_def));
    cell_edge_inset_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          view_mode_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &view_param_def));
    view_checked_out = (err == PF_Err_NONE);

    ERR(PF_CHECKOUT_PARAM(in_data,
                          mask_layer_param(),
                          in_data->current_time,
                          in_data->time_step,
                          in_data->time_scale,
                          &mask_layer_param_def));
    mask_layer_checked_out = (err == PF_Err_NONE);

    if (!err) {
        AeUiParameterState ui_state {};
        ui_state.legacy_lens_preset_index = legacy_lens_param.u.pd.value;
        ui_state.lens_manufacturer_index = manufacturer_param.u.pd.value;
        ui_state.lens_model_index = lens_model_param.u.pd.value;
        ui_state.use_sensor_size = use_sensor_param_def.u.bd.value != 0;
        ui_state.sensor_preset_index = sensor_preset_param_def.u.pd.value;
        ui_state.fov_h_deg = fov_h_param_def.u.fs_d.value;
        ui_state.auto_fov_v = auto_fov_v_param_def.u.bd.value != 0;
        ui_state.fov_v_deg = fov_v_param_def.u.fs_d.value;
        ui_state.sensor_width_mm = sensor_width_param_def.u.fs_d.value;
        ui_state.sensor_height_mm = sensor_height_param_def.u.fs_d.value;
        ui_state.focal_length_mm = focal_length_param_def.u.fs_d.value;
        ui_state.aperture_blades = aperture_blades_param_def.u.sd.value;
        ui_state.aperture_rotation_deg = aperture_rotation_param_def.u.fs_d.value;
        ui_state.flare_gain = flare_gain_param_def.u.fs_d.value;
        ui_state.sky_brightness = sky_brightness_param_def.u.fs_d.value;
        ui_state.threshold = threshold_param_def.u.fs_d.value;
        ui_state.ray_grid = ray_grid_param_def.u.sd.value;
        ui_state.downsample = downsample_param_def.u.sd.value;
        ui_state.max_sources = max_sources_param_def.u.sd.value;
        ui_state.cluster_radius_px = cluster_radius_param_def.u.sd.value;
        ui_state.ghost_blur = ghost_blur_param_def.u.fs_d.value;
        ui_state.ghost_blur_passes = ghost_blur_passes_param_def.u.sd.value;
        ui_state.ghost_cleanup_mode_index = ghost_cleanup_mode_param_def.u.pd.value;
        ui_state.haze_gain = haze_gain_param_def.u.fs_d.value;
        ui_state.haze_radius = haze_radius_param_def.u.fs_d.value;
        ui_state.haze_blur_passes = haze_blur_passes_param_def.u.sd.value;
        ui_state.starburst_gain = starburst_gain_param_def.u.fs_d.value;
        ui_state.starburst_scale = starburst_scale_param_def.u.fs_d.value;
        ui_state.spectral_samples_index = spectral_samples_param_def.u.pd.value;
        ui_state.adaptive_sampling_strength = adaptive_sampling_strength_param_def.u.fs_d.value;
        ui_state.footprint_radius_bias = footprint_radius_bias_param_def.u.fs_d.value;
        ui_state.footprint_clamp = footprint_clamp_param_def.u.fs_d.value;
        ui_state.max_adaptive_pair_grid = max_adaptive_pair_grid_param_def.u.sd.value;
        ui_state.pupil_jitter_mode_index = pupil_jitter_mode_param_def.u.pd.value;
        ui_state.pupil_jitter_seed = pupil_jitter_seed_param_def.u.sd.value;
        ui_state.projected_cells_mode_index = projected_cells_mode_param_def.u.pd.value;
        ui_state.cell_coverage_bias = cell_coverage_bias_param_def.u.fs_d.value;
        ui_state.cell_edge_inset = cell_edge_inset_param_def.u.fs_d.value;
        ui_state.view_mode_index = view_param_def.u.pd.value;

        if (!apply_ui_parameter_state(ui_state, out_state)) {
            err = PF_Err_BAD_CALLBACK_PARAM;
        }
    }

    if (view_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &view_param_def));
    }
    if (mask_layer_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &mask_layer_param_def));
    }
    if (max_adaptive_pair_grid_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &max_adaptive_pair_grid_param_def));
    }
    if (pupil_jitter_seed_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &pupil_jitter_seed_param_def));
    }
    if (pupil_jitter_mode_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &pupil_jitter_mode_param_def));
    }
    if (projected_cells_mode_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &projected_cells_mode_param_def));
    }
    if (cell_edge_inset_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &cell_edge_inset_param_def));
    }
    if (cell_coverage_bias_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &cell_coverage_bias_param_def));
    }
    if (footprint_clamp_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &footprint_clamp_param_def));
    }
    if (footprint_radius_bias_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &footprint_radius_bias_param_def));
    }
    if (adaptive_sampling_strength_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &adaptive_sampling_strength_param_def));
    }
    if (spectral_samples_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &spectral_samples_param_def));
    }
    if (starburst_scale_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &starburst_scale_param_def));
    }
    if (starburst_gain_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &starburst_gain_param_def));
    }
    if (haze_blur_passes_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &haze_blur_passes_param_def));
    }
    if (haze_radius_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &haze_radius_param_def));
    }
    if (haze_gain_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &haze_gain_param_def));
    }
    if (ghost_blur_passes_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &ghost_blur_passes_param_def));
    }
    if (ghost_cleanup_mode_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &ghost_cleanup_mode_param_def));
    }
    if (ghost_blur_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &ghost_blur_param_def));
    }
    if (downsample_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &downsample_param_def));
    }
    if (max_sources_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &max_sources_param_def));
    }
    if (cluster_radius_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &cluster_radius_param_def));
    }
    if (ray_grid_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &ray_grid_param_def));
    }
    if (threshold_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &threshold_param_def));
    }
    if (sky_brightness_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &sky_brightness_param_def));
    }
    if (flare_gain_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &flare_gain_param_def));
    }
    if (aperture_rotation_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &aperture_rotation_param_def));
    }
    if (aperture_blades_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &aperture_blades_param_def));
    }
    if (focal_length_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &focal_length_param_def));
    }
    if (sensor_height_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &sensor_height_param_def));
    }
    if (sensor_width_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &sensor_width_param_def));
    }
    if (fov_v_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &fov_v_param_def));
    }
    if (auto_fov_v_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &auto_fov_v_param_def));
    }
    if (fov_h_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &fov_h_param_def));
    }
    if (sensor_preset_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &sensor_preset_param_def));
    }
    if (use_sensor_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &use_sensor_param_def));
    }
    if (lens_model_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &lens_model_param));
    }
    if (manufacturer_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &manufacturer_param));
    }
    if (legacy_lens_checked_out) {
        ERR2(PF_CHECKIN_PARAM(in_data, &legacy_lens_param));
    }

    return err ? err : err2;
}

} // namespace

PF_Err PluginHandleSmartPreRender(PF_InData* in_data, PF_OutData*, void* extra)
{
    auto* render_extra = reinterpret_cast<PF_PreRenderExtra*>(extra);
    if (!in_data || !render_extra) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    // SmartFX caching only invalidates on dependencies declared during pre-render.
    // Touch the render params here so AE re-renders when view or flare controls change.
    AeParameterState state {};
    PF_Err err = build_render_state_from_checked_out_params(in_data, state);
    if (err != PF_Err_NONE) {
        return err;
    }

    PF_RenderRequest request = render_extra->input->output_request;
    request.rect.left = 0;
    request.rect.top = 0;
    request.rect.right = in_data->width;
    request.rect.bottom = in_data->height;

    PF_CheckoutResult input_result;
    AEFX_CLR_STRUCT(input_result);

    err = render_extra->cb->checkout_layer(
        in_data->effect_ref,
        PARAM_INPUT,
        kSmartInputCheckoutId,
        &request,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &input_result);
    if (err != PF_Err_NONE) {
        return err;
    }

    PF_CheckoutResult mask_result;
    AEFX_CLR_STRUCT(mask_result);
    const PF_Err mask_err = render_extra->cb->checkout_layer(
        in_data->effect_ref,
        mask_layer_param(),
        kSmartMaskCheckoutId,
        &request,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &mask_result);

    union_rects(input_result.result_rect, render_extra->output->result_rect);
    union_rects(input_result.max_result_rect, render_extra->output->max_result_rect);

    auto* rects = new SmartRenderLayerRects {};
    rects->input_rect = input_result.result_rect;
    if (mask_err == PF_Err_NONE) {
        rects->mask_rect = mask_result.result_rect;
        rects->has_mask = TRUE;
    }
    render_extra->output->pre_render_data = rects;
    render_extra->output->delete_pre_render_data_func = delete_smart_render_layer_rects;
    return PF_Err_NONE;
}

PF_Err PluginHandleSmartRender(PF_InData* in_data, PF_OutData* out_data, void* extra)
{
    auto* render_extra = reinterpret_cast<PF_SmartRenderExtra*>(extra);
    if (!in_data || !out_data || !render_extra) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;
    const auto* rects =
        reinterpret_cast<const SmartRenderLayerRects*>(render_extra->input->pre_render_data);
    const PF_LRect* mask_rect = (rects && rects->has_mask) ? &rects->mask_rect : nullptr;

    AeParameterState state {};
    ERR(build_render_state_from_checked_out_params(in_data, state));

    PF_EffectWorld* input_world = nullptr;
    PF_EffectWorld* mask_world = nullptr;
    PF_EffectWorld* output_world = nullptr;
    bool mask_checked_out = false;

    if (!err) {
        ERR(render_extra->cb->checkout_layer_pixels(in_data->effect_ref, kSmartInputCheckoutId, &input_world));
    }
    if (!err) {
        const PF_Err mask_err =
            render_extra->cb->checkout_layer_pixels(in_data->effect_ref, kSmartMaskCheckoutId, &mask_world);
        if (mask_err == PF_Err_NONE) {
            mask_checked_out = true;
        } else {
            mask_world = nullptr;
        }
    }
    if (!err) {
        ERR(render_extra->cb->checkout_output(in_data->effect_ref, &output_world));
    }
    if (!err) {
        ERR(render_checked_out_worlds(in_data, out_data, state, input_world, mask_world, mask_rect, output_world));
    }

    if (input_world) {
        ERR2(render_extra->cb->checkin_layer_pixels(in_data->effect_ref, kSmartInputCheckoutId));
    }
    if (mask_checked_out) {
        ERR2(render_extra->cb->checkin_layer_pixels(in_data->effect_ref, kSmartMaskCheckoutId));
    }

    return err ? err : err2;
}

PF_Err PluginHandleLegacyRender(PF_InData* in_data,
                                PF_OutData* out_data,
                                PF_ParamDef* params[],
                                PF_LayerDef* output)
{
    if (!in_data || !out_data || !params || !output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    AeUiParameterState ui_state {};
    if (!read_ui_state_from_params(params, ui_state)) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    AeParameterState state {};
    if (!apply_ui_parameter_state(ui_state, state)) {
        return PF_COPY(&params[PARAM_INPUT]->u.ld, output, nullptr, nullptr);
    }

    PF_EffectWorld* mask_world = nullptr;
    if (params[mask_layer_param()]) {
        mask_world = &params[mask_layer_param()]->u.ld;
    }

    return render_checked_out_worlds(in_data, out_data, state, &params[PARAM_INPUT]->u.ld, mask_world, nullptr, output);
}
