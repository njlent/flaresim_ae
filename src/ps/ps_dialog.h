#pragma once

#include "frame_bridge.h"
#include "param_schema.h"

#include <filesystem>
#include <string>

struct PsDialogContext
{
    void* parent_window = nullptr;
    // Whole document, colour premultiplied by transparency, in document units
    // (0..1 for 8/16-bit, linear scene values for 32-bit).
    const FloatImageBuffer* image = nullptr;
    bool linear_document = false;
    // Colour space of the document's pixels: what Color Space "Auto" renders
    // in and what the preview converts from for display.
    WorkingColorSpace document_space = WorkingColorSpace::LinearRec709;
    // Whether document_space came from a recognised profile rather than the
    // Rec.709 fallback.
    bool document_profile_known = false;
    std::string asset_root;
    std::string document_description;
    // Where named presets live; empty means ps_default_preset_folder().
    std::filesystem::path preset_folder;
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
