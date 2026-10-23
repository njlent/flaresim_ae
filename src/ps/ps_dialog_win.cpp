#include "ps_dialog.h"

#include "builtin_lenses.h"
#include "ps_image.h"
#include "ps_settings.h"
#include "ps_version.h"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr UINT kPreviewReadyMessage = WM_APP + 1;

constexpr int kIdManufacturer = 1100;
constexpr int kIdLens = 1101;
constexpr int kIdPreviewEnable = 1102;
constexpr int kIdPreviewResolution = 1103;
constexpr int kIdDefaults = 1104;
constexpr int kIdStatus = 1105;
constexpr int kIdParamBase = 2000;
constexpr int kIdsPerParam = 4;

enum ParamRole
{
    RoleLabel = 0,
    RoleTrack = 1,
    RoleValue = 2,
};

constexpr int kFloatTrackSteps = 1000;

constexpr const wchar_t* kPanelClass = L"FlareSimPSPanel";
constexpr const wchar_t* kPreviewClass = L"FlareSimPSPreview";

enum class PreviewResolution
{
    Fit = 0,
    Half = 1,
    Full = 2,
};

std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring window_text(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<std::size_t>(std::max(length, 0)) + 1u, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(std::max(length, 0)));
    return text;
}

// Locale-independent so a host that called setlocale (e.g. German decimal
// commas) cannot change how values are shown or parsed.
std::wstring format_value(const PsParamDesc& desc, double value)
{
    char buffer[64] {};
    std::to_chars_result result {};
    if (desc.kind == PsParamKind::Float) {
        result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value, std::chars_format::fixed, desc.decimals);
    } else {
        result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, static_cast<long long>(value));
    }
    return widen(std::string(buffer, result.ptr));
}

bool parse_value(const std::wstring& text, double& out_value)
{
    std::string ascii = narrow(text);
    std::replace(ascii.begin(), ascii.end(), ',', '.');
    const auto first = ascii.find_first_not_of(" \t");
    const auto last = ascii.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    ascii = ascii.substr(first, last - first + 1);
    const char* begin = ascii.data();
    if (*begin == '+') {
        ++begin;
    }
    const char* end = ascii.data() + ascii.size();
    const auto result = std::from_chars(begin, end, out_value);
    return result.ec == std::errc() && result.ptr == end;
}

bool param_enabled(const PsParamDesc& desc, const AeUiParameterState& s)
{
    const std::string_view key = desc.key;
    if (key == "fov_h") {
        return !s.use_sensor_size;
    }
    if (key == "fov_v") {
        return !s.use_sensor_size && !s.auto_fov_v;
    }
    if (key == "auto_fov_v") {
        return !s.use_sensor_size;
    }
    if (key == "sensor_preset" || key == "focal_length") {
        return s.use_sensor_size;
    }
    if (key == "sensor_width" || key == "sensor_height") {
        return s.use_sensor_size && s.sensor_preset_index == 1;
    }
    if (key.rfind("source_", 0) == 0 && key != "source_cap") {
        return s.manual_source_enabled;
    }
    if (key.rfind("preview_", 0) == 0 && key != "preview_mode") {
        return s.preview_mode;
    }
    if (key == "spectral_seed") {
        return s.spectral_jitter_mode_index != 1;
    }
    if (key == "jitter_seed") {
        return s.pupil_jitter_mode_index != 1;
    }
    if (key == "ghost_blur" || key == "ghost_blur_passes") {
        return s.ghost_cleanup_mode_index != 2;
    }
    return true;
}

struct ParamRow
{
    const PsParamDesc* desc = nullptr;
    HWND label = nullptr;
    HWND track = nullptr;
    HWND value = nullptr;
};

struct PreviewRequest
{
    AeUiParameterState state {};
    PreviewResolution resolution = PreviewResolution::Fit;
    int display_width = 0;
    int display_height = 0;
    std::uint64_t generation = 0;
};

struct PreviewResult
{
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
    std::uint64_t generation = 0;
    std::wstring status;
};

class Dialog;

// Renders previews off the UI thread. Only the newest request matters, so a
// request that arrives mid-render simply replaces the pending one.
class PreviewWorker
{
public:
    PreviewWorker(const PsDialogContext& context, HWND notify) : context_(context), notify_(notify)
    {
        thread_ = std::thread([this] { run(); });
    }

    ~PreviewWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void submit(const PreviewRequest& request)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = request;
            has_pending_ = true;
        }
        wake_.notify_all();
    }

    bool take_result(PreviewResult& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_result_) {
            return false;
        }
        out = std::move(result_);
        has_result_ = false;
        return true;
    }

