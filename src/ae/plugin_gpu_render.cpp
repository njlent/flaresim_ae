#include "plugin_entry.h"

#include "AEFX_SuiteHelper.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AE_EffectGPUSuites.h"

#include "frame_bridge.h"
#include "ghost_cuda.h"
#include "render_shared.h"

#include <string>

#if __has_include(<cuda_runtime.h>)
#include <cuda_runtime.h>
#define FLARESIM_AE_HAS_CUDA_SMART_RENDER 1
#else
#define FLARESIM_AE_HAS_CUDA_SMART_RENDER 0
#endif

namespace {

#if FLARESIM_AE_HAS_CUDA_SMART_RENDER
bool copy_gpu_world_to_gpu_world(const PF_EffectWorld& src_world,
                                 const void* src_pixels,
                                 PF_EffectWorld& dst_world,
                                 void* dst_pixels)
{
    if (!src_pixels || !dst_pixels ||
        src_world.width != dst_world.width ||
        src_world.height != dst_world.height) {
        return false;
    }

    const cudaError_t error = cudaMemcpy2D(dst_pixels,
                                           static_cast<std::size_t>(dst_world.rowbytes),
                                           src_pixels,
                                           static_cast<std::size_t>(src_world.rowbytes),
                                           static_cast<std::size_t>(src_world.width) * 4u * sizeof(float),
                                           static_cast<std::size_t>(src_world.height),
                                           cudaMemcpyDeviceToDevice);
    return error == cudaSuccess;
}
#endif

PF_Err render_checked_out_gpu_worlds(PF_InData* in_data,
                                     PF_OutData* out_data,
                                     const AeParameterState& state,
                                     PF_EffectWorld* input_world,
                                     PF_EffectWorld* mask_world,
                                     PF_EffectWorld* output_world)
{
    if (!in_data || !out_data || !input_world || !output_world) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

#if !FLARESIM_AE_HAS_CUDA_SMART_RENDER
    (void)state;
    (void)mask_world;
    return PF_Err_UNRECOGNIZED_PARAM_TYPE;
#else
    if (!cuda_ghost_renderer_compiled()) {
        return PF_Err_UNRECOGNIZED_PARAM_TYPE;
    }

    std::string asset_root;
    if (!resolve_plugin_asset_root(reinterpret_cast<const void*>(&PluginHandleSmartRenderGPU), asset_root)) {
        return PF_Err_BAD_CALLBACK_PARAM;
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
    if (format != PF_PixelFormat_GPU_BGRA128) {
        return PF_Err_UNRECOGNIZED_PARAM_TYPE;
    }

    AEFX_SuiteScoper<PF_GPUDeviceSuite1> gpu_suite(
        in_data,
        kPFGPUDeviceSuite,
        kPFGPUDeviceSuiteVersion1,
        out_data);

    void* input_device_pixels = nullptr;
    err = gpu_suite->GetGPUWorldData(in_data->effect_ref, input_world, &input_device_pixels);
    if (err != PF_Err_NONE) {
        return err;
    }

    void* output_device_pixels = nullptr;
    err = gpu_suite->GetGPUWorldData(in_data->effect_ref, output_world, &output_device_pixels);
    if (err != PF_Err_NONE) {
        return err;
    }

    void* mask_device_pixels = nullptr;
    if (mask_world) {
        const PF_Err mask_err = gpu_suite->GetGPUWorldData(in_data->effect_ref,
                                                           mask_world,
                                                           &mask_device_pixels);
        if (mask_err != PF_Err_NONE) {
            mask_world = nullptr;
            mask_device_pixels = nullptr;
        }
    }

    const bool rendered = render_frame_to_bgra128_device_buffer(
        asset_root,
        state,
        static_cast<const float*>(input_device_pixels),
        static_cast<float*>(output_device_pixels),
        input_world->width,
        input_world->height,
        input_world->rowbytes / static_cast<int>(sizeof(float)),
        output_world->rowbytes / static_cast<int>(sizeof(float)),
        mask_world ? static_cast<const float*>(mask_device_pixels) : nullptr,
        mask_world ? (mask_world->rowbytes / static_cast<int>(sizeof(float))) : 0);

    if (rendered) {
        return PF_Err_NONE;
    }

    if (!copy_gpu_world_to_gpu_world(*input_world,
                                     input_device_pixels,
                                     *output_world,
                                     output_device_pixels)) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return PF_Err_NONE;
#endif
}

} // namespace

PF_Err PluginHandleGpuDeviceSetup(PF_InData*, PF_OutData* out_data, void* extra)
{
    auto* gpu_extra = reinterpret_cast<PF_GPUDeviceSetupExtra*>(extra);
    if (!out_data || !gpu_extra || !gpu_extra->input || !gpu_extra->output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    gpu_extra->output->gpu_data = nullptr;
    if (cuda_ghost_renderer_compiled() &&
        gpu_extra->input->what_gpu == PF_GPU_Framework_CUDA) {
        out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    }

    return PF_Err_NONE;
}

PF_Err PluginHandleGpuDeviceSetdown(PF_InData*, PF_OutData*, void*)
{
    return PF_Err_NONE;
}

PF_Err PluginHandleSmartRenderGPU(PF_InData* in_data, PF_OutData* out_data, void* extra)
{
    auto* render_extra = reinterpret_cast<PF_SmartRenderExtra*>(extra);
    if (!in_data || !out_data || !render_extra || !render_extra->input || !render_extra->cb) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_Err err = PF_Err_NONE;
    PF_Err err2 = PF_Err_NONE;

    const auto* rects =
        reinterpret_cast<const SmartRenderContextData*>(render_extra->input->pre_render_data);

    AeParameterState state {};
    if (rects && rects->has_state) {
        state = rects->state;
    } else {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_EffectWorld* input_world = nullptr;
    PF_EffectWorld* mask_world = nullptr;
    PF_EffectWorld* output_world = nullptr;
    bool mask_checked_out = false;

    ERR(render_extra->cb->checkout_layer_pixels(in_data->effect_ref,
                                                kSmartInputCheckoutId,
                                                &input_world));
    if (!err) {
        const PF_Err mask_err = render_extra->cb->checkout_layer_pixels(in_data->effect_ref,
                                                                        kSmartMaskCheckoutId,
                                                                        &mask_world);
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
        ERR(render_checked_out_gpu_worlds(in_data,
                                          out_data,
                                          state,
                                          input_world,
                                          mask_world,
                                          output_world));
    }

    if (input_world) {
        ERR2(render_extra->cb->checkin_layer_pixels(in_data->effect_ref, kSmartInputCheckoutId));
    }
    if (mask_checked_out) {
        ERR2(render_extra->cb->checkin_layer_pixels(in_data->effect_ref, kSmartMaskCheckoutId));
    }

    return err ? err : err2;
}
