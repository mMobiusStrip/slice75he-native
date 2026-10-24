// Win32 + Direct3D 11 host for the ImGui UI. Renders only while something changes,
// and otherwise sleeps in GetMessage so the app idles at ~0% CPU.
#include <windows.h>
#include <d3d11.h>
#include <dbt.h>
#include <dwmapi.h>
#include <hidsdi.h>

#include "app.h"
#include "i18n.h"
#include "imgui.h"
#include "version.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr UINT_PTR kDeviceRescanTimer = 1;
constexpr DWORD kActiveRenderMs = 400;  // keep rendering briefly after input so ImGui settles

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;
bool g_minimized = false;
DWORD g_lastActivity = 0;
float g_dpiScale = 1.0f;
bool g_fontReload = false;
bool g_freeFontInput = false;  // drop TTF data once the atlas texture exists (CJK font files are ~20 MB)

void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

void CleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swapChain, &g_device, &got, &g_context);
    }
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // Latin + Latin Extended-A covers Turkish and German; arrows/symbols are used by key labels. CJK glyphs are
    // added only for the characters the active language actually uses, which keeps the atlas small.
    static const ImWchar kBase[] = {0x0020, 0x017F, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2300, 0x23FF,
                                    0x25A0, 0x25FF, 0x3000, 0x303F, 0xFF01, 0xFF5E, 0};
    static ImVector<ImWchar> ranges;
    ranges.clear();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(kBase);
    builder.AddText(I18nGlyphText().c_str());
    builder.BuildRanges(&ranges);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * scale, &cfg, ranges.Data)) {
        cfg.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&cfg);
    }
    if (const char* cjk = I18nCjkFontPath()) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.OversampleH = 1;
        io.Fonts->AddFontFromFileTTF(cjk, 16.0f * scale, &merge, ranges.Data);  // fills only missing glyphs
    }
    g_freeFontInput = true;
    ImGui::GetStyle() = ImGuiStyle();
    AppApplyStyle();
    ImGui::GetStyle().ScaleAllSizes(scale);
}

void Render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    AppDraw();
    ImGui::Render();

    const float clear[4] = {0.07f, 0.07f, 0.08f, 1.0f};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapChain->Present(1, 0);

    if (g_freeFontInput) {  // the GPU texture now holds the glyphs; the source data is no longer needed
        g_freeFontInput = false;
        ImGui::GetIO().Fonts->ClearInputData();
        ImGui::GetIO().Fonts->ClearTexData();
    }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
        AppOnKey(msg, wParam, lParam)) {
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            g_minimized = wParam == SIZE_MINIMIZED;
            if (!g_minimized) {
                g_resizeW = LOWORD(lParam);
                g_resizeH = HIWORD(lParam);
            }
            return 0;
        case WM_DPICHANGED: {
            g_dpiScale = HIWORD(wParam) / 96.0f;
            const RECT* r = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            LoadFonts(g_dpiScale);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize = {static_cast<LONG>(960 * g_dpiScale), static_cast<LONG>(640 * g_dpiScale)};
            return 0;
        }
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
                SetTimer(hwnd, kDeviceRescanTimer, 300, nullptr);  // debounce bursts of interface events
            }
            return TRUE;
        case WM_TIMER:
            if (wParam == kDeviceRescanTimer) {
                KillTimer(hwnd, kDeviceRescanTimer);
                AppOnDeviceChange();
                return 0;
            }
            if (AppOnTimer(wParam)) return 0;
            break;
        case WM_APP_HID:
            AppOnHidNotify();
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT application menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void AppRequestRedraw() { g_lastActivity = GetTickCount(); }

void AppRequestFontReload() {
    g_fontReload = true;
    AppRequestRedraw();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    AppEarlyInit();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Slice75HEWindow";
    RegisterClassExW(&wc);

    g_dpiScale = GetDpiForSystem() / 96.0f;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Slice75 HE Native " TEXT(APP_VERSION_STR), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                static_cast<int>(1280 * g_dpiScale), static_cast<int>(900 * g_dpiScale), nullptr,
                                nullptr, inst, nullptr);
    g_dpiScale = GetDpiForWindow(hwnd) / 96.0f;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    if (!CreateDeviceD3D(hwnd)) {
        MessageBoxA(hwnd, T("Direct3D 11 başlatılamadı."), "Slice75 HE", MB_ICONERROR);
        return 1;
    }

    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    HidD_GetHidGuid(&filter.dbcc_classguid);
    HDEVNOTIFY notify = RegisterDeviceNotificationW(hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    LoadFonts(g_dpiScale);

    AppInit(hwnd);
    SetTimer(hwnd, kAppTickTimer, 200, nullptr);
    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);
    AppRequestRedraw();

    bool running = true;
    while (running) {
        // Block until a message arrives unless we are still animating.
        bool active = AppNeedsAnimation() || GetTickCount() - g_lastActivity < kActiveRenderMs;
        // The housekeeping tick does not force a frame; AppOnTimer requests one when state changed.
        auto dispatch = [](MSG& msg) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (!(msg.message == WM_TIMER && msg.wParam == kAppTickTimer)) AppRequestRedraw();
        };
        MSG msg;
        if (!active && !g_minimized) {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
            dispatch(msg);
            if (GetTickCount() - g_lastActivity >= kActiveRenderMs) continue;  // nothing to draw
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            dispatch(msg);
        }
        if (!running) break;
        if (g_minimized) {
            WaitMessage();
            continue;
        }
        if (g_fontReload) {  // language changed: rebuild the atlas before the next frame starts
            g_fontReload = false;
            LoadFonts(g_dpiScale);
            ImGui_ImplDX11_InvalidateDeviceObjects();
        }
        if (g_resizeW != 0 && g_resizeH != 0) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }
        Render();
    }

    AppShutdown();
    if (notify) UnregisterDeviceNotification(notify);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    return 0;
}
