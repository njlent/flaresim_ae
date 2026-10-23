// Opens the Photoshop filter dialog outside Photoshop on a synthetic HDR
// image. With --capture <prefix> it saves window captures, clicks the preview
// to place the manual source, presses OK and prints the resulting settings.
//
// usage: flaresim_ps_dialog_harness [--capture <prefix>] [--wait-ms <ms>] [--settings <file>]

#include "ps_dialog.h"
#include "ps_settings.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

FloatImageBuffer make_scene(int width, int height)
{
    FloatImageBuffer image;
    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.width = width;
    image.height = height;
    image.r.resize(np);
    image.g.resize(np);
    image.b.resize(np);
    image.alpha.assign(np, 1.0f);
    for (int y = 0; y < height; ++y) {
        const float t = static_cast<float>(y) / height;
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            image.r[i] = 0.02f + 0.10f * (1.0f - t);
            image.g[i] = 0.03f + 0.14f * (1.0f - t);
            image.b[i] = 0.06f + 0.30f * (1.0f - t);
        }
    }
    auto sun = [&](int cx, int cy, int radius, float value) {
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (x < 0 || y < 0 || x >= width || y >= height || (x - cx) * (x - cx) + (y - cy) * (y - cy) > radius * radius) {
                    continue;
                }
                const std::size_t i = static_cast<std::size_t>(y) * width + x;
                image.r[i] = value;
                image.g[i] = value * 0.92f;
                image.b[i] = value * 0.8f;
            }
        }
    };
    sun(width * 7 / 10, height / 4, 6, 400.0f);
    sun(width / 5, height / 3, 2, 60.0f);
    return image;
}

HWND find_dialog(DWORD thread_id)
{
    HWND found = nullptr;
    EnumThreadWindows(thread_id, [](HWND hwnd, LPARAM lparam) -> BOOL {
        wchar_t title[64] {};
        GetWindowTextW(hwnd, title, 63);
        if (std::wcsncmp(title, L"FlareSim", 8) == 0 && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(lparam) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

HWND find_child(HWND parent, const wchar_t* cls)
{
    struct Query { const wchar_t* cls; HWND hwnd; } query {cls, nullptr};
    EnumChildWindows(parent, [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* q = reinterpret_cast<Query*>(lparam);
        wchar_t name[64] {};
        GetClassNameW(hwnd, name, 63);
        if (std::wcscmp(name, q->cls) == 0) {
            q->hwnd = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.hwnd;
}

bool capture(HWND hwnd, const std::string& path)
{
    RECT rc {};
    GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);

    BITMAPFILEHEADER file {};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(w * h * 4);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(bits), static_cast<std::streamsize>(w) * h * 4);

    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::string capture_prefix;
    std::string settings_path;
    int wait_ms = 6000;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--capture") == 0) {
            capture_prefix = argv[++i];
        } else if (std::strcmp(argv[i], "--wait-ms") == 0) {
            wait_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--settings") == 0) {
            settings_path = argv[++i];
        }
    }

    const FloatImageBuffer image = make_scene(1600, 900);
    PsDialogContext context;
    context.image = &image;
    context.linear_document = true;
    context.asset_root = FLARESIM_REPO_ROOT;
    context.document_description = "harness 1600x900 32-bit";

    AeUiParameterState state = ps_default_ui_state();
    if (!settings_path.empty()) {
        std::ifstream file(settings_path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!ps_deserialize_settings(text, state)) {
            std::fprintf(stderr, "bad settings file\n");
            return 1;
        }
    }
    std::thread driver;
    if (!capture_prefix.empty()) {
        const DWORD ui_thread = GetCurrentThreadId();
        driver = std::thread([=] {
            HWND dialog = nullptr;
            for (int i = 0; i < 100 && !dialog; ++i) {
                Sleep(100);
                dialog = find_dialog(ui_thread);
            }
            if (!dialog) {
                std::fprintf(stderr, "dialog did not appear\n");
                return;
            }
            Sleep(wait_ms);
            capture(dialog, capture_prefix + "-1.bmp");

            HWND preview = find_child(dialog, L"FlareSimPSPreview");
            if (preview) {
                RECT rc {};
                GetClientRect(preview, &rc);
                const LPARAM point = MAKELPARAM(rc.right * 3 / 10, rc.bottom * 6 / 10);
                SendMessageW(preview, WM_LBUTTONDOWN, MK_LBUTTON, point);
                SendMessageW(preview, WM_LBUTTONUP, 0, point);
            }
            Sleep(wait_ms);
            capture(dialog, capture_prefix + "-2.bmp");
            PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        });
    }

    const PsDialogResult result = ps_run_dialog(context, state);
    if (driver.joinable()) {
        driver.join();
    }
    std::printf("result=%s\n", result == PsDialogResult::Ok ? "ok" : result == PsDialogResult::Cancel ? "cancel" : "error");
    if (result == PsDialogResult::Ok) {
        std::printf("%s", ps_serialize_settings(state).c_str());
    }
    return result == PsDialogResult::Error ? 1 : 0;
}
