#include "plugin_entry.h"
#include "plugin_version.h"

#include "AEFX_SuiteHelper.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"

#include "ghost_cuda.h"

#include <cstdio>

PF_Err PluginHandleAbout(PF_InData*, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    std::snprintf(out_data->return_msg,
                  sizeof(out_data->return_msg),
                  "%s %d.%d.%d\rAfter Effects lens flare renderer backed by the shared FlareSim core.",
                  FLARESIM_AE_EFFECT_NAME,
                  FLARESIM_AE_VERSION_MAJOR,
                  FLARESIM_AE_VERSION_MINOR,
                  FLARESIM_AE_VERSION_PATCH);
    return PF_Err_NONE;
}

PF_Err PluginHandleGlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef*[], PF_LayerDef*)
{
    PF_Err err = PF_Err_NONE;
    out_data->my_version = FLARESIM_AE_PIPL_VERSION;

    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    if (cuda_ghost_renderer_compiled()) {
        out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;

        AEFX_SuiteScoper<PF_PixelFormatSuite2> pixel_format_suite(
            in_data,
            kPFPixelFormatSuite,
            kPFPixelFormatSuiteVersion2,
            out_data);
        ERR(pixel_format_suite->PF_ClearSupportedPixelFormats(in_data->effect_ref));
        ERR(pixel_format_suite->PF_AddSupportedPixelFormat(in_data->effect_ref,
                                                           PF_PixelFormat_ARGB128));
        ERR(pixel_format_suite->PF_AddSupportedPixelFormat(in_data->effect_ref,
                                                           PF_PixelFormat_ARGB64));
        ERR(pixel_format_suite->PF_AddSupportedPixelFormat(in_data->effect_ref,
                                                           PF_PixelFormat_ARGB32));
        ERR(pixel_format_suite->PF_AddSupportedPixelFormat(in_data->effect_ref,
                                                           PF_PixelFormat_GPU_BGRA128));
    }
    return err;
}
