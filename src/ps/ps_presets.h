#pragma once

#include "param_schema.h"

#include <filesystem>
#include <string>
#include <vector>

// Named presets are flaresim-ps settings files, one per preset, called
// <name>.flaresim in a single folder (%APPDATA%\FlareSim\presets by default).
// They are plain text, so they can be copied between machines or edited by
// hand. Names are UTF-8.

constexpr const char* kPsPresetExtension = ".flaresim";

std::filesystem::path ps_default_preset_folder();

// A usable file name: not empty, at most 100 characters, none of \ / : * ? " < > |,
// no control characters, and no leading or trailing space or trailing dot.
bool ps_valid_preset_name(const std::string& name);

// Preset names in the folder, sorted case-insensitively. Empty if the folder
// does not exist.
std::vector<std::string> ps_list_presets(const std::filesystem::path& folder);

bool ps_preset_exists(const std::filesystem::path& folder, const std::string& name);

// Loads onto the defaults, so keys missing from the file keep their defaults.
bool ps_load_preset(const std::filesystem::path& folder, const std::string& name, AeUiParameterState& out_state);

// Creates the folder when needed and overwrites an existing preset.
bool ps_save_preset(const std::filesystem::path& folder, const std::string& name, const AeUiParameterState& state);

bool ps_delete_preset(const std::filesystem::path& folder, const std::string& name);

// The first preset (in list order) whose settings equal state, or empty.
std::string ps_find_matching_preset(const std::filesystem::path& folder, const AeUiParameterState& state);
