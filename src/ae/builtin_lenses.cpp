#include "builtin_lenses.h"

#include <cstring>

namespace {

constexpr BuiltinLensDescriptor kBuiltinLenses[] = {
    {"arri-zeiss-master-prime-t1.3-50mm", "ARRI/Zeiss Master Prime T1.3 50mm", "assets/lenses/space55/arri-zeiss-master-prime-t1.3-50mm.lens"},
    {"canon-ef-200-400-f4", "Canon EF 200-400mm f/4", "assets/lenses/space55/canon-ef-200-400-f4.lens"},
    {"cooke-triplet", "Cooke Triplet", "assets/lenses/space55/cooketriplet.lens"},
    {"double-gauss", "Double Gauss", "assets/lenses/space55/doublegauss.lens"},
    {"test-lens", "Test Lens", "assets/lenses/space55/test.lens"},
};

} // namespace

const BuiltinLensDescriptor* builtin_lenses()
{
    return kBuiltinLenses;
}

std::size_t builtin_lens_count()
{
    return sizeof(kBuiltinLenses) / sizeof(kBuiltinLenses[0]);
}

const BuiltinLensDescriptor* find_builtin_lens(const char* id)
{
    if (!id) {
        return nullptr;
    }

    for (std::size_t i = 0; i < builtin_lens_count(); ++i) {
        if (std::strcmp(kBuiltinLenses[i].id, id) == 0) {
            return &kBuiltinLenses[i];
        }
    }
    return nullptr;
}
