#include "ec_lens_bank.h"

#include "builtin_lenses.h"

#include <cstddef>
#include <cstring>

namespace {

struct EcLensText
{
    const char* relative_path;
    const char* text;
};

#include "ec_lens_bank_generated.inc"

} // namespace

const char* ec_lens_text(const char* relative_path)
{
    if (!relative_path) {
        return nullptr;
    }

    for (const EcLensText& entry : kEcLensTexts) {
        if (std::strcmp(entry.relative_path, relative_path) == 0) {
            return entry.text;
        }
    }
    return nullptr;
}

const char* ec_lens_popup_options_json()
{
    return kEcLensPopupOptionsJson;
}

std::size_t ec_lens_popup_option_count()
{
    return kEcLensPopupOptionCount;
}

bool ec_load_builtin_lens(const char* builtin_id, LensSystem& out_lens)
{
    const BuiltinLensDescriptor* lens = find_builtin_lens(builtin_id);
    if (!lens) {
        return false;
    }

    const char* text = ec_lens_text(lens->relative_path);
    return text && out_lens.load_from_text(text, lens->relative_path);
}
