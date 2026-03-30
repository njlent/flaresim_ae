#pragma once

#include <cstddef>

struct BuiltinLensManufacturerDescriptor
{
    const char* id;
    const char* label;
    int first_lens_index;
    int lens_count;
};

struct BuiltinLensDescriptor
{
    const char* id;
    const char* label;
    const char* relative_path;
    int manufacturer_index;
};

const BuiltinLensManufacturerDescriptor* builtin_lens_manufacturers();
std::size_t builtin_lens_manufacturer_count();
const BuiltinLensManufacturerDescriptor* builtin_lens_manufacturer(std::size_t index);

const BuiltinLensDescriptor* builtin_lenses();
std::size_t builtin_lens_count();
const BuiltinLensDescriptor* builtin_lenses_for_manufacturer(std::size_t manufacturer_index);
std::size_t builtin_lens_count_for_manufacturer(std::size_t manufacturer_index);

const BuiltinLensDescriptor* find_builtin_lens(const char* id);
