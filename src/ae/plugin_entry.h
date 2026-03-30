#pragma once

#include "AEConfig.h"
#include "AE_Effect.h"
#include "entry.h"

#ifdef AE_OS_WIN
#define FLARESIM_AE_EXPORT __declspec(dllexport)
#else
#define FLARESIM_AE_EXPORT
#endif

PF_Err PluginHandleAbout(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleGlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleParamSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleSequenceSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleSequenceResetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleSequenceFlatten(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleSequenceSetdown(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
PF_Err PluginHandleSmartPreRender(PF_InData* in_data, PF_OutData* out_data, void* extra);
PF_Err PluginHandleSmartRender(PF_InData* in_data, PF_OutData* out_data, void* extra);
PF_Err PluginHandleLegacyRender(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output);
