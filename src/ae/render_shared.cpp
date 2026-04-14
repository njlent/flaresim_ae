#include "render_shared.h"

#include "asset_root.h"

#ifdef AE_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

void delete_smart_render_context_data(void* data)
{
    auto* context = reinterpret_cast<SmartRenderContextData*>(data);
    delete context;
}

bool resolve_plugin_asset_root(const void* anchor, std::string& out_asset_root)
{
    out_asset_root.clear();

#ifdef FLARESIM_REPO_ROOT
    if (is_flaresim_asset_root(FLARESIM_REPO_ROOT)) {
        out_asset_root = FLARESIM_REPO_ROOT;
        return true;
    }
#endif

#ifdef AE_OS_WIN
    HMODULE module = nullptr;
    if (anchor &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(anchor),
                           &module)) {
        char module_path[MAX_PATH] {};
        const DWORD module_path_len = GetModuleFileNameA(module, module_path, MAX_PATH);
        if (module_path_len > 0 &&
            find_flaresim_asset_root(std::string(module_path, module_path_len), out_asset_root)) {
            return true;
        }
    }
#endif

    return false;
}
