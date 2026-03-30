#pragma once

#include "lens.h"
#include "parameter_state.h"

#include <string>

bool resolve_lens_path(
    const AeLensSelection& selection,
    const std::string& asset_root,
    std::string& out_path
);

bool load_selected_lens(
    const AeLensSelection& selection,
    const std::string& asset_root,
    LensSystem& out_lens,
    std::string* out_path = nullptr
);
