#pragma once

#include "param_schema.h"
#include "parameter_state.h"

#include <cstddef>
#include <string>
#include <vector>

// Host-agnostic description of every user-facing FlareSim control in the
// Photoshop filter. The dialog, the scripting descriptor and the last-used
// settings file are all driven from this one table, so it is the only place a
// new control has to be registered on the Photoshop side.

enum class PsParamKind
{
    Float,
    Int,
    Bool,
    Choice,
};

struct PsParamDesc
{
    const char* key;
    const char* label;
    const char* section;
    PsParamKind kind;
    float AeUiParameterState::*float_member;
    int AeUiParameterState::*int_member;
    bool AeUiParameterState::*bool_member;
    float slider_min;
    float slider_max;
    float hard_min;
    float hard_max;
    int decimals;
    std::string (*choice_labels)();
};

const PsParamDesc* ps_param_table();
std::size_t ps_param_count();
const PsParamDesc* ps_find_param(const char* key);

std::vector<std::string> ps_split_choice_labels(const std::string& popup);

double ps_get_param_value(const PsParamDesc& desc, const AeUiParameterState& state);
// Clamps to the hard range (and to the choice count for popups).
void ps_set_param_value(const PsParamDesc& desc, AeUiParameterState& state, double value);

AeUiParameterState ps_default_ui_state();

std::string ps_lens_id(const AeUiParameterState& state);
bool ps_set_lens_id(AeUiParameterState& state, const std::string& builtin_id);

// Text form: a "flaresim-ps <version>" header, then one key=value per line.
// Lens is stored by builtin id and popups by label so lens bundle or popup
// reordering does not silently change saved documents.
std::string ps_serialize_settings(const AeUiParameterState& state);
// Applies recognised keys on top of out_state. Unknown keys are ignored.
bool ps_deserialize_settings(const std::string& text, AeUiParameterState& out_state);

// Photoshop has no timeline, so the frame-based auto seed is always off.
bool ps_build_parameter_state(const AeUiParameterState& ui_state, AeParameterState& out_state);
