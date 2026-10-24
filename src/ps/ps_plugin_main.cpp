// Photoshop filter entry point. Reads the whole document into float buffers,
// runs the shared FlareSim runtime (the same path the AE plugin uses), and
// writes the filtered area back. Settings travel as flaresim-ps text in the
// parameters handle (Ctrl+F re-runs), the scripting descriptor (actions and
// Smart Filters) and a last-used file under %APPDATA%. The descriptor can also
// name a preset, which scripts use to apply saved settings by name.

#include "PIDefines.h"
#include "PIFilter.h"
#include "PIAbout.h"

#include "asset_root.h"
#include "color_space.h"
#include "frame_bridge.h"
#include "ps_dialog.h"
#include "ps_image.h"
#include "ps_presets.h"
#include "ps_settings.h"
#include "ps_version.h"
#include "render_frame.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

constexpr DescriptorKeyID kKeySettings = 'FsSt';
constexpr DescriptorKeyID kKeyPreset = 'FsPr';
constexpr std::uint32_t kGlobalsMagic = 'FlSm';
// Input bands are sized to stay well inside Photoshop's tile memory.
constexpr std::int64_t kBandBytes = 32ll * 1024ll * 1024ll;

struct Globals
{
    std::uint32_t magic = kGlobalsMagic;
    bool query_for_parameters = true;
    // Set when the user pressed OK in the dialog. Scripted and action runs
    // must not overwrite the interactive last-used settings.
    bool dialog_confirmed = false;
    // Preset whose settings equal the ones just rendered; recorded next to the
    // settings so the Actions panel shows which preset an action step used.
    std::string preset_name;
};

class PsError : public std::exception
{
public:
    PsError(int16 code, std::string message = {}) : code_(code), message_(std::move(message)) {}
    int16 code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    int16 code_;
    std::string message_;
};

Globals* globals_from(intptr_t* data)
{
    auto* globals = reinterpret_cast<Globals*>(*data);
    if (globals && globals->magic == kGlobalsMagic) {
        return globals;
    }
    globals = new Globals {};
    *data = reinterpret_cast<intptr_t>(globals);
    return globals;
}

HWND host_window(const FilterRecord* record)
{
    const auto* platform = record ? static_cast<const PlatformData*>(record->platformData) : nullptr;
    return platform ? reinterpret_cast<HWND>(platform->hwnd) : nullptr;
}

void report_error(FilterRecord* record, int16* result, const std::string& message)
{
    if (record && record->errorString && !message.empty()) {
        const std::size_t length = std::min<std::size_t>(message.size(), 255);
        (*record->errorString)[0] = static_cast<unsigned char>(length);
        std::memcpy(&(*record->errorString)[1], message.data(), length);
        *result = errReportString;
    } else {
        *result = filterBadParameters;
    }
}

// ---------------------------------------------------------------------------
// Settings persistence

std::string read_handle_text(const FilterRecord* record, Handle handle)
{
    if (!handle || !record->handleProcs) {
        return {};
    }
    const int32 size = record->handleProcs->getSizeProc(handle);
    Ptr data = record->handleProcs->lockProc(handle, false);
    std::string text;
    if (data && size > 0) {
        text.assign(data, data + size);
        const std::size_t nul = text.find('\0');
        if (nul != std::string::npos) {
            text.resize(nul);
        }
    }
    record->handleProcs->unlockProc(handle);
    return text;
}

Handle new_handle_with_text(const FilterRecord* record, const std::string& text)
{
    Handle handle = record->handleProcs->newProc(static_cast<int32>(text.size()));
    if (!handle) {
        return nullptr;
    }
    Ptr data = record->handleProcs->lockProc(handle, false);
    if (data && !text.empty()) {
        std::memcpy(data, text.data(), text.size());
    }
    record->handleProcs->unlockProc(handle);
    return handle;
}

void store_parameters_handle(FilterRecord* record, const std::string& text)
{
    if (!record->handleProcs) {
        return;
    }
    // Photoshop owns record->parameters; resize in place when it already exists.
    const std::string stored = text + '\0';
    if (record->parameters) {
        if (record->handleProcs->setSizeProc(record->parameters, static_cast<int32>(stored.size())) != noErr) {
            return;
        }
    } else {
        record->parameters = record->handleProcs->newProc(static_cast<int32>(stored.size()));
        if (!record->parameters) {
            return;
        }
    }
    Ptr data = record->handleProcs->lockProc(record->parameters, false);
    if (data) {
        std::memcpy(data, stored.data(), stored.size());
    }
    record->handleProcs->unlockProc(record->parameters);
}