private:
    int proxy_factor(const PreviewRequest& request) const
    {
        const FloatImageBuffer& image = *context_.image;
        switch (request.resolution) {
            case PreviewResolution::Full:
                return 1;
            case PreviewResolution::Half:
                return std::min(2, std::max(1, std::min(image.width, image.height)));
            case PreviewResolution::Fit:
            default: {
                const int fx = request.display_width > 0 ? image.width / request.display_width : 1;
                const int fy = request.display_height > 0 ? image.height / request.display_height : 1;
                return std::max(1, std::min(fx, fy));
            }
        }
    }

    bool newer_request_pending()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return has_pending_ || quit_;
    }

    void publish(PreviewResult&& result)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(result);
            has_result_ = true;
        }
        PostMessageW(notify_, kPreviewReadyMessage, 0, 0);
    }

    // Says how many image highlights became flare sources, and when none did,
    // how the brightest pixel compares to Threshold. A flare from only the
    // manual source is otherwise easy to mistake for the filter ignoring the image.
    std::wstring describe_sources(const AeUiParameterState& state, std::size_t total) const
    {
        const std::size_t manual = state.manual_source_enabled ? 1u : 0u;
        const std::size_t image_sources = total > manual ? total - manual : 0u;
        if (image_sources == 0) {
            return L"No image sources: brightest pixel " + format_number(peak_luminance_) +
                   L" vs Threshold " + format_number(state.threshold) +
                   (context_.linear_document ? L"" : L" (8/16-bit images peak at 1.0)");
        }
        return std::to_wstring(image_sources) + (image_sources == 1 ? L" image source" : L" image sources") +
               (manual ? L" + manual" : L"");
    }

    static std::wstring format_number(double value)
    {
        char buffer[48] {};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value, std::chars_format::fixed,
                                          value >= 100.0 ? 0 : 2);
        return widen(std::string(buffer, result.ptr));
    }

    void run()
    {
        const FloatImageBuffer& image = *context_.image;
        for (std::size_t i = 0; i < image.r.size(); ++i) {
            peak_luminance_ = std::max(peak_luminance_, 0.2126f * image.r[i] + 0.7152f * image.g[i] + 0.0722f * image.b[i]);
        }

        for (;;) {
            PreviewRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return quit_ || has_pending_; });
                if (quit_) {
                    return;
                }
                request = pending_;
                has_pending_ = false;
            }

            PreviewResult result;
            result.generation = request.generation;
            const auto started = std::chrono::steady_clock::now();

            AeParameterState state {};
            if (!ps_build_parameter_state(request.state, state)) {
                result.status = L"Invalid settings";
                publish(std::move(result));
                continue;
            }

            const int factor = proxy_factor(request);
            const FloatImageBuffer* input = context_.image;
            if (factor > 1) {
                if (proxy_factor_ != factor || proxy_threshold_ != state.threshold) {
                    ps_build_preview_proxy(*context_.image, factor, state.threshold, proxy_);
                    proxy_factor_ = factor;
                    proxy_threshold_ = state.threshold;
                }
                input = &proxy_;
                ps_scale_state_for_proxy(state, factor);
            }

            const bool ok = render_frame_to_float_image(context_.asset_root, state, *input, output_);
            if (newer_request_pending()) {
                continue;
            }

            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            if (!ok) {
                result.status = L"Preview render failed (lens assets missing?)";
                publish(std::move(result));
                continue;
            }

            ps_make_display_bitmap(output_, context_.linear_document, request.display_width,
                                   request.display_height, result.pixels);
            result.width = request.display_width;
            result.height = request.display_height;
            result.status = describe_sources(request.state, last_render_source_count()) + L" · Preview " +
                            std::to_wstring(input->width) + L"×" + std::to_wstring(input->height) +
                            (factor > 1 ? L" (1/" + std::to_wstring(factor) + L")" : std::wstring(L" (full)")) +
                            L" · " + std::to_wstring(elapsed.count()) + L" ms";
            publish(std::move(result));
        }
    }

    const PsDialogContext& context_;
    HWND notify_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool quit_ = false;
    bool has_pending_ = false;
    PreviewRequest pending_ {};
    bool has_result_ = false;
    PreviewResult result_ {};

    // Worker-thread only.
    FloatImageBuffer proxy_;
    int proxy_factor_ = 0;
    float proxy_threshold_ = -1.0f;
    FloatImageBuffer output_;
    float peak_luminance_ = 0.0f;
};

HINSTANCE module_instance()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module_instance),
                       &module);
    return module;
}

class Dialog
{
public:
    Dialog(const PsDialogContext& context, const AeUiParameterState& state) : context_(context), working_(state) {}

    PsDialogResult run()
    {
        if (!register_classes()) {
            return PsDialogResult::Error;
        }

        // Empty in-memory template; every control is created in WM_INITDIALOG.
        std::vector<WORD> buffer(64, 0);
        auto* tmpl = reinterpret_cast<DLGTEMPLATE*>(buffer.data());
        tmpl->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | DS_MODALFRAME |
                      DS_SETFONT | WS_CLIPCHILDREN;
        tmpl->cx = 600;
        tmpl->cy = 360;
        WORD* extra = reinterpret_cast<WORD*>(tmpl + 1);
        *extra++ = 0; // menu
        *extra++ = 0; // class
        const wchar_t title[] = L"FlareSim";
        std::memcpy(extra, title, sizeof(title));
        extra += sizeof(title) / sizeof(wchar_t);
        *extra++ = 9; // point size
        const wchar_t face[] = L"Segoe UI";
        std::memcpy(extra, face, sizeof(face));

        const INT_PTR code = DialogBoxIndirectParamW(module_instance(),
                                                     tmpl,
                                                     static_cast<HWND>(context_.parent_window),
                                                     &Dialog::dialog_proc,
                                                     reinterpret_cast<LPARAM>(this));
        if (code == -1) {
            return PsDialogResult::Error;
        }
        return code == IDOK ? PsDialogResult::Ok : PsDialogResult::Cancel;
    }

