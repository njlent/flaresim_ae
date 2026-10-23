#pragma once

#include "frame_bridge.h"
#include "param_schema.h"

#include <string>

struct PsDialogContext
{
    void* parent_window = nullptr;
    // Whole document, colour premultiplied by transparency, in document units
    // (0..1 for 8/16-bit, linear scene values for 32-bit).
    const FloatImageBuffer* image = nullptr;
    bool linear_document = false;
    std::string asset_root;
    std::string document_description;
};

enum class PsDialogResult
{
    Ok,
    Cancel,
    Error,
};

// Modal settings dialog with an asynchronous preview. On Ok, state holds the
// edited settings; otherwise it is left untouched.
PsDialogResult ps_run_dialog(const PsDialogContext& context, AeUiParameterState& state);
