#pragma once

#include <cstddef>

struct BuiltinLensDescriptor
{
    const char* id;
    const char* label;
    const char* relative_path;
};

const BuiltinLensDescriptor* builtin_lenses();
std::size_t builtin_lens_count();
const BuiltinLensDescriptor* find_builtin_lens(const char* id);
