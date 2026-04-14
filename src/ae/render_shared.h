#pragma once

#include "AE_Effect.h"

#include "parameter_state.h"

#include <string>

constexpr A_long kSmartInputCheckoutId = 1;
constexpr A_long kSmartMaskCheckoutId = 2;

struct SmartRenderContextData
{
    PF_LRect input_rect {};
    PF_LRect mask_rect {};
    PF_Boolean has_mask = 0;
    PF_Boolean has_state = 0;
    AeParameterState state {};
};

void delete_smart_render_context_data(void* data);
bool resolve_plugin_asset_root(const void* anchor, std::string& out_asset_root);