fs::path last_used_path()
{
    PWSTR folder = nullptr;
    fs::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder))) {
        path = fs::path(folder) / L"FlareSim" / L"photoshop-last-used.txt";
    }
    CoTaskMemFree(folder);
    return path;
}

void load_last_used(AeUiParameterState& state)
{
    const fs::path path = last_used_path();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ps_deserialize_settings(buffer.str(), state);
}

void save_last_used(const std::string& text)
{
    const fs::path path = last_used_path();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

struct DescriptorInput
{
    bool present = false;
    bool show_dialog = false;
    std::string preset;
    bool has_settings = false;
    std::string settings;
};

DescriptorInput read_descriptor(FilterRecord* record)
{
    DescriptorInput input;
    PIDescriptorParameters* params = record->descriptorParameters;
    if (!params || !params->readDescriptorProcs || !params->descriptor) {
        return input;
    }
    ReadDescriptorProcs* procs = params->readDescriptorProcs;

    DescriptorKeyID keys[] = {kKeyPreset, kKeySettings, 0};
    PIReadDescriptor token = procs->openReadDescriptorProc(params->descriptor, keys);
    if (token) {
        DescriptorKeyID key = 0;
        DescriptorTypeID type = 0;
        int32 flags = 0;
        while (procs->getKeyProc(token, &key, &type, &flags)) {
            if (key != kKeySettings && key != kKeyPreset) {
                continue;
            }
            Handle text = nullptr;
            if (procs->getTextProc(token, &text) == noErr && text) {
                if (key == kKeySettings) {
                    input.settings = read_handle_text(record, text);
                    input.has_settings = true;
                } else {
                    input.preset = read_handle_text(record, text);
                }
                record->handleProcs->disposeProc(text);
            }
        }
        procs->closeReadDescriptorProc(token);
    }
    record->handleProcs->disposeProc(params->descriptor);
    params->descriptor = nullptr;

    input.present = true;
    input.show_dialog = params->playInfo == plugInDialogDisplay;
    return input;
}

// Settings missing from the descriptor fall back to defaults (or to the named
// preset), never to the last-used state, so a script or action renders the
// same every time. The preset loads first and the settings text goes on top,
// so a script can name a preset and override single keys. Recorded actions
// carry the full settings, so they still play back if the preset is gone.
void apply_descriptor(const DescriptorInput& input, AeUiParameterState& state)
{
    state = ps_default_ui_state();
    if (!input.preset.empty() && !ps_load_preset(ps_default_preset_folder(), input.preset, state) &&
        !input.has_settings) {
        throw PsError(filterBadParameters,
                      "FlareSim preset \"" + input.preset + "\" was not found in %APPDATA%\\FlareSim\\presets.");
    }
    if (input.has_settings && !ps_deserialize_settings(input.settings, state)) {
        throw PsError(filterBadParameters, "FlareSim settings text must start with \"flaresim-ps 1\".");
    }
}

bool put_text(FilterRecord* record, WriteDescriptorProcs* procs, PIWriteDescriptor token, DescriptorKeyID key,
              const std::string& text)
{
    Handle handle = new_handle_with_text(record, text);
    if (!handle) {
        return false;
    }
    const bool ok = procs->putTextProc(token, key, handle) == noErr;
    // The descriptor keeps its own copy.
    record->handleProcs->disposeProc(handle);
    return ok;
}

void write_descriptor(FilterRecord* record, const std::string& text, const std::string& preset_name)
{
    PIDescriptorParameters* params = record->descriptorParameters;
    if (!params || !params->writeDescriptorProcs) {
        return;
    }
    WriteDescriptorProcs* procs = params->writeDescriptorProcs;
    PIWriteDescriptor token = procs->openWriteDescriptorProc();
    if (!token) {
        return;
    }
    if (!preset_name.empty()) {
        put_text(record, procs, token, kKeyPreset, preset_name);
    }
    put_text(record, procs, token, kKeySettings, text);
    if (params->descriptor) {
        record->handleProcs->disposeProc(params->descriptor);
        params->descriptor = nullptr;
    }
    PIDescriptorHandle handle = nullptr;
    procs->closeWriteDescriptorProc(token, &handle);
    params->descriptor = handle;
    params->recordInfo = plugInDialogOptional;
}

// ---------------------------------------------------------------------------
// Assets

std::string resolve_asset_root()
{
#ifdef FLARESIM_REPO_ROOT
    if (is_flaresim_asset_root(FLARESIM_REPO_ROOT)) {
        return FLARESIM_REPO_ROOT;
    }
#endif
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&resolve_asset_root),
                           &module)) {
        wchar_t path[MAX_PATH * 4] {};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        std::string root;
        if (length > 0 && find_flaresim_asset_root(fs::path(path).string(), root)) {
            return root;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Pixel transfer

struct DocumentLayout
{
    VRect image {};
    VRect filter {};
    int depth = 8;
    int input_planes = 3;
    int output_planes = 3;
    bool input_alpha = false;
    bool output_alpha = false;
};

VRect make_rect(int32 left, int32 top, int32 right, int32 bottom)
{
    VRect rect {};
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    return rect;
}

void set_rects(FilterRecord* record, const VRect& in_rect, const VRect& out_rect)
{
    const VRect empty = make_rect(0, 0, 0, 0);
    BigDocumentStruct* big = record->bigDocumentData;
    big->inRect32 = in_rect;
    big->outRect32 = out_rect;
    big->maskRect32 = empty;
}

bool is_transparency_case(int16 filter_case)
{
    return filter_case == filterCaseEditableTransparencyNoSelection ||
           filter_case == filterCaseEditableTransparencyWithSelection ||
           filter_case == filterCaseProtectedTransparencyNoSelection ||
           filter_case == filterCaseProtectedTransparencyWithSelection;
}

bool is_editable_transparency_case(int16 filter_case)
{
    return filter_case == filterCaseEditableTransparencyNoSelection ||
           filter_case == filterCaseEditableTransparencyWithSelection;
}

DocumentLayout describe_document(const FilterRecord* record)
{
    if (!record->bigDocumentData) {
        throw PsError(errPlugInHostInsufficient, "FlareSim needs Photoshop CS or newer (big document support).");
    }
    if (record->imageMode != plugInModeRGBColor && record->imageMode != plugInModeRGB48 &&
        record->imageMode != plugInModeRGB96) {
        throw PsError(filterBadMode, "FlareSim works on RGB documents (8, 16 or 32 bits per channel).");
    }

    DocumentLayout layout;
    const BigDocumentStruct* big = record->bigDocumentData;
    layout.image = make_rect(0, 0, big->imageSize32.h, big->imageSize32.v);
    layout.filter = big->filterRect32;
    layout.depth = record->depth;
    if (layout.depth != 8 && layout.depth != 16 && layout.depth != 32) {
        throw PsError(filterBadMode, "Unsupported bit depth.");
    }

    const bool counts_set = record->inLayerPlanes != 0 || record->inTransparencyMask != 0 ||
                            record->inNonLayerPlanes != 0;
    if (counts_set) {
        layout.input_alpha = record->inLayerPlanes >= 3 && record->inTransparencyMask > 0;
        layout.output_alpha = record->outLayerPlanes >= 3 && record->outTransparencyMask > 0;
    } else {
        layout.input_alpha = record->planes >= 4 && is_transparency_case(record->filterCase);
        layout.output_alpha = layout.input_alpha && is_editable_transparency_case(record->filterCase);
    }
    layout.input_planes = layout.input_alpha ? 4 : 3;
    layout.output_planes = layout.output_alpha ? 4 : 3;
    if (record->planes < layout.input_planes) {
        throw PsError(filterBadMode, "FlareSim needs the red, green and blue channels to be editable.");
    }
    return layout;
}

int band_rows(int width, int planes, int depth)
{
    const std::int64_t row_bytes = static_cast<std::int64_t>(width) * planes * (depth / 8);
    return static_cast<int>(std::max<std::int64_t>(1, kBandBytes / std::max<std::int64_t>(row_bytes, 1)));
}

void check_abort(FilterRecord* record)
{
    if (record->abortProc && record->abortProc()) {
        throw PsError(userCanceledErr);
    }
}

void allocate(FloatImageBuffer& image, int width, int height)
{
    const std::size_t np = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.width = width;
    image.height = height;
    image.r.assign(np, 0.0f);
    image.g.assign(np, 0.0f);
    image.b.assign(np, 0.0f);
    image.alpha.assign(np, 1.0f);
}

// Progress budget in permille: 0..200 reading, 200..900 rendering, 900..1000 writing.
constexpr int32 kProgressTotal = 1000;
constexpr int32 kProgressReadEnd = 200;
constexpr int32 kProgressRenderEnd = 900;

void read_document(FilterRecord* record, const DocumentLayout& layout, FloatImageBuffer& image)
{
    const int width = layout.image.right - layout.image.left;
    const int height = layout.image.bottom - layout.image.top;
    allocate(image, width, height);

    record->inputRate = static_cast<Fixed>(1) << 16;
    record->maskRate = static_cast<Fixed>(1) << 16;
    record->inLoPlane = 0;
    record->inHiPlane = static_cast<int16>(layout.input_planes - 1);
    record->outLoPlane = 0;
    record->outHiPlane = 0;
    record->wantLayout = piLayoutTraditional;

    const int rows = band_rows(width, layout.input_planes, layout.depth);
    const VRect empty = make_rect(0, 0, 0, 0);
    for (int top = 0; top < height; top += rows) {
        const int bottom = std::min(height, top + rows);
        set_rects(record, make_rect(0, top, width, bottom), empty);
        const OSErr err = record->advanceState();
        if (err != noErr) {
            throw PsError(err);
        }
        PsPixelBuffer band {record->inData, record->inRowBytes, layout.depth, layout.input_planes, width, bottom - top};
        PsChannelMap map {layout.input_alpha ? 3 : -1};
        if (!ps_read_pixels(band, map, layout.input_alpha, image, 0, top)) {
            throw PsError(filterBadParameters, "FlareSim could not read the document pixels.");
        }
        record->progressProc(static_cast<int32>(kProgressReadEnd * static_cast<std::int64_t>(bottom) / height), kProgressTotal);
        check_abort(record);
    }
}

void write_document(FilterRecord* record, const DocumentLayout& layout, const FloatImageBuffer& image)
{
    const VRect filter = layout.filter;
    const int width = filter.right - filter.left;
    const int height = filter.bottom - filter.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    record->inLoPlane = 0;
    record->inHiPlane = 0;
    record->outLoPlane = 0;
    record->outHiPlane = static_cast<int16>(layout.output_planes - 1);

    const int rows = band_rows(width, layout.output_planes, layout.depth);
    const VRect empty = make_rect(0, 0, 0, 0);
    for (int top = filter.top; top < filter.bottom; top += rows) {
        const int bottom = std::min(static_cast<int>(filter.bottom), top + rows);
        set_rects(record, empty, make_rect(filter.left, top, filter.right, bottom));
        const OSErr err = record->advanceState();
        if (err != noErr) {
            throw PsError(err);
        }
        PsPixelBuffer band {record->outData, record->outRowBytes, layout.depth, layout.output_planes, width, bottom - top};
        PsChannelMap map {layout.output_alpha ? 3 : -1};
        if (!ps_write_pixels(image, filter.left, top, map, layout.input_alpha, band)) {
            throw PsError(filterBadParameters, "FlareSim could not write the filtered pixels.");
        }
        record->progressProc(kProgressRenderEnd + static_cast<int32>((kProgressTotal - kProgressRenderEnd) *
                                                                     static_cast<std::int64_t>(bottom - filter.top) / height),
                             kProgressTotal);
        check_abort(record);
    }
}

// Renders on a short-lived thread so the runtime's thread_local caches (and
// any CUDA resources they hold) are released at thread exit, never inside
// DLL unload. The main thread drives Photoshop's progress bar from the
// render's own progress and turns Esc into a cancel, which the render picks up
// between chunks of flare sources.
bool render_on_worker(FilterRecord* record,
                      const std::string& asset_root,
                      const AeParameterState& state,
                      const FloatImageBuffer& input,
                      FloatImageBuffer& output)
{
    std::atomic<bool> done {false};
    std::atomic<bool> cancel {false};
    std::atomic<int> permille {0};
    bool ok = false;
    std::thread worker([&] {
        try {
            ScopedRenderProgress progress([&](float fraction) {
                permille.store(static_cast<int>(fraction * 1000.0f));
                return !cancel.load();
            });
            ok = render_frame_to_float_image(asset_root, state, input, output);
        } catch (...) {
            ok = false;
        }
        done.store(true);
    });

    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!cancel.load() && record->abortProc && record->abortProc()) {
            cancel.store(true);
        }
        const std::int64_t span = kProgressRenderEnd - kProgressReadEnd;
        record->progressProc(kProgressReadEnd + static_cast<int32>(span * permille.load() / 1000), kProgressTotal);
    }
    worker.join();
    if (cancel.load()) {
        throw PsError(userCanceledErr);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Selectors

void do_about(AboutRecord* about)
{
    const auto* platform = about ? static_cast<const PlatformData*>(about->platformData) : nullptr;
    const std::wstring text =
        L"FlareSim PS " FLARESIM_PS_VERSION_W L"\n\n"
        L"Physically based lens flare simulation: ghost reflections are ray traced through real lens "
        L"prescriptions per wavelength. Port of the FlareSim After Effects plug-in.\n\n"
        L"Works on RGB documents in 8, 16 and 32 bits per channel. Use 32-bit (linear HDR) documents "
        L"for physically meaningful source thresholds.";
    MessageBoxW(platform ? reinterpret_cast<HWND>(platform->hwnd) : nullptr, text.c_str(), L"About FlareSim",
                MB_OK | MB_ICONINFORMATION);
}

// Only called when the user picks the menu item, never for Ctrl+F or actions.
void do_parameters(Globals* globals)
{
    globals->query_for_parameters = true;
}

void do_prepare(FilterRecord* record)
{
    record->bufferSpace = 0;
    record->bufferSpace64 = 0;
    // We read and write in bands, so a modest working set is enough.
    const std::int64_t wanted = kBandBytes * 4;
    if (record->maxSpace64 > wanted) {
        record->maxSpace64 = wanted;
    }
    if (record->maxSpace > wanted) {
        record->maxSpace = static_cast<int32>(wanted);
    }
}

// What Color Space "Auto" means for this document. 32-bit documents hold
// linear values in the primaries of their profile, so the profile's
// colorants identify the space. 8/16-bit documents are gamma encoded; they
// keep the Linear Rec.709 / sRGB behaviour.
WorkingColorSpace document_working_space(const FilterRecord* record, const DocumentLayout& layout, bool& out_known)
{
    WorkingColorSpace space = WorkingColorSpace::LinearRec709;
    out_known = false;
    if (layout.depth != 32 || !record->canUseICCProfiles || !record->iCCprofileData || !record->handleProcs) {
        return space;
    }
    const int32 size = record->handleProcs->getSizeProc(record->iCCprofileData);
    Ptr data = record->handleProcs->lockProc(record->iCCprofileData, false);
    if (data && size > 0) {
        out_known = working_space_from_icc_profile(data, static_cast<std::size_t>(size), space);
    }
    record->handleProcs->unlockProc(record->iCCprofileData);
    return space;
}

std::wstring describe(const DocumentLayout& layout)
{
    const int width = layout.image.right - layout.image.left;
    const int height = layout.image.bottom - layout.image.top;
    std::wstring text = std::to_wstring(width) + L"×" + std::to_wstring(height) + L" · " +
                        std::to_wstring(layout.depth) + L"-bit";
    if (layout.depth == 32) {
        text += L" linear";
    }
    if (layout.input_alpha) {
        text += L" · layer transparency";
    }
    return text;
}

std::string to_utf8(const std::wstring& text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

void do_start(FilterRecord* record, Globals* globals)
{
    if (!record->advanceState) {
        throw PsError(errPlugInHostInsufficient, "This host does not support advanceState.");
    }
    const DocumentLayout layout = describe_document(record);
    // Without this Photoshop clamps 32-bit results to 0..1 and the HDR flare is lost.
    record->allowOverrangeColors = true;

    AeUiParameterState ui = ps_default_ui_state();
    const std::string stored = read_handle_text(record, record->parameters);
    if (!stored.empty()) {
        ps_deserialize_settings(stored, ui);
    } else {
        load_last_used(ui);
    }

    bool show_dialog = globals->query_for_parameters;
    const DescriptorInput descriptor = read_descriptor(record);
    if (descriptor.present) {
        apply_descriptor(descriptor, ui);
        show_dialog = descriptor.show_dialog;
    }

    const std::string asset_root = resolve_asset_root();
    if (asset_root.empty()) {
        throw PsError(filterBadParameters,
                      "FlareSim lens assets not found. Copy the 'assets' folder next to FlareSimPS.8bf.");
    }

    FloatImageBuffer input;
    read_document(record, layout, input);
    bool document_profile_known = false;
    const WorkingColorSpace document_space = document_working_space(record, layout, document_profile_known);

    if (show_dialog) {
        PsDialogContext context;
        context.parent_window = host_window(record);
        context.image = &input;
        context.linear_document = layout.depth == 32;
        context.document_space = document_space;
        context.document_profile_known = document_profile_known;
        context.asset_root = asset_root;
        context.document_description = to_utf8(describe(layout));
        const PsDialogResult result = ps_run_dialog(context, ui);
        if (result == PsDialogResult::Cancel) {
            throw PsError(userCanceledErr);
        }
        if (result == PsDialogResult::Error) {
            throw PsError(filterBadParameters, "FlareSim could not open its dialog.");
        }
    }
    globals->query_for_parameters = false;
    globals->dialog_confirmed = show_dialog;

    const std::string text = ps_serialize_settings(ui);
    store_parameters_handle(record, text);
    globals->preset_name = ps_find_matching_preset(ps_default_preset_folder(), ui);

    AeParameterState state {};
    if (!ps_build_parameter_state(ui, state)) {
        throw PsError(filterBadParameters, "FlareSim settings are invalid.");
    }
    resolve_auto_working_space(state, document_space);

    FloatImageBuffer output;
    if (!render_on_worker(record, asset_root, state, input, output)) {
        throw PsError(filterBadParameters, "FlareSim render failed (check the lens assets).");
    }

    // Protected transparency keeps the stored alpha, so colour has to be
    // un-premultiplied against that alpha rather than the rendered one.
    if (layout.input_alpha && !layout.output_alpha) {
        output.alpha = input.alpha;
    }
    input = FloatImageBuffer {};

    write_document(record, layout, output);
    set_rects(record, make_rect(0, 0, 0, 0), make_rect(0, 0, 0, 0));
}

void do_continue(FilterRecord* record)
{
    if (record->bigDocumentData) {
        set_rects(record, make_rect(0, 0, 0, 0), make_rect(0, 0, 0, 0));
    }
}

void do_finish(FilterRecord* record, Globals* globals)
{
    const std::string text = read_handle_text(record, record->parameters);
    if (text.empty()) {
        return;
    }
    write_descriptor(record, text, globals->preset_name);
    if (globals->dialog_confirmed) {
        save_last_used(text);
        globals->dialog_confirmed = false;
    }
}

} // namespace

DLLExport MACPASCAL void PluginMain(const int16 selector, FilterRecordPtr record, intptr_t* data, int16* result)
{
    if (!result) {
        return;
    }
    *result = noErr;

    try {
        if (selector == filterSelectorAbout) {
            do_about(reinterpret_cast<AboutRecord*>(record));
            return;
        }
        if (!record || !data) {
            *result = filterBadParameters;
            return;
        }
        if (record->bigDocumentData) {
            record->bigDocumentData->PluginUsing32BitCoordinates = true;
        }

        Globals* globals = globals_from(data);
        switch (selector) {
            case filterSelectorParameters:
                do_parameters(globals);
                break;
            case filterSelectorPrepare:
                do_prepare(record);
                break;
            case filterSelectorStart:
                do_start(record, globals);
                break;
            case filterSelectorContinue:
                do_continue(record);
                break;
            case filterSelectorFinish:
                do_finish(record, globals);
                break;
            default:
                break;
        }
    } catch (const PsError& error) {
        if (!error.message().empty()) {
            report_error(record, result, error.message());
        } else {
            *result = error.code();
        }
        if (record && record->bigDocumentData && selector == filterSelectorStart) {
            set_rects(record, make_rect(0, 0, 0, 0), make_rect(0, 0, 0, 0));
        }
    } catch (const std::bad_alloc&) {
        *result = memFullErr;
    } catch (...) {
        report_error(record, result, "FlareSim hit an unexpected error.");
    }
}
