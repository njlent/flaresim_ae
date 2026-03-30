#pragma once

#include <string>

bool is_flaresim_asset_root(const std::string& candidate_root);

bool find_flaresim_asset_root(
    const std::string& anchor_path,
    std::string& out_asset_root
);
