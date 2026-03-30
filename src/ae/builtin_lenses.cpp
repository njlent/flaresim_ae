#include "builtin_lenses.h"

#include <cstring>

namespace {

#include "builtin_lenses_generated.inc"

} // namespace

const BuiltinLensManufacturerDescriptor* builtin_lens_manufacturers()
{
    return kBuiltinLensManufacturers;
}

std::size_t builtin_lens_manufacturer_count()
{
    return sizeof(kBuiltinLensManufacturers) / sizeof(kBuiltinLensManufacturers[0]);
}

const BuiltinLensManufacturerDescriptor* builtin_lens_manufacturer(std::size_t index)
{
    if (index >= builtin_lens_manufacturer_count()) {
        return nullptr;
    }
    return &kBuiltinLensManufacturers[index];
}

const BuiltinLensDescriptor* builtin_lenses()
{
    return kBuiltinLenses;
}

std::size_t builtin_lens_count()
{
    return sizeof(kBuiltinLenses) / sizeof(kBuiltinLenses[0]);
}

const BuiltinLensDescriptor* builtin_lenses_for_manufacturer(std::size_t manufacturer_index)
{
    const auto* manufacturer = builtin_lens_manufacturer(manufacturer_index);
    if (!manufacturer) {
        return nullptr;
    }
    return &kBuiltinLenses[manufacturer->first_lens_index];
}

std::size_t builtin_lens_count_for_manufacturer(std::size_t manufacturer_index)
{
    const auto* manufacturer = builtin_lens_manufacturer(manufacturer_index);
    return manufacturer ? static_cast<std::size_t>(manufacturer->lens_count) : 0;
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
