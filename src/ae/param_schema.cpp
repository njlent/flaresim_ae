#include "param_schema.h"

#include "builtin_lenses.h"

#include <string_view>

namespace {

struct OutputViewDescriptor
{
    AeOutputView view;
    std::string_view label;
};

constexpr OutputViewDescriptor kOutputViews[] = {
    {AeOutputView::Composite, "Composite"},
    {AeOutputView::FlareOnly, "Flare Only"},
    {AeOutputView::BloomOnly, "Bloom Only"},
    {AeOutputView::Sources, "Sources"},
    {AeOutputView::Diagnostics, "Diagnostics"},
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

} // namespace

std::string build_lens_preset_popup_string()
{
    std::string popup;
    for (std::size_t i = 0; i < builtin_lens_count(); ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += builtin_lenses()[i].label;
    }
    return popup;
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

    for (std::size_t i = 0; i < builtin_lens_count(); ++i) {
        if (std::string_view(builtin_lenses()[i].id) == builtin_id) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

bool lens_selection_from_popup(int popup_index, AeLensSelection& out_selection)
{
    if (popup_index < 1 || popup_index > static_cast<int>(builtin_lens_count())) {
        return false;
    }

    const BuiltinLensDescriptor& lens = builtin_lenses()[popup_index - 1];
    out_selection.source = AeLensSourceKind::Builtin;
    out_selection.builtin_id = lens.id;
    out_selection.external_path = nullptr;
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
    if (ui_state.ray_grid < 1 || ui_state.downsample < 1 || ui_state.max_sources < 0) {
        return false;
    }

    AeLensSelection lens {};
    if (!lens_selection_from_popup(ui_state.lens_preset_index, lens)) {
        return false;
    }

    AeOutputView view {};
    if (!output_view_from_popup(ui_state.view_mode_index, view)) {
        return false;
    }

    out_state.lens = lens;
    out_state.view = view;
    out_state.flare_gain = ui_state.flare_gain;
    out_state.threshold = ui_state.threshold;
    out_state.ray_grid = ui_state.ray_grid;
    out_state.max_sources = ui_state.max_sources;
    out_state.downsample = ui_state.downsample;
    return true;
}