    const AeUiParameterState& state() const { return working_; }

private:
    static bool register_classes()
    {
        INITCOMMONCONTROLSEX icc {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

        const HINSTANCE instance = module_instance();
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));

        if (!GetClassInfoExW(instance, kPanelClass, &wc)) {
            wc.lpfnWndProc = &Dialog::panel_proc;
            wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
            wc.lpszClassName = kPanelClass;
            if (!RegisterClassExW(&wc)) {
                return false;
            }
        }

        WNDCLASSEXW pc {};
        pc.cbSize = sizeof(pc);
        pc.hInstance = instance;
        if (!GetClassInfoExW(instance, kPreviewClass, &pc)) {
            pc.lpfnWndProc = &Dialog::preview_proc;
            pc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32515));
            pc.hbrBackground = nullptr;
            pc.lpszClassName = kPreviewClass;
            if (!RegisterClassExW(&pc)) {
                return false;
            }
        }
        return true;
    }

    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    static Dialog* from(HWND hwnd) { return reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

    static INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_INITDIALOG) {
            auto* self = reinterpret_cast<Dialog*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
            self->on_init();
            return FALSE;
        }

        Dialog* self = from(hwnd);
        if (!self) {
            return FALSE;
        }

        switch (message) {
            case WM_COMMAND:
                return self->on_command(LOWORD(wparam), HIWORD(wparam));
            case WM_SIZE:
                self->layout();
                return TRUE;
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
                info->ptMinTrackSize.x = self->scale(820);
                info->ptMinTrackSize.y = self->scale(480);
                return TRUE;
            }
            case WM_DPICHANGED: {
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                self->dpi_ = HIWORD(wparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                             suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
                return TRUE;
            }
            case kPreviewReadyMessage:
                self->on_preview_ready();
                return TRUE;
            case WM_DESTROY:
                self->worker_.reset();
                return FALSE;
            default:
                return FALSE;
        }
    }

    static LRESULT CALLBACK panel_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        Dialog* self = from(hwnd);
        if (self) {
            switch (message) {
                case WM_COMMAND:
                    self->on_panel_command(LOWORD(wparam), HIWORD(wparam), reinterpret_cast<HWND>(lparam));
                    return 0;
                case WM_HSCROLL:
                    if (lparam) {
                        self->on_track(reinterpret_cast<HWND>(lparam));
                    }
                    return 0;
                case WM_VSCROLL:
                    self->on_panel_scroll(LOWORD(wparam));
                    return 0;
                case WM_MOUSEWHEEL:
                    self->scroll_panel_by(-GET_WHEEL_DELTA_WPARAM(wparam) * self->scale(48) / WHEEL_DELTA);
                    return 0;
                case WM_SIZE:
                    self->update_panel_scrollbar();
                    return 0;
                default:
                    break;
            }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    static LRESULT CALLBACK preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        Dialog* self = from(hwnd);
        if (self) {
            switch (message) {
                case WM_PAINT:
                    self->paint_preview();
                    return 0;
                case WM_ERASEBKGND:
                    return 1;
                case WM_LBUTTONDOWN:
                    SetCapture(hwnd);
                    self->dragging_ = true;
                    self->place_source(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    return 0;
                case WM_MOUSEMOVE:
                    if (self->dragging_ && (wparam & MK_LBUTTON)) {
                        self->place_source(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    }
                    return 0;
                case WM_LBUTTONUP:
                case WM_CAPTURECHANGED:
                    if (self->dragging_) {
                        self->dragging_ = false;
                        if (message == WM_LBUTTONUP) {
                            ReleaseCapture();
                        }
                    }
                    return 0;
                case WM_SIZE:
                    self->request_preview();
                    return 0;
                default:
                    break;
            }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    HWND create(const wchar_t* cls, const wchar_t* text, DWORD style, HWND parent, int id, DWORD ex_style = 0)
    {
        HWND child = CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), module_instance(), nullptr);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        return child;
    }

    void create_fonts()
    {
        NONCLIENTMETRICSW metrics {};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_);
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
        LOGFONTW bold = metrics.lfMessageFont;
        bold.lfWeight = FW_SEMIBOLD;
        bold_font_ = CreateFontIndirectW(&bold);
    }

    void on_init()
    {
        dpi_ = static_cast<int>(GetDpiForWindow(hwnd_));
        if (dpi_ <= 0) {
            dpi_ = 96;
        }
        create_fonts();

        const std::wstring title = L"FlareSim " + widen(FLARESIM_PS_VERSION_STRING) +
                                   (context_.document_description.empty()
                                        ? std::wstring()
                                        : L" — " + widen(context_.document_description));
        SetWindowTextW(hwnd_, title.c_str());

        preview_ = create(kPreviewClass, L"", WS_BORDER, hwnd_, 0);
        SetWindowLongPtrW(preview_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        lens_label_ = create(WC_STATICW, L"Lens", SS_LEFT, hwnd_, 0);
        SendMessageW(lens_label_, WM_SETFONT, reinterpret_cast<WPARAM>(bold_font_), FALSE);
        manufacturer_ = create(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, hwnd_, kIdManufacturer);
        lens_ = create(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, hwnd_, kIdLens);
        for (const std::string& label : ps_split_choice_labels(build_lens_manufacturer_popup_string())) {
            SendMessageW(manufacturer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(label).c_str()));
        }

        panel_ = CreateWindowExW(WS_EX_CONTROLPARENT, kPanelClass, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 10, 10,
                                 hwnd_, nullptr, module_instance(), nullptr);
        SetWindowLongPtrW(panel_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        create_param_rows();

        preview_enable_ = create(WC_BUTTONW, L"Preview", BS_AUTOCHECKBOX | WS_TABSTOP, hwnd_, kIdPreviewEnable);
        Button_SetCheck(preview_enable_, BST_CHECKED);
        preview_resolution_ = create(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, hwnd_, kIdPreviewResolution);
        for (const wchar_t* label : {L"Fit to preview", L"Half resolution", L"Full resolution"}) {
            SendMessageW(preview_resolution_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        SendMessageW(preview_resolution_, CB_SETCURSEL, 0, 0);
        status_ = create(WC_STATICW, L"", SS_LEFT | SS_ENDELLIPSIS, hwnd_, kIdStatus);
        defaults_ = create(WC_BUTTONW, L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, hwnd_, kIdDefaults);
        ok_ = create(WC_BUTTONW, L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, hwnd_, IDOK);
        cancel_ = create(WC_BUTTONW, L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, hwnd_, IDCANCEL);

        sync_controls_from_state();

        // Size to ~80% of the monitor work area, centred on the host window.
        HMONITOR monitor = MonitorFromWindow(context_.parent_window ? static_cast<HWND>(context_.parent_window) : hwnd_,
                                             MONITOR_DEFAULTTONEAREST);
        MONITORINFO info {sizeof(info)};
        GetMonitorInfoW(monitor, &info);
        const RECT work = info.rcWork;
        const int width = std::min(scale(1280), static_cast<int>((work.right - work.left) * 0.85));
        const int height = std::min(scale(800), static_cast<int>((work.bottom - work.top) * 0.85));
        SetWindowPos(hwnd_, nullptr, work.left + ((work.right - work.left) - width) / 2,
                     work.top + ((work.bottom - work.top) - height) / 2, width, height, SWP_NOZORDER);

        worker_ = std::make_unique<PreviewWorker>(context_, hwnd_);
        layout();
        request_preview();
        SetFocus(ok_);
    }

    void create_param_rows()
    {
        const char* section = nullptr;
        for (std::size_t i = 0; i < ps_param_count(); ++i) {
            const PsParamDesc& desc = ps_param_table()[i];
            if (!section || std::strcmp(section, desc.section) != 0) {
                section = desc.section;
                HWND header = create(WC_STATICW, widen(section).c_str(), SS_LEFT, panel_, 0);
                SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(bold_font_), FALSE);
                row_order_.push_back({nullptr, header, nullptr, nullptr});
            }

            const int base = kIdParamBase + static_cast<int>(i) * kIdsPerParam;
            ParamRow row;
            row.desc = &desc;
            switch (desc.kind) {
                case PsParamKind::Float:
                case PsParamKind::Int:
                    row.label = create(WC_STATICW, widen(desc.label).c_str(), SS_LEFT | SS_ENDELLIPSIS, panel_, base + RoleLabel);
                    row.track = create(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, panel_, base + RoleTrack);
                    row.value = create(WC_EDITW, L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP, panel_, base + RoleValue,
                                       WS_EX_CLIENTEDGE);
                    if (desc.kind == PsParamKind::Float) {
                        SendMessageW(row.track, TBM_SETRANGEMIN, FALSE, 0);
                        SendMessageW(row.track, TBM_SETRANGEMAX, FALSE, kFloatTrackSteps);
                        SendMessageW(row.track, TBM_SETPAGESIZE, 0, kFloatTrackSteps / 20);
                    } else {
                        SendMessageW(row.track, TBM_SETRANGEMIN, FALSE, static_cast<LPARAM>(desc.slider_min));
                        SendMessageW(row.track, TBM_SETRANGEMAX, FALSE, static_cast<LPARAM>(desc.slider_max));
                        SendMessageW(row.track, TBM_SETPAGESIZE, 0,
                                     std::max<LPARAM>(1, static_cast<LPARAM>((desc.slider_max - desc.slider_min) / 20)));
                    }
                    break;
                case PsParamKind::Bool:
                    row.value = create(WC_BUTTONW, widen(desc.label).c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, panel_,
                                       base + RoleValue);
                    break;
                case PsParamKind::Choice:
                    row.label = create(WC_STATICW, widen(desc.label).c_str(), SS_LEFT | SS_ENDELLIPSIS, panel_, base + RoleLabel);
                    row.value = create(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, panel_,
                                       base + RoleValue);
                    for (const std::string& label : ps_split_choice_labels(desc.choice_labels())) {
                        SendMessageW(row.value, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(label).c_str()));
                    }
                    break;
            }
            rows_.push_back(row);
            row_order_.push_back(row);
        }
    }

    void layout()
    {
        if (!panel_) {
            return;
        }
        RECT client {};
        GetClientRect(hwnd_, &client);
        const int margin = scale(10);
        const int gap = scale(8);
        const int panel_width = scale(450);
        const int bottom_height = scale(28);
        const int combo_height = scale(24);
        const int right_x = client.right - margin - panel_width;
        const int bottom_y = client.bottom - margin - bottom_height;

        HDWP batch = BeginDeferWindowPos(16);
        auto place = [&](HWND child, int x, int y, int w, int h) {
            batch = DeferWindowPos(batch, child, nullptr, x, y, std::max(w, 0), std::max(h, 0), SWP_NOZORDER | SWP_NOACTIVATE);
        };

        place(preview_, margin, margin, right_x - gap - margin, bottom_y - gap - margin);

        int y = margin;
        place(lens_label_, right_x, y + scale(3), panel_width, scale(20));
        y += scale(24);
        place(manufacturer_, right_x, y, scale(150), scale(300));
        place(lens_, right_x + scale(156), y, panel_width - scale(156), scale(400));
        y += combo_height + gap;
        place(panel_, right_x, y, panel_width, bottom_y - gap - y);

        const int button_width = scale(84);
        int x = client.right - margin - button_width;
        place(cancel_, x, bottom_y, button_width, bottom_height);
        x -= button_width + gap;
        place(ok_, x, bottom_y, button_width, bottom_height);
        x -= button_width + gap;
        place(defaults_, x, bottom_y, button_width, bottom_height);

        place(preview_enable_, margin, bottom_y + scale(4), scale(80), scale(20));
        place(preview_resolution_, margin + scale(86), bottom_y + scale(2), scale(140), scale(200));
        place(status_, margin + scale(236), bottom_y + scale(6), std::max(0, x - gap - margin - scale(236)), scale(20));
        EndDeferWindowPos(batch);

        layout_panel();
    }

    void layout_panel()
    {
        RECT client {};
        GetClientRect(panel_, &client);
        const int width = client.right;
        const int pad = scale(8);
        const int label_width = scale(150);
        const int value_width = scale(78);
        const int row_height = scale(26);
        const int control_height = scale(22);
        const int track_x = pad + label_width;
        const int value_x = std::max(track_x + scale(60), width - pad - value_width);
        const int track_width = value_x - track_x - scale(4);

        int y = pad - scroll_pos_;
        bool first = true;
        HDWP batch = BeginDeferWindowPos(static_cast<int>(row_order_.size() * 3));
        auto place = [&](HWND child, int x, int yy, int w, int h) {
            if (child) {
                batch = DeferWindowPos(batch, child, nullptr, x, yy, std::max(w, 0), h, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        };

        for (const ParamRow& row : row_order_) {
            if (!row.desc) {
                if (!first) {
                    y += scale(8);
                }
                place(row.label, pad, y + scale(4), width - pad * 2, scale(20));
                y += row_height;
                first = false;
                continue;
            }
            switch (row.desc->kind) {
                case PsParamKind::Float:
                case PsParamKind::Int:
                    place(row.label, pad, y + scale(4), label_width - scale(4), scale(18));
                    place(row.track, track_x, y + scale(1), track_width, control_height);
                    place(row.value, value_x, y + scale(1), value_width, control_height);
                    break;
                case PsParamKind::Bool:
                    place(row.value, track_x, y + scale(2), width - track_x - pad, scale(20));
                    break;
                case PsParamKind::Choice:
                    place(row.label, pad, y + scale(4), label_width - scale(4), scale(18));
                    place(row.value, track_x, y + scale(1), width - track_x - pad, scale(300));
                    break;
            }
            y += row_height;
        }
        EndDeferWindowPos(batch);

        content_height_ = y + scroll_pos_ + pad;
        update_panel_scrollbar();
    }

    void update_panel_scrollbar()
    {
        RECT client {};
        GetClientRect(panel_, &client);
        const int page = client.bottom;
        const int max_scroll = std::max(0, content_height_ - page);
        if (scroll_pos_ > max_scroll) {
            scroll_panel_by(max_scroll - scroll_pos_);
        }
        SCROLLINFO info {sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0;
        info.nMax = std::max(0, content_height_ - 1);
        info.nPage = static_cast<UINT>(std::max(page, 0));
        info.nPos = scroll_pos_;
        SetScrollInfo(panel_, SB_VERT, &info, TRUE);
    }

    void scroll_panel_by(int delta)
    {
        RECT client {};
        GetClientRect(panel_, &client);
        const int max_scroll = std::max(0, content_height_ - static_cast<int>(client.bottom));
        const int target = std::clamp(scroll_pos_ + delta, 0, max_scroll);
        const int applied = target - scroll_pos_;
        if (applied == 0) {
            return;
        }
        scroll_pos_ = target;
        ScrollWindowEx(panel_, 0, -applied, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        SetScrollPos(panel_, SB_VERT, scroll_pos_, TRUE);
        UpdateWindow(panel_);
    }

    void on_panel_scroll(int code)
    {
        SCROLLINFO info {sizeof(info), SIF_ALL};
        GetScrollInfo(panel_, SB_VERT, &info);
        const int line = scale(26);
        int target = scroll_pos_;
        switch (code) {
            case SB_LINEUP: target -= line; break;
            case SB_LINEDOWN: target += line; break;
            case SB_PAGEUP: target -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: target += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: target = info.nTrackPos; break;
            case SB_TOP: target = 0; break;
            case SB_BOTTOM: target = content_height_; break;
            default: return;
        }
        scroll_panel_by(target - scroll_pos_);
    }

    // Keeps a newly focused control visible when tabbing through the panel.
    void ensure_visible(HWND child)
    {
        if (!child || GetParent(child) != panel_) {
            return;
        }
        RECT rc {};
        GetWindowRect(child, &rc);
        MapWindowPoints(nullptr, panel_, reinterpret_cast<POINT*>(&rc), 2);
        RECT client {};
        GetClientRect(panel_, &client);
        if (rc.top < 0) {
            scroll_panel_by(rc.top - scale(8));
        } else if (rc.bottom > client.bottom) {
            scroll_panel_by(rc.bottom - client.bottom + scale(8));
        }
    }

    const ParamRow* row_for_control(int id)
    {
        const int index = (id - kIdParamBase) / kIdsPerParam;
        if (id < kIdParamBase || index < 0 || index >= static_cast<int>(rows_.size())) {
            return nullptr;
        }
        return &rows_[static_cast<std::size_t>(index)];
    }

    static int role_for_control(int id) { return (id - kIdParamBase) % kIdsPerParam; }

    void sync_row(const ParamRow& row)
    {
        const PsParamDesc& desc = *row.desc;
        const double value = ps_get_param_value(desc, working_);
        switch (desc.kind) {
            case PsParamKind::Float: {
                const double span = std::max(1.0e-9, static_cast<double>(desc.slider_max - desc.slider_min));
                const double t = std::clamp((value - desc.slider_min) / span, 0.0, 1.0);
                SendMessageW(row.track, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(t * kFloatTrackSteps)));
                SetWindowTextW(row.value, format_value(desc, value).c_str());
                break;
            }
            case PsParamKind::Int:
                SendMessageW(row.track, TBM_SETPOS, TRUE, static_cast<LPARAM>(value));
                SetWindowTextW(row.value, format_value(desc, value).c_str());
                break;
            case PsParamKind::Bool:
                Button_SetCheck(row.value, value >= 0.5 ? BST_CHECKED : BST_UNCHECKED);
                break;
            case PsParamKind::Choice:
                SendMessageW(row.value, CB_SETCURSEL, static_cast<WPARAM>(static_cast<int>(value) - 1), 0);
                break;
        }
    }

    void sync_lens_controls()
    {
        SendMessageW(manufacturer_, CB_SETCURSEL, static_cast<WPARAM>(working_.lens_manufacturer_index - 1), 0);
        SendMessageW(lens_, CB_RESETCONTENT, 0, 0);
        const std::string labels = build_lens_popup_string_for_manufacturer(working_.lens_manufacturer_index - 1);
        for (const std::string& label : ps_split_choice_labels(labels)) {
            SendMessageW(lens_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(label).c_str()));
        }
        SendMessageW(lens_, CB_SETCURSEL, static_cast<WPARAM>(working_.lens_model_index - 1), 0);
    }

    void update_enabled()
    {
        for (const ParamRow& row : rows_) {
            const BOOL enabled = param_enabled(*row.desc, working_) ? TRUE : FALSE;
            for (HWND child : {row.label, row.track, row.value}) {
                if (child) {
                    EnableWindow(child, enabled);
                }
            }
        }
    }

    void sync_controls_from_state()
    {
        updating_ = true;
        sync_lens_controls();
        for (const ParamRow& row : rows_) {
            sync_row(row);
        }
        update_enabled();
        updating_ = false;
    }

    void commit_edit(const ParamRow& row)
    {
        double value = 0.0;
        if (parse_value(window_text(row.value), value)) {
            ps_set_param_value(*row.desc, working_, value);
        }
        updating_ = true;
        sync_row(row);
        updating_ = false;
        request_preview();
    }

    void on_track(HWND track)
    {
        if (updating_) {
            return;
        }
        const ParamRow* row = row_for_control(GetDlgCtrlID(track));
        if (!row || row->track != track) {
            return;
        }
        const PsParamDesc& desc = *row->desc;
        const LRESULT pos = SendMessageW(track, TBM_GETPOS, 0, 0);
        double value = static_cast<double>(pos);
        if (desc.kind == PsParamKind::Float) {
            value = desc.slider_min + (desc.slider_max - desc.slider_min) * (static_cast<double>(pos) / kFloatTrackSteps);
        }
        ps_set_param_value(desc, working_, value);
        updating_ = true;
        SetWindowTextW(row->value, format_value(desc, ps_get_param_value(desc, working_)).c_str());
        updating_ = false;
        request_preview();
    }

    void on_panel_command(int id, int code, HWND control)
    {
        if (code == EN_SETFOCUS || code == BN_SETFOCUS || code == CBN_SETFOCUS) {
            ensure_visible(control);
        }
        if (updating_) {
            return;
        }
        const ParamRow* row = row_for_control(id);
        if (!row || role_for_control(id) != RoleValue) {
            return;
        }
        switch (row->desc->kind) {
            case PsParamKind::Float:
            case PsParamKind::Int:
                if (code == EN_KILLFOCUS) {
                    commit_edit(*row);
                }
                return;
            case PsParamKind::Bool:
                if (code == BN_CLICKED) {
                    ps_set_param_value(*row->desc, working_, Button_GetCheck(row->value) == BST_CHECKED ? 1.0 : 0.0);
                    update_enabled();
                    request_preview();
                }
                return;
            case PsParamKind::Choice:
                if (code == CBN_SELCHANGE) {
                    const LRESULT selection = SendMessageW(row->value, CB_GETCURSEL, 0, 0);
                    if (selection >= 0) {
                        ps_set_param_value(*row->desc, working_, static_cast<double>(selection + 1));
                        update_enabled();
                        request_preview();
                    }
                }
                return;
        }
    }

    INT_PTR on_command(int id, int code)
    {
        switch (id) {
            case IDOK: {
                // Enter inside a value field commits that field instead of closing.
                HWND focus = GetFocus();
                if (focus && GetParent(focus) == panel_) {
                    const ParamRow* row = row_for_control(GetDlgCtrlID(focus));
                    if (row && row->value == focus &&
                        (row->desc->kind == PsParamKind::Float || row->desc->kind == PsParamKind::Int)) {
                        commit_edit(*row);
                        SendMessageW(focus, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                }
                close(IDOK);
                return TRUE;
            }
            case IDCANCEL:
                close(IDCANCEL);
                return TRUE;
            case kIdDefaults: {
                working_ = ps_default_ui_state();
                sync_controls_from_state();
                request_preview();
                return TRUE;
            }
            case kIdManufacturer:
                if (code == CBN_SELCHANGE && !updating_) {
                    const LRESULT selection = SendMessageW(manufacturer_, CB_GETCURSEL, 0, 0);
                    if (selection >= 0) {
                        working_.lens_manufacturer_index = static_cast<int>(selection) + 1;
                        working_.lens_model_index = 1;
                        updating_ = true;
                        sync_lens_controls();
                        updating_ = false;
                        request_preview();
                    }
                }
                return TRUE;
            case kIdLens:
                if (code == CBN_SELCHANGE && !updating_) {
                    const LRESULT selection = SendMessageW(lens_, CB_GETCURSEL, 0, 0);
                    if (selection >= 0) {
                        working_.lens_model_index = static_cast<int>(selection) + 1;
                        request_preview();
                    }
                }
                return TRUE;
            case kIdPreviewEnable:
                if (code == BN_CLICKED) {
                    preview_enabled_ = Button_GetCheck(preview_enable_) == BST_CHECKED;
                    request_preview();
                }
                return TRUE;
            case kIdPreviewResolution:
                if (code == CBN_SELCHANGE) {
                    request_preview();
                }
                return TRUE;
            default:
                return FALSE;
        }
    }

    void close(int code)
    {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)));
        // Joins the worker; a running preview render finishes first.
        worker_.reset();
        EndDialog(hwnd_, code);
    }

    RECT image_rect() const
    {
        RECT client {};
        GetClientRect(preview_, &client);
        const FloatImageBuffer& image = *context_.image;
        const int pad = scale(6);
        const int avail_w = std::max(1, static_cast<int>(client.right) - pad * 2);
        const int avail_h = std::max(1, static_cast<int>(client.bottom) - pad * 2);
        const double s = std::min(static_cast<double>(avail_w) / image.width, static_cast<double>(avail_h) / image.height);
        const int w = std::max(1, static_cast<int>(image.width * s));
        const int h = std::max(1, static_cast<int>(image.height * s));
        const int x = (static_cast<int>(client.right) - w) / 2;
        const int y = (static_cast<int>(client.bottom) - h) / 2;
        return {x, y, x + w, y + h};
    }

    void request_preview()
    {
        if (!worker_ || !preview_) {
            return;
        }
        const RECT target = image_rect();
        if (!preview_enabled_) {
            // Show the untouched document so the user can still compare.
            ps_make_display_bitmap(*context_.image, context_.linear_document, target.right - target.left,
                                   target.bottom - target.top, display_pixels_);
            display_width_ = target.right - target.left;
            display_height_ = target.bottom - target.top;
            SetWindowTextW(status_, L"Preview off");
            InvalidateRect(preview_, nullptr, FALSE);
            return;
        }

        PreviewRequest request;
        request.state = working_;
        const LRESULT res = SendMessageW(preview_resolution_, CB_GETCURSEL, 0, 0);
        request.resolution = static_cast<PreviewResolution>(std::clamp<LRESULT>(res, 0, 2));
        request.display_width = target.right - target.left;
        request.display_height = target.bottom - target.top;
        request.generation = ++generation_;
        worker_->submit(request);
        SetWindowTextW(status_, L"Rendering preview…");
    }

    void on_preview_ready()
    {
        if (!worker_) {
            return;
        }
        PreviewResult result;
        if (!worker_->take_result(result)) {
            return;
        }
        if (!result.pixels.empty()) {
            display_pixels_ = std::move(result.pixels);
            display_width_ = result.width;
            display_height_ = result.height;
            InvalidateRect(preview_, nullptr, FALSE);
        }
        std::wstring status = result.status;
        if (result.generation != generation_) {
            status += L" · updating…";
        }
        status += L"  ·  click the preview to place the manual source";
        SetWindowTextW(status_, status.c_str());
    }

    void paint_preview()
    {
        PAINTSTRUCT ps {};
        HDC target = BeginPaint(preview_, &ps);
        RECT client {};
        GetClientRect(preview_, &client);

        HDC dc = CreateCompatibleDC(target);
        HBITMAP surface = CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
        HGDIOBJ old_surface = SelectObject(dc, surface);

        HBRUSH backdrop = CreateSolidBrush(RGB(38, 38, 38));
        FillRect(dc, &client, backdrop);
        DeleteObject(backdrop);

        const RECT dest = image_rect();
        if (!display_pixels_.empty() && display_width_ > 0 && display_height_ > 0) {
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = display_width_;
            info.bmiHeader.biHeight = -display_height_;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, dest.left, dest.top, dest.right - dest.left, dest.bottom - dest.top, 0, 0,
                          display_width_, display_height_, display_pixels_.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }

        if (working_.manual_source_enabled) {
            const int cx = dest.left + static_cast<int>(working_.manual_source_x * (dest.right - dest.left));
            const int cy = dest.top + static_cast<int>(working_.manual_source_y * (dest.bottom - dest.top));
            const int arm = scale(10);
            // Dark outline under a bright stroke keeps the marker visible on
            // both blown-out flare and dark sky.
            for (const auto& [width, colour] : {std::pair {scale(4), RGB(0, 0, 0)}, std::pair {scale(2), RGB(255, 200, 40)}}) {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, width), colour);
                HGDIOBJ old_pen = SelectObject(dc, pen);
                MoveToEx(dc, cx - arm, cy, nullptr);
                LineTo(dc, cx + arm + 1, cy);
                MoveToEx(dc, cx, cy - arm, nullptr);
                LineTo(dc, cx, cy + arm + 1);
                SelectObject(dc, old_pen);
                DeleteObject(pen);
            }
        }

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_surface);
        DeleteObject(surface);
        DeleteDC(dc);
        EndPaint(preview_, &ps);
    }

    void place_source(int x, int y)
    {
        const RECT dest = image_rect();
        const double w = std::max(1L, dest.right - dest.left);
        const double h = std::max(1L, dest.bottom - dest.top);
        working_.manual_source_enabled = true;
        // Dragging past the image edge gives an off-frame source, like typing
        // Source X/Y outside 0..1.
        ps_set_param_value(*ps_find_param("source_x"), working_, (x - dest.left) / w);
        ps_set_param_value(*ps_find_param("source_y"), working_, (y - dest.top) / h);

        updating_ = true;
        for (const ParamRow& row : rows_) {
            const std::string_view key = row.desc->key;
            if (key == "manual_source" || key == "source_x" || key == "source_y") {
                sync_row(row);
            }
        }
        update_enabled();
        updating_ = false;
        InvalidateRect(preview_, nullptr, FALSE);
        request_preview();
    }

    const PsDialogContext& context_;
    AeUiParameterState working_;
    HWND hwnd_ = nullptr;
    HWND preview_ = nullptr;
    HWND lens_label_ = nullptr;
    HWND manufacturer_ = nullptr;
    HWND lens_ = nullptr;
    HWND panel_ = nullptr;
    HWND preview_enable_ = nullptr;
    HWND preview_resolution_ = nullptr;
    HWND status_ = nullptr;
    HWND defaults_ = nullptr;
    HWND ok_ = nullptr;
    HWND cancel_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_font_ = nullptr;
    std::vector<ParamRow> rows_;
    std::vector<ParamRow> row_order_;
    int dpi_ = 96;
    int scroll_pos_ = 0;
    int content_height_ = 0;
    bool updating_ = false;
    bool dragging_ = false;
    bool preview_enabled_ = true;
    std::uint64_t generation_ = 0;
    std::unique_ptr<PreviewWorker> worker_;
    std::vector<std::uint32_t> display_pixels_;
    int display_width_ = 0;
    int display_height_ = 0;

public:
    ~Dialog()
    {
        worker_.reset();
        if (font_) {
            DeleteObject(font_);
        }
        if (bold_font_) {
            DeleteObject(bold_font_);
        }
    }
};

} // namespace

PsDialogResult ps_run_dialog(const PsDialogContext& context, AeUiParameterState& state)
{
    if (!context.image || context.image->width <= 0 || context.image->height <= 0) {
        return PsDialogResult::Error;
    }
    Dialog dialog(context, state);
    const PsDialogResult result = dialog.run();
    if (result == PsDialogResult::Ok) {
        state = dialog.state();
    }
    return result;
}
