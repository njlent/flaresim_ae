#include "ps_presets.h"

#include "ps_settings.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

fs::path path_from_utf8(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8_from_path(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path preset_path(const fs::path& folder, const std::string& name)
{
    return folder / path_from_utf8(name + kPsPresetExtension);
}

bool read_file(const fs::path& path, std::string& out_text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out_text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

fs::path ps_default_preset_folder()
{
#ifdef _WIN32
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata && *appdata) {
        return fs::path(appdata) / L"FlareSim" / L"presets";
    }
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return fs::path(home) / ".config" / "FlareSim" / "presets";
    }
#endif
    return {};
}

bool ps_valid_preset_name(const std::string& name)
{
    if (name.empty() || name.size() > 100) {
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            return false;
        }
        if (std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> ps_list_presets(const fs::path& folder)
{
    std::vector<std::pair<std::wstring, std::string>> found;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) {
        return {};
    }
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kPsPresetExtension) {
            continue;
        }
        const fs::path stem = it->path().stem();
        std::wstring key = stem.wstring();
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        found.emplace_back(std::move(key), utf8_from_path(stem));
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> names;
    names.reserve(found.size());
    for (auto& entry : found) {
        names.push_back(std::move(entry.second));
    }
    return names;
}

bool ps_preset_exists(const fs::path& folder, const std::string& name)
{
    std::error_code ec;
    return ps_valid_preset_name(name) && fs::is_regular_file(preset_path(folder, name), ec);
}

bool ps_load_preset(const fs::path& folder, const std::string& name, AeUiParameterState& out_state)
{
    if (!ps_valid_preset_name(name)) {
        return false;
    }
    std::string text;
    if (!read_file(preset_path(folder, name), text)) {
        return false;
    }
    AeUiParameterState state = ps_default_ui_state();
    if (!ps_deserialize_settings(text, state)) {
        return false;
    }
    out_state = state;
    return true;
}

bool ps_save_preset(const fs::path& folder, const std::string& name, const AeUiParameterState& state)
{
    if (folder.empty() || !ps_valid_preset_name(name)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    // Write a temporary file first so a failed save never leaves half a preset.
    const fs::path target = preset_path(folder, name);
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << ps_serialize_settings(state);
        if (!file) {
            return false;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool ps_delete_preset(const fs::path& folder, const std::string& name)
{
    if (!ps_valid_preset_name(name)) {
        return false;
    }
    std::error_code ec;
    return fs::remove(preset_path(folder, name), ec) && !ec;
}

std::string ps_find_matching_preset(const fs::path& folder, const AeUiParameterState& state)
{
    const std::string wanted = ps_serialize_settings(state);
    for (const std::string& name : ps_list_presets(folder)) {
        AeUiParameterState preset {};
        if (ps_load_preset(folder, name, preset) && ps_serialize_settings(preset) == wanted) {
            return name;
        }
    }
    return {};
}
