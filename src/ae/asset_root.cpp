#include "asset_root.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr const char* kLensManifestPath = "assets/lenses/space55/manifest.json";

} // namespace

bool is_flaresim_asset_root(const std::string& candidate_root)
{
    if (candidate_root.empty()) {
        return false;
    }

    const fs::path manifest = fs::path(candidate_root) / kLensManifestPath;
    return fs::exists(manifest) && fs::is_regular_file(manifest);
}

bool find_flaresim_asset_root(
    const std::string& anchor_path,
    std::string& out_asset_root)
{
    out_asset_root.clear();
    if (anchor_path.empty()) {
        return false;
    }

    fs::path current = fs::path(anchor_path);
    if (fs::is_regular_file(current)) {
        current = current.parent_path();
    }

    while (!current.empty()) {
        if (is_flaresim_asset_root(current.string())) {
            out_asset_root = current.string();
            return true;
        }

        const fs::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return false;
}
