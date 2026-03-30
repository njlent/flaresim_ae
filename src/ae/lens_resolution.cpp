#include "lens_resolution.h"

#include "builtin_lenses.h"

bool resolve_lens_path(
    const AeLensSelection& selection,
    const std::string& repo_root,
    std::string& out_path)
{
    out_path.clear();

    if (selection.source == AeLensSourceKind::Builtin) {
        const auto* builtin = find_builtin_lens(selection.builtin_id);
        if (!builtin) {
            return false;
        }
        out_path = repo_root + "/" + builtin->relative_path;
        return true;
    }

    if (selection.external_path && selection.external_path[0] != '\0') {
        out_path = selection.external_path;
        return true;
    }

    return false;
}

bool load_selected_lens(
    const AeLensSelection& selection,
    const std::string& repo_root,
    LensSystem& out_lens,
    std::string* out_path)
{
    std::string resolved;
    if (!resolve_lens_path(selection, repo_root, resolved)) {
        return false;
    }
    if (out_path) {
        *out_path = resolved;
    }
    return out_lens.load(resolved.c_str());
}
