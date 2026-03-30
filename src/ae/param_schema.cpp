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

const BuiltinLensManufacturerDescriptor* legacy_lens_manufacturer()
{
    return builtin_lens_manufacturer(0);
}

bool is_default_grouped_lens_selection(const AeUiParameterState& ui_state)
{
    return ui_state.lens_manufacturer_index == default_lens_manufacturer_popup_index() &&
           ui_state.lens_model_index == default_lens_model_popup_index();
}

} // namespace

int lens_group_start_param(int manufacturer_index)
{
    return PARAM_LENS_MANUFACTURER + 1 + (manufacturer_index * LENS_PARAMS_PER_MANUFACTURER);
}

int lens_section_start_param()
{
    return PARAM_LENS_SECTION_START;
}

int lens_popup_param(int manufacturer_index)
{
    return lens_group_start_param(manufacturer_index) + 1;
}

int lens_group_end_param(int manufacturer_index)
{
    return lens_group_start_param(manufacturer_index) + 2;
}

int lens_section_end_param()
{
    return PARAM_LENS_MANUFACTURER + 1 +
           static_cast<int>(builtin_lens_manufacturer_count()) * LENS_PARAMS_PER_MANUFACTURER;
}

int flare_section_start_param()
{
    return lens_section_end_param() + 1;
}

int flare_gain_param()
{
    return flare_section_start_param() + 1;
}

int threshold_param()
{
    return flare_gain_param() + 1;
}

int ray_grid_param()
{
    return threshold_param() + 1;
}

int downsample_param()
{
    return ray_grid_param() + 1;
}

int flare_section_end_param()
{
    return downsample_param() + 1;
}

int view_mode_param()
{
    return flare_section_end_param() + 1;
}

int mask_layer_param()
{
    return view_mode_param() + 1;
}

int parameter_count()
{
    return mask_layer_param() + 1;
}

int lens_group_start_param_id(int manufacturer_index)
{
    return PARAM_ID_LENS_GROUP_BASE + (manufacturer_index * LENS_PARAMS_PER_MANUFACTURER);
}

int lens_popup_param_id(int manufacturer_index)
{
    return lens_group_start_param_id(manufacturer_index) + 1;
}

int lens_group_end_param_id(int manufacturer_index)
{
    return lens_group_start_param_id(manufacturer_index) + 2;
}

const char* default_builtin_lens_id()
{
    return "double-gauss";
}

int default_legacy_lens_popup_index()
{
    return lens_popup_index_for_builtin(default_builtin_lens_id());
}

int default_lens_manufacturer_popup_index()
{
    return lens_manufacturer_popup_index_for_builtin(default_builtin_lens_id());
}

int default_lens_model_popup_index()
{
    return lens_model_popup_index_for_builtin(default_builtin_lens_id());
}

std::string build_lens_preset_popup_string()
{
    const auto* manufacturer = legacy_lens_manufacturer();
    if (!manufacturer) {
        return {};
    }

    const auto* lenses = builtin_lenses_for_manufacturer(0);
    std::string popup;
    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += lenses[i].label;
    }
    return popup;
}

std::string build_lens_manufacturer_popup_string()
{
    std::string popup;
    for (std::size_t i = 0; i < builtin_lens_manufacturer_count(); ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += builtin_lens_manufacturers()[i].label;
    }
    return popup;
}

std::string build_lens_popup_string_for_manufacturer(int manufacturer_index)
{
    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(manufacturer_index));
    if (!manufacturer || !lenses) {
        return {};
    }

    std::string popup;
    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (!popup.empty()) {
            popup += '|';
        }
        popup += lenses[i].label;
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

    const auto* manufacturer = legacy_lens_manufacturer();
    const auto* lenses = builtin_lenses_for_manufacturer(0);
    if (!manufacturer || !lenses) {
        return 0;
    }

    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (std::string_view(lenses[i].id) == builtin_id) {
            return i + 1;
        }
    }
    return 0;
}

int lens_manufacturer_popup_index_for_builtin(const char* builtin_id)
{
    const auto* lens = find_builtin_lens(builtin_id);
    return lens ? (lens->manufacturer_index + 1) : 0;
}

int lens_model_popup_index_for_builtin(const char* builtin_id)
{
    const auto* lens = find_builtin_lens(builtin_id);
    if (!lens) {
        return 0;
    }

    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(lens->manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(lens->manufacturer_index));
    if (!manufacturer || !lenses) {
        return 0;
    }

    for (int i = 0; i < manufacturer->lens_count; ++i) {
        if (std::string_view(lenses[i].id) == builtin_id) {
            return i + 1;
        }
    }
    return 0;
}

bool legacy_lens_selection_from_popup(int popup_index, AeLensSelection& out_selection)
{
    const auto* manufacturer = legacy_lens_manufacturer();
    if (!manufacturer || popup_index < 1 || popup_index > manufacturer->lens_count) {
        return false;
    }

    const auto* lenses = builtin_lenses_for_manufacturer(0);
    const BuiltinLensDescriptor& lens = lenses[popup_index - 1];
    out_selection.source = AeLensSourceKind::Builtin;
    out_selection.builtin_id = lens.id;
    out_selection.external_path = nullptr;
    return true;
}

bool lens_selection_from_popup(int manufacturer_popup_index, int lens_popup_index, AeLensSelection& out_selection)
{
    if (manufacturer_popup_index < 1 ||
        manufacturer_popup_index > static_cast<int>(builtin_lens_manufacturer_count())) {
        return false;
    }

    const int manufacturer_index = manufacturer_popup_index - 1;
    const auto* manufacturer = builtin_lens_manufacturer(static_cast<std::size_t>(manufacturer_index));
    const auto* lenses = builtin_lenses_for_manufacturer(static_cast<std::size_t>(manufacturer_index));
    if (!manufacturer || !lenses || lens_popup_index < 1 || lens_popup_index > manufacturer->lens_count) {
        return false;
    }

    const BuiltinLensDescriptor& lens = lenses[lens_popup_index - 1];
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
    if (ui_state.ray_grid < 1 || ui_state.downsample < 1) {
        return false;
    }

    AeLensSelection grouped_lens {};
    AeLensSelection legacy_lens {};
    const bool selected_grouped_lens =
        lens_selection_from_popup(ui_state.lens_manufacturer_index, ui_state.lens_model_index, grouped_lens);
    const bool selected_legacy_lens =
        legacy_lens_selection_from_popup(ui_state.legacy_lens_preset_index, legacy_lens);

    if (!selected_grouped_lens && !selected_legacy_lens) {
        return false;
    }

    if (selected_legacy_lens &&
        ui_state.legacy_lens_preset_index != default_legacy_lens_popup_index() &&
        is_default_grouped_lens_selection(ui_state)) {
        out_state.lens = legacy_lens;
    } else if (selected_grouped_lens) {
        out_state.lens = grouped_lens;
    } else {
        return false;
    }

    AeOutputView view {};
    if (!output_view_from_popup(ui_state.view_mode_index, view)) {
        return false;
    }

    out_state.view = view;
    out_state.flare_gain = ui_state.flare_gain;
    out_state.threshold = ui_state.threshold;
    out_state.ray_grid = ui_state.ray_grid;
    out_state.downsample = ui_state.downsample;
    return true;
}
