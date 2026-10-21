#pragma once

#include "lens.h"

#include <cstddef>

// Builtin lens prescriptions compiled into the EffectCraft module (no file access there).
const char* ec_lens_text(const char* relative_path);

// Loads the builtin lens with this id from the embedded bank.
bool ec_load_builtin_lens(const char* builtin_id, LensSystem& out_lens);

// The Lens popup's options as a JSON array body ("A","B",...), built by the generator, and
// how many there are (one per builtin lens, in table order).
const char* ec_lens_popup_options_json();
std::size_t ec_lens_popup_option_count();
