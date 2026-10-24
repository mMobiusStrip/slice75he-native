// Configurator UI (Dear ImGui). All device I/O goes through Keyboard; this file only edits its mirrored state
// and queues the matching write packets.
#include "app.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "keyboard.h"
#include "keycodes.h"

using namespace proto;

namespace {

// ---------------------------------------------------------------------------------------------------------------
// State

enum class Tab { Keymap, Performance, Advanced, Macros, Lighting, Device };

struct Macro {
    std::string name;
    std::vector<MacroEvent> events;
};

HWND g_hwnd = nullptr;
Keyboard g_kb;
Tab g_tab = Tab::Keymap;
std::set<uint8_t> g_sel;
uint8_t g_anchor = 0;  // last clicked key; drives the editors
int g_layer = 0;
std::string g_lastStatus;

// Key picking for RS / SOCD partner keys: the next keyboard click fills this instead of selecting.
uint8_t* g_pickTarget = nullptr;

// Advanced-key editor buffer, reloaded whenever the anchor key changes.
struct AdvEdit {
    uint8_t key = 0;
    int type = ADV_NONE;
    uint16_t codes[4] = {};
    uint8_t trps[4] = {};
    uint16_t depths[3] = {500, 1000, 1500};
    uint16_t dksDb1 = 1400, dksDb2 = 3000;
    uint16_t delayMs = 200;
    uint8_t partner = 0;
    int socdMode = 0;
    int socdType = 0;
} g_adv;

std::vector<Macro> g_macros;
int g_macroSel = -1;
bool g_recording = false;
DWORD g_lastRecordAt = 0;
int g_bindMode = 0;
int g_bindRepeat = 1;
int g_bindDelay = 100;

bool g_confirmReset = false;

const char* const kAdvNames[] = {"Yok", "DKS", "MPT", "MT", "TGL", "END", "Makro", "", "SOCD", "RS"};
const char* const kAdvTags[] = {"", "DKS", "MPT", "MT", "TGL", "END", "MCR", "", "SOCD", "RS"};

// ---------------------------------------------------------------------------------------------------------------
// Helpers

DeviceState& S() { return g_kb.State(); }
bool Ready() { return S().conn == ConnState::Ready; }

ImU32 Rgb(uint32_t c, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, a));
}

bool ColorEdit(const char* label, uint32_t* c, bool* committed) {
    float f[3] = {((*c >> 16) & 0xFF) / 255.f, ((*c >> 8) & 0xFF) / 255.f, (*c & 0xFF) / 255.f};
    bool changed = ImGui::ColorEdit3(label, f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayHex);
    if (changed) {
        auto ch = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
        *c = (ch(f[0]) << 16) | (ch(f[1]) << 8) | ch(f[2]);
    }
    *committed = ImGui::IsItemDeactivatedAfterEdit();
    return changed;
}

// Slider in millimetres over a micrometre value; returns true when the user finishes an edit.
bool SliderMm(const char* label, uint16_t* um, int minUm, int maxUm, int stepUm) {
    stepUm = std::max(stepUm, 1);
    maxUm = std::max(maxUm, minUm + stepUm);
    int decimals = stepUm >= 100 ? 1 : stepUm >= 10 ? 2 : 3;
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df mm", decimals);
    float v = *um / 1000.f;
    if (ImGui::SliderFloat(label, &v, minUm / 1000.f, maxUm / 1000.f, fmt)) {
        int raw = static_cast<int>(std::lround(v * 1000));
        raw = minUm + static_cast<int>(std::lround((raw - minUm) / static_cast<double>(stepUm))) * stepUm;
        *um = static_cast<uint16_t>(std::clamp(raw, minUm, maxUm));
    }
    return ImGui::IsItemDeactivatedAfterEdit();
}

std::vector<uint8_t> Selected() {
    std::vector<uint8_t> out;
    for (uint8_t kv : g_sel) {
        if (S().keys[kv].present) out.push_back(kv);
    }
    return out;
}

void SelectOnly(uint8_t kv) {
    g_sel.clear();
    g_sel.insert(kv);
    g_anchor = kv;
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Macro library (stored locally; the keyboard cannot return macro bodies)

std::wstring MacroFile() {
    PWSTR base = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base))) {
        path = std::wstring(base) + L"\\Slice75HE";
        CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\macros.txt";
    }
    CoTaskMemFree(base);
    return path;
}

void SaveMacros() {
    std::ofstream f(MacroFile(), std::ios::binary | std::ios::trunc);
    for (const Macro& m : g_macros) {
        f << "macro\t" << m.name << "\n";
        for (const MacroEvent& e : m.events) f << e.code << ' ' << (e.down ? 'd' : 'u') << ' ' << e.delayMs << "\n";
    }
}

void LoadMacros() {
    g_macros.clear();
    std::ifstream f(MacroFile(), std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("macro\t", 0) == 0) {
            g_macros.push_back({line.substr(6), {}});
        } else if (!g_macros.empty() && !line.empty()) {
            std::istringstream in(line);
            unsigned code = 0, delay = 0;
            char dir = 'd';
            if (in >> code >> dir >> delay) {
                g_macros.back().events.push_back({static_cast<uint16_t>(code), dir == 'd', delay});
            }
        }
    }
    if (!g_macros.empty()) g_macroSel = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Keyboard drawing. Units are key widths ("u").

struct KeyGeom {
    float x, y, w;
};

// Physical Slice75 layout (16u wide 75%), traced from the product render. Keys of each matrix row are placed in
// column order; if a firmware reports a different key count per row we fall back to the web app's flow tables.
constexpr float kRow0Gap = 0.25f;
constexpr float kBoardW = 16.0f;
constexpr float kBoardH = 6.0f + kRow0Gap;
const std::vector<std::vector<KeyGeom>> kSlice75 = {
    {{0, 0, 1}, {1.25f, 0, 1}, {2.25f, 0, 1}, {3.25f, 0, 1}, {4.25f, 0, 1}, {5.5f, 0, 1}, {6.5f, 0, 1},
     {7.5f, 0, 1}, {8.5f, 0, 1}, {9.75f, 0, 1}, {10.75f, 0, 1}, {11.75f, 0, 1}, {12.75f, 0, 1}, {14, 0, 1},
     {15, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {4, 1, 1}, {5, 1, 1}, {6, 1, 1}, {7, 1, 1}, {8, 1, 1},
     {9, 1, 1}, {10, 1, 1}, {11, 1, 1}, {12, 1, 1}, {13, 1, 2}, {15, 1, 1}},
    {{0, 2, 1.5f}, {1.5f, 2, 1}, {2.5f, 2, 1}, {3.5f, 2, 1}, {4.5f, 2, 1}, {5.5f, 2, 1}, {6.5f, 2, 1},
     {7.5f, 2, 1}, {8.5f, 2, 1}, {9.5f, 2, 1}, {10.5f, 2, 1}, {11.5f, 2, 1}, {12.5f, 2, 1}, {13.5f, 2, 1.5f},
     {15, 2, 1}},
    {{0, 3, 1.75f}, {1.75f, 3, 1}, {2.75f, 3, 1}, {3.75f, 3, 1}, {4.75f, 3, 1}, {5.75f, 3, 1}, {6.75f, 3, 1},
     {7.75f, 3, 1}, {8.75f, 3, 1}, {9.75f, 3, 1}, {10.75f, 3, 1}, {11.75f, 3, 1}, {12.75f, 3, 2.25f}},
    {{0, 4, 2.25f}, {2.25f, 4, 1}, {3.25f, 4, 1}, {4.25f, 4, 1}, {5.25f, 4, 1}, {6.25f, 4, 1}, {7.25f, 4, 1},
     {8.25f, 4, 1}, {9.25f, 4, 1}, {10.25f, 4, 1}, {11.25f, 4, 1}, {12.25f, 4, 1.75f}, {14, 4, 1}},
    {{0, 5, 1.25f}, {1.25f, 5, 1.25f}, {2.5f, 5, 1.25f}, {3.75f, 5, 6.25f}, {10, 5, 1.25f}, {11.25f, 5, 1.25f},
     {13, 5, 1}, {14, 5, 1}, {15, 5, 1}},
};

const float kWidth80B[kRows][kCols] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1},
    {1.5f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5f, 1, 1, 1, 1, 1, 1, 1},
    {1.8f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.2f, 1, 1, 1, 1, 1, 1, 1},
    {2.3f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.7f, 1, 1, 1, 1, 1, 1, 1},
    {1.3f, 1.2f, 1.3f, 1, 1, 1, 6.2f, 1, 1, 1, 1, 1.2f, 1.2f, 1, 1, 1, 1, 1, 1, 1, 1},
};
const float kMarginLeft80B[kCols] = {0, .1f, 0, 0, 0, .2f, 0, 0, 0, .2f, 0, 0, 0, .15f, 0, 0, 0, 0, 0, 0, 0};
const float kMarginRight80B[kRows][kCols] = {
    {.2f, 0, .1f, 0, .15f, .05f, .05f, .05f, .1f, .05f, .05f, .05f, .15f, .1f, 0, 0, 0, 0, 0, 0, 0},
    {.1f, 0, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, 0, 0, 0, 0, 0, 0},
    {.1f, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, .2f, 0, 0, .1f, 0, 0, 0, 0, 0, 0, 0},
    {.05f, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, .1f, 0, 0, .3f, 0, 0, 0, 0, 0, 0, 0},
    {.1f, 0, .1f, 0, 0, .1f, 0, .1f, 0, .1f, 0, .1f, .15f, .1f, 1, 1, 0, 0, 0, 0, 0},
    {0, .1f, .1f, 0, 0, 0, .3f, 0, 0, 0, 0, .15f, .65f, 0, 0, 0, 0, 0, 0, 0, 0},
};
// Returns true when the Slice75 physical layout matched the matrix (logo plate and case details are drawn then).
bool ComputeGeometry(KeyGeom out[kRows][kCols], float* totalW, float* totalH) {
    bool physical = true;
    for (int r = 0; r < kRows && physical; ++r) {
        size_t n = 0;
        for (int c = 0; c < kCols; ++c) n += S().matrix[r][c] != 0;
        physical = n == kSlice75[r].size();
    }

    *totalW = 0;
    float y = 0;
    for (int r = 0; r < kRows; ++r) {
        float x = 0;
        size_t i = 0;
        for (int c = 0; c < kCols; ++c) {
            out[r][c] = {0, 0, 0};
            if (S().matrix[r][c] == 0) continue;
            if (physical) {
                KeyGeom g = kSlice75[r][i++];
                if (r > 0) g.y += kRow0Gap;
                out[r][c] = g;
                *totalW = std::max(*totalW, g.x + g.w);
                continue;
            }
            if (r == 0) x += kMarginLeft80B[c];
            out[r][c] = {x, y, kWidth80B[r][c]};
            x += kWidth80B[r][c] + kMarginRight80B[r][c];
            *totalW = std::max(*totalW, x);
        }
        y += 1.0f + (r == 0 ? kRow0Gap : 0.0f);
    }
    *totalH = y;
    return physical;
}

// Case / keycap colours per case colour reported by the keyboard (0 black, 1 gray, 2 white, 3 all black, 4 pink).
struct BoardTheme {
    uint32_t caseTop, caseEdge, plate, capSide, capTop, legend;
};

BoardTheme ThemeFor(uint8_t caseColor) {
    switch (caseColor) {
        case 1: return {0x5E6167, 0x45474C, 0x2A2B2F, 0x232428, 0x3A3C42, 0xE4E4E8};
        case 2: return {0xDCDCE0, 0xB9BABF, 0xC6C7CC, 0xC9CAD0, 0xF4F4F6, 0x2E2F34};
        case 3: return {0x1D1D20, 0x101012, 0x0A0A0B, 0x111113, 0x24242A, 0xC4C5CB};
        case 4: return {0xEDBFCB, 0xD59DAC, 0xE3C7CF, 0xE2D5D9, 0xFAF1F3, 0x5A3C45};
        default: return {0x313238, 0x1F2023, 0x111214, 0x1C1D21, 0x36373E, 0xE2E2E8};
    }
}

uint32_t Mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) {
        float v = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
        return static_cast<uint32_t>(std::clamp(v, 0.f, 255.f)) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}

float Luma(uint32_t c) { return (((c >> 16) & 0xFF) * 0.3f + ((c >> 8) & 0xFF) * 0.59f + (c & 0xFF) * 0.11f) / 255.f; }

std::string KeyFace(const KeyState& k, uint8_t kv, std::string* sub) {
    const DeviceState& s = S();
    char buf[32];
    switch (g_tab) {
        case Tab::Keymap:
            *sub = kAdvTags[std::min<int>(k.advType, 9)];
            return KeyCodeLabel(k.layers[g_layer]);
        case Tab::Performance:
            if (g_kb.GetLiveMode() == LiveMode::TravelTest) {
                snprintf(buf, sizeof(buf), "%.2f", k.liveTravel / 1000.f);
                *sub = buf;
            } else {
                uint16_t act = k.touchMode == TOUCH_GLOBAL ? s.globalActuation : k.db[0];
                snprintf(buf, sizeof(buf), "%.2f", act / 1000.f);
                *sub = std::string(k.touchMode == TOUCH_RAPID ? "RT " : k.touchMode == TOUCH_SINGLE ? "• " : "") + buf;
            }
            return KeyCodeLabel(kv);
        case Tab::Device:
            if (g_kb.GetLiveMode() == LiveMode::Calibration) {
                snprintf(buf, sizeof(buf), "%.2f", k.liveTravel / 1000.f);
                *sub = buf;
            }
            return KeyCodeLabel(kv);
        default:
            *sub = kAdvTags[std::min<int>(k.advType, 9)];
            return KeyCodeLabel(kv);
    }
}

// Backlight colour shown under a key in the Lighting tab, or 0 alpha when the light is off.
bool KeyGlow(const KeyState& k, float xFrac, uint32_t* color, float* strength) {
    const LightState& L = S().mainLight;
    if (g_tab != Tab::Lighting || !L.on) return false;
    *strength = 0.35f + 0.65f * std::clamp(L.brightness / 4.f, 0.f, 1.f);
    if (L.mode == 21) {
        *color = k.color;
    } else if (L.mode == 0 && L.staticSlot < 7) {
        *color = L.colors[L.staticSlot];
    } else {
        // Effects and the rainbow slot: hint with a hue sweep across the board.
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(xFrac * 0.85f, 0.8f, 1.f, r, g, b);
        *color = (static_cast<uint32_t>(r * 255) << 16) | (static_cast<uint32_t>(g * 255) << 8) |
                 static_cast<uint32_t>(b * 255);
    }
    return true;
}

void DrawKeyboard() {
    DeviceState& s = S();
    KeyGeom geom[kRows][kCols];
    float totalW = 0, totalH = 0;
    const bool physical = ComputeGeometry(geom, &totalW, &totalH);
    if (totalW <= 0) {
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 8));
        return;
    }
    if (physical) {
        totalW = kBoardW;
        totalH = kBoardH;
    }

    constexpr float kCasePad = 0.34f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float u = std::min(avail / (totalW + 2 * kCasePad), ImGui::GetFontSize() * 3.0f);
    const float boardPxW = (totalW + 2 * kCasePad) * u;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 caseMin = start + ImVec2((avail - boardPxW) * 0.5f, u * 0.05f);
    const ImVec2 origin = caseMin + ImVec2(kCasePad * u, kCasePad * u);
    const ImVec2 caseMax = origin + ImVec2((totalW + kCasePad) * u, (totalH + kCasePad) * u);
    auto P = [&](float x, float y) { return origin + ImVec2(x * u, y * u); };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const BoardTheme th = ThemeFor(s.caseColor);
    const bool calibrating = g_kb.GetLiveMode() == LiveMode::Calibration;
    const bool testing = g_kb.GetLiveMode() == LiveMode::TravelTest;
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);

    // Case: drop shadow, side wall, top face, top-edge highlight.
    const float caseR = u * 0.24f;
    dl->AddRectFilled(caseMin + ImVec2(0, u * 0.12f), caseMax + ImVec2(0, u * 0.16f), IM_COL32(0, 0, 0, 90), caseR);
    dl->AddRectFilled(caseMin + ImVec2(0, u * 0.06f), caseMax + ImVec2(0, u * 0.08f), Rgb(th.caseEdge), caseR);
    dl->AddRectFilled(caseMin, caseMax, Rgb(th.caseTop), caseR);
    dl->AddLine(caseMin + ImVec2(caseR, 1), ImVec2(caseMax.x - caseR, caseMin.y + 1), Rgb(Mix(th.caseTop, 0xFFFFFF, 0.18f)),
                1.0f);

    // Recessed plate wells around key groups (the outlines visible on the real case).
    auto well = [&](float x0, float y0, float x1, float y1) {
        const float m = 0.06f;
        dl->AddRectFilled(P(x0 - m, y0 - m), P(x1 + m, y1 + m), Rgb(th.plate), u * 0.12f);
        dl->AddRect(P(x0 - m, y0 - m), P(x1 + m, y1 + m), Rgb(Mix(th.plate, 0xFFFFFF, 0.08f)), u * 0.12f, 0, 1.0f);
    };
    if (physical) {
        well(0, 0, 1, 1);
        well(1.25f, 0, 5.25f, 1);
        well(5.5f, 0, 9.5f, 1);
        well(9.75f, 0, 13.75f, 1);
        well(14, 0, 16, 1);
        well(0, 1 + kRow0Gap, 16, kBoardH);
        // Logo plate in the right column next to Enter.
        ImVec2 l0 = P(15.08f, 3 + kRow0Gap + 0.08f), l1 = P(15.92f, 4 + kRow0Gap - 0.08f);
        dl->AddRectFilled(l0, l1, Rgb(Mix(th.caseTop, 0x000000, 0.25f)), u * 0.1f);
        dl->AddRect(l0, l1, Rgb(Mix(th.caseTop, 0xFFFFFF, 0.25f)), u * 0.1f, 0, 1.0f);
        const char* logo = "HE";
        float lfs = u * 0.32f;
        ImVec2 ls = ImGui::GetFont()->CalcTextSizeA(lfs, FLT_MAX, 0, logo);
        dl->AddText(ImGui::GetFont(), lfs, (l0 + l1 - ls) * 0.5f, Rgb(Mix(th.caseTop, 0xFFFFFF, 0.45f)), logo);
    } else {
        well(0, 0, totalW, totalH);
    }

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            uint8_t kv = s.matrix[r][c];
            if (kv == 0) continue;
            const KeyState& k = s.keys[kv];
            const KeyGeom& g = geom[r][c];
            const float gap = 0.045f;
            ImVec2 p0 = P(g.x + gap, g.y + gap);
            ImVec2 p1 = P(g.x + g.w - gap, g.y + 1 - gap);

            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(kv);
            ImGui::InvisibleButton("key", p1 - p0);
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                if (g_pickTarget) {
                    *g_pickTarget = kv;
                    g_pickTarget = nullptr;
                } else if (ImGui::GetIO().KeyCtrl) {
                    if (!g_sel.erase(kv)) g_sel.insert(kv);
                    g_anchor = kv;
                } else if (ImGui::GetIO().KeyShift) {
                    g_sel.insert(kv);
                    g_anchor = kv;
                } else {
                    SelectOnly(kv);
                }
            }
            ImGui::PopID();
            const bool selected = g_sel.count(kv) != 0;

            // Live depth pushes the cap top down a little and fills it from the bottom.
            float depth = 0;
            if (testing || calibrating) {
                depth = std::clamp(k.liveTravel / static_cast<float>(std::max<uint16_t>(s.KeyMaxTravel(k), 1)), 0.f, 1.f);
            }

            const float capR = u * 0.13f;
            uint32_t glow = 0;
            float glowK = 0;
            const bool lit = KeyGlow(k, (g.x + g.w * 0.5f) / totalW, &glow, &glowK);
            if (lit) {
                dl->AddRectFilled(p0 - ImVec2(u * 0.07f, u * 0.07f), p1 + ImVec2(u * 0.07f, u * 0.07f),
                                  Rgb(glow, 0.28f * glowK), capR * 1.6f);
                dl->AddRectFilled(p0 - ImVec2(u * 0.035f, u * 0.035f), p1 + ImVec2(u * 0.035f, u * 0.035f),
                                  Rgb(glow, 0.55f * glowK), capR * 1.3f);
            }

            uint32_t side = th.capSide, top = th.capTop;
            if (lit && s.mainLight.mode == 21) {
                side = Mix(side, glow, 0.35f * glowK);
                top = Mix(top, glow, 0.5f * glowK);
            }
            if (hovered) top = Mix(top, Luma(top) > 0.6f ? 0x000000 : 0xFFFFFF, 0.08f);

            dl->AddRectFilled(p0 + ImVec2(0, u * 0.03f), p1 + ImVec2(0, u * 0.03f), IM_COL32(0, 0, 0, 70), capR);
            dl->AddRectFilled(p0, p1, Rgb(side), capR);

            const float press = depth * u * 0.05f;
            ImVec2 t0 = p0 + ImVec2(u * 0.09f, u * 0.05f + press);
            ImVec2 t1 = p1 - ImVec2(u * 0.09f, u * 0.15f - press);
            dl->AddRectFilled(t0, t1, Rgb(top), capR * 0.8f);
            dl->AddLine(t0 + ImVec2(capR * 0.6f, 0.5f), ImVec2(t1.x - capR * 0.6f, t0.y + 0.5f),
                        Rgb(Mix(top, 0xFFFFFF, 0.14f)), 1.0f);

            if (depth > 0) {
                bool done = calibrating && k.liveTravel + 10 >= s.KeyMaxTravel(k);
                ImU32 bar = done ? IM_COL32(70, 200, 120, 150)
                                 : (testing && k.livePress >= 1 && k.livePress <= 7 ? IM_COL32(245, 150, 45, 160)
                                                                                    : IM_COL32(80, 150, 240, 130));
                dl->AddRectFilled(ImVec2(t0.x, t1.y - (t1.y - t0.y) * depth), t1, bar, capR * 0.8f);
            }
            if (selected) {
                dl->AddRect(p0 - ImVec2(1.5f, 1.5f), p1 + ImVec2(1.5f, 1.5f), accent, capR * 1.15f, 0, 2.2f);
            }

            // Legends sit on the cap top; long names shrink to fit.
            std::string sub;
            std::string label = KeyFace(k, kv, &sub);
            // Lit keys get shine-through legends in the backlight colour.
            ImU32 legend = Luma(top) > 0.6f ? IM_COL32(35, 35, 40, 255) : Rgb(th.legend);
            if (lit && Luma(top) <= 0.6f) legend = Rgb(Mix(th.legend, Mix(glow, 0xFFFFFF, 0.25f), 0.8f * glowK));
            const float maxW = (t1.x - t0.x) - u * 0.08f;
            float fs = std::min(ImGui::GetFontSize(), u * 0.3f);
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str());
            if (ts.x > maxW) {
                fs *= maxW / ts.x;
                ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str());
            }
            const float cx = (t0.x + t1.x) * 0.5f;
            if (sub.empty()) {
                dl->AddText(ImGui::GetFont(), fs, ImVec2(cx - ts.x * 0.5f, (t0.y + t1.y - ts.y) * 0.5f), legend,
                            label.c_str());
            } else {
                float sfs = std::min(fs * 0.8f, u * 0.22f);
                ImVec2 ss = ImGui::GetFont()->CalcTextSizeA(sfs, FLT_MAX, 0, sub.c_str());
                float block = ts.y + ss.y;
                float y = (t0.y + t1.y - block) * 0.5f;
                dl->AddText(ImGui::GetFont(), fs, ImVec2(cx - ts.x * 0.5f, y), legend, label.c_str());
                dl->AddText(ImGui::GetFont(), sfs, ImVec2(cx - ss.x * 0.5f, y + ts.y), accent, sub.c_str());
            }
        }
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(avail, (caseMax.y - start.y) + u * 0.3f));
}

void SelectionToolbar() {
    DeviceState& s = S();
    auto selectCodes = [&](std::initializer_list<uint8_t> kvs) {
        g_sel.clear();
        for (uint8_t kv : kvs) {
            if (s.keys[kv].present) g_sel.insert(kv);
        }
        if (!g_sel.empty()) g_anchor = *g_sel.begin();
    };
    if (ImGui::SmallButton("Tümünü seç")) {
        for (uint8_t kv : s.AllKeys()) g_sel.insert(kv);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Seçimi temizle")) g_sel.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("WASD")) selectCodes({0x1A, 0x04, 0x16, 0x07});
    ImGui::SameLine();
    if (ImGui::SmallButton("Harfler")) {
        g_sel.clear();
        for (uint8_t kv = 0x04; kv <= 0x1D; ++kv) {
            if (s.keys[kv].present) g_sel.insert(kv);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu tuş seçili  ·  Ctrl+tık: ekle/çıkar", Selected().size());
    if (g_pickTarget) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "→ Eş tuşu klavyede tıklayın");
        ImGui::SameLine();
        if (ImGui::SmallButton("Vazgeç")) g_pickTarget = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Shared widgets (one visual language for every tab: cards, segmented selectors, tiles, switches)

constexpr float kSideWidth = 17.0f;  // right-hand settings card, in font sizes

bool StyledButton(const char* label, ImVec2 size, ImVec4 base) {
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(base.x * 1.12f, base.y * 1.12f, base.z * 1.12f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(base.x * 0.85f, base.y * 0.85f, base.z * 0.85f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (size.y == 0) size.y = ImGui::GetFrameHeight() * 1.2f;
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    return StyledButton(label, size, ImVec4(0.80f, 0.45f, 0.15f, 1));
}

bool DangerButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    return StyledButton(label, size, ImVec4(0.72f, 0.22f, 0.20f, 1));
}

bool WideButton(const char* label) {
    return ImGui::Button(label, ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.2f));
}

// Row of joined toggle buttons; returns true when the value changed.
bool Segmented(const char* id, const char* const* labels, int n, int* value, float width) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 0));
    const float w = (width - 3.0f * (n - 1)) / n;
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine();
        const bool sel = *value == i;
        const ImU32 bg = ImGui::GetColorU32(sel ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? bg : ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
        ImGui::PushID(i);
        if (ImGui::Button(labels[i], ImVec2(w, ImGui::GetFrameHeight() * 1.15f)) && !sel) {
            *value = i;
            changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// iOS-style switch.
bool Toggle(const char* id, bool* v) {
    const float h = ImGui::GetFrameHeight() * 0.85f, w = h * 1.85f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (clicked) *v = !*v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h),
                      *v ? ImGui::GetColorU32(ImGuiCol_CheckMark) : ImGui::GetColorU32(ImGuiCol_FrameBgActive), h * 0.5f);
    float cx = *v ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), h * 0.5f - 3.0f, IM_COL32(245, 245, 248, 255));
    return clicked;
}

// Label on the left, switch on the right edge of the current column.
bool ToggleRow(const char* label, bool* v, const char* help = nullptr) {
    const float startX = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = ImGui::GetFrameHeight() * 0.85f * 1.85f;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) HelpMarker(help);
    ImGui::SameLine(startX + avail - w);
    return Toggle(label, v);
}

void CardTitle(const char* title) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void BeginCard(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.105f, 0.12f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::BeginChild(id, ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Two-column page: stretchy main area and a fixed settings column on the right.
bool BeginPage(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("main", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("side", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * kSideWidth);
    ImGui::TableNextColumn();
    return true;
}

void PageSide() { ImGui::TableNextColumn(); }
void EndPage() { ImGui::EndTable(); }

// Millimetre slider with its label above, filling the column.
bool MmSlider(const char* label, uint16_t* um, int minUm, int maxUm, int stepUm) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool r = SliderMm("##s", um, minUm, maxUm, stepUm);
    ImGui::PopID();
    ImGui::Spacing();
    return r;
}

// Large text line (key names in the side cards).
void BigText(const char* text) {
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
}

void HueStrip(ImDrawList* dl, ImVec2 a, ImVec2 b, float hue0, float hue1, float sat = 0.85f) {
    const int steps = 6;
    for (int i = 0; i < steps; ++i) {
        float t0 = i / static_cast<float>(steps), t1 = (i + 1) / static_cast<float>(steps);
        ImVec4 c0, c1;
        ImGui::ColorConvertHSVtoRGB(std::fmod(hue0 + (hue1 - hue0) * t0 + 1.f, 1.f), sat, 1.f, c0.x, c0.y, c0.z);
        ImGui::ColorConvertHSVtoRGB(std::fmod(hue0 + (hue1 - hue0) * t1 + 1.f, 1.f), sat, 1.f, c1.x, c1.y, c1.z);
        c0.w = c1.w = 1;
        ImU32 u0 = ImGui::ColorConvertFloat4ToU32(c0), u1 = ImGui::ColorConvertFloat4ToU32(c1);
        dl->AddRectFilledMultiColor(ImVec2(a.x + (b.x - a.x) * t0, a.y), ImVec2(a.x + (b.x - a.x) * t1 + 0.5f, b.y), u0,
                                    u1, u1, u0);
    }
}

// Selectable card with a title, an optional description and an optional colour band.
bool OptionCard(const char* id, const char* title, const char* desc, bool selected, ImVec2 size, float hue = -1) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImVec2 p1 = p0 + size;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1,
                      ImGui::GetColorU32(selected ? ImGuiCol_Header : hov ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                      7.0f);
    if (selected) dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_CheckMark), 7.0f, 0, 2.0f);
    const float fs = ImGui::GetFontSize();
    float y = p0.y + 8;
    if (hue >= 0) {
        HueStrip(dl, ImVec2(p0.x + 8, y), ImVec2(p1.x - 8, y + fs * 0.35f), hue, hue + 0.35f, 0.75f);
        y += fs * 0.35f + 6;
    }
    ImGui::PushClipRect(p0, p1 - ImVec2(6, 0), true);
    dl->AddText(ImVec2(p0.x + 10, y), ImGui::GetColorU32(ImGuiCol_Text), title);
    if (desc && desc[0]) {
        dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(p0.x + 10, y + fs + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    desc, nullptr, size.x - 20);
    }
    ImGui::PopClipRect();
    return clicked;
}

// Keycap-looking button for keycode pickers.
bool KeyTile(const char* id, const char* label, bool selected, ImVec2 size) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered();
    ImVec2 p1 = p0 + size;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.16f;
    dl->AddRectFilled(p0, p1, Rgb(selected ? 0x7A4314 : 0x1C1D21), r);
    ImVec2 t0 = p0 + ImVec2(size.x * 0.07f, size.y * 0.06f), t1 = p1 - ImVec2(size.x * 0.07f, size.y * 0.16f);
    dl->AddRectFilled(t0, t1, Rgb(selected ? 0xB8661F : hov ? 0x44464E : 0x36373E), r * 0.8f);
    dl->AddLine(t0 + ImVec2(r, 0.5f), ImVec2(t1.x - r, t0.y + 0.5f), Rgb(selected ? 0xD98A45 : 0x4A4C54), 1.0f);
    float fsz = ImGui::GetFontSize();
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fsz, FLT_MAX, 0, label);
    const float maxW = (t1.x - t0.x) - 6;
    if (ts.x > maxW) {
        fsz *= maxW / ts.x;
        ts = ImGui::GetFont()->CalcTextSizeA(fsz, FLT_MAX, 0, label);
    }
    dl->AddText(ImGui::GetFont(), fsz, (t0 + t1 - ts) * 0.5f, IM_COL32(232, 232, 238, 255), label);
    return clicked;
}

// Square colour tile. Returns 1 on click, 2 on double-click / right-click (edit request).
int ColorTile(const char* id, uint32_t color, bool selected, float size, bool rainbow = false) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(size, size), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    int result = 0;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) result = 1;
    if ((hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        result = 2;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1 = p0 + ImVec2(size, size);
    const float r = size * 0.2f;
    if (rainbow) {
        ImVec2 i0 = p0 + ImVec2(size * 0.08f, size * 0.08f), i1 = p1 - ImVec2(size * 0.08f, size * 0.08f);
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), r);
        HueStrip(dl, i0, i1, 0.f, 0.9f);
    } else {
        dl->AddRectFilled(p0, p1, Rgb(color), r);
    }
    if (selected) {
        dl->AddRect(p0 - ImVec2(3, 3), p1 + ImVec2(3, 3), ImGui::GetColorU32(ImGuiCol_CheckMark), r + 3, 0, 2.5f);
    } else if (hovered) {
        dl->AddRect(p0 - ImVec2(2, 2), p1 + ImVec2(2, 2), IM_COL32(255, 255, 255, 110), r + 2, 0, 1.5f);
    } else {
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 30), r, 0, 1.0f);
    }
    return result;
}

// Colour editor shown in a popup; returns true when an edit was committed.
bool ColorEditPopup(const char* popupId, uint32_t* color) {
    bool committed = false;
    if (ImGui::BeginPopup(popupId)) {
        float f[3] = {((*color >> 16) & 0xFF) / 255.f, ((*color >> 8) & 0xFF) / 255.f, (*color & 0xFF) / 255.f};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 13);
        if (ImGui::ColorPicker3("##picker", f,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                    ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar)) {
            auto ch = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
            *color = (ch(f[0]) << 16) | (ch(f[1]) << 8) | ch(f[2]);
        }
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        static const uint32_t kPresets[] = {0xFF0000, 0xFF6A00, 0xFFD000, 0x00FF00, 0x00FFC8,
                                            0x00A0FF, 0x0000FF, 0x8000FF, 0xFF00C8, 0xFFFFFF};
        const float sz = ImGui::GetFontSize() * 1.15f;
        for (int i = 0; i < 10; ++i) {
            if (i % 5) ImGui::SameLine(0, 5);
            ImGui::PushID(i);
            if (ColorTile("p", kPresets[i], *color == kPresets[i], sz) == 1) {
                *color = kPresets[i];
                committed = true;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Tamam", ImVec2(-FLT_MIN, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return committed;
}

// ---------------------------------------------------------------------------------------------------------------
// Keycode picker

struct CatDef {
    const char* name;
    KeyCat cat;
};
const CatDef kCats[] = {
    {"Harf", KeyCat::Letter},       {"Sayı", KeyCat::Number},     {"Sembol", KeyCat::Symbol},
    {"Temel", KeyCat::Basic},       {"Fonksiyon", KeyCat::Function}, {"Kontrol", KeyCat::Control},
    {"Fare", KeyCat::Mouse},
};
constexpr int kCatCount = sizeof(kCats) / sizeof(kCats[0]);

// Category selector + keycap tile grid; returns true and sets *out when a tile is clicked.
bool KeyCodeGrid(const char* id, uint16_t* out, bool advanced) {
    bool chosen = false;
    ImGui::PushID(id);
    int* cat = ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("cat"), 0);
    const char* names[kCatCount];
    for (int i = 0; i < kCatCount; ++i) names[i] = kCats[i].name;
    Segmented("cats", names, kCatCount, cat, ImGui::GetContentRegionAvail().x);
    ImGui::Spacing();

    const float fs = ImGui::GetFontSize();
    const ImVec2 tile(fs * 3.9f, fs * 2.5f);
    const float gap = 6;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((avail + gap) / (tile.x + gap)));
    const KeyCat want = kCats[*cat].cat;
    int n = 0;
    for (size_t i = 0; i < kKeyCodeCount; ++i) {
        const KeyCodeInfo& k = kKeyCodes[i];
        bool match = k.cat == want || (advanced && want == KeyCat::Function && k.cat == KeyCat::FunctionAdv);
        if (!match) continue;
        if (n++ % perRow != 0) ImGui::SameLine(0, gap);
        ImGui::PushID(k.code);
        if (KeyTile("k", k.label, *out == k.code, tile)) {
            *out = k.code;
            chosen = true;
        }
        if (k.hint[0] && ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(k.hint);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return chosen;
}

// Button showing a keycode; opens a popup picker. Returns true when the code changed.
bool KeyCodeButton(const char* id, uint16_t* code, float width = 0) {
    ImGui::PushID(id);
    std::string label = *code ? KeyCodeLabel(*code) : std::string("— seç —");
    if (width <= 0) width = ImGui::GetFontSize() * 7;
    if (ImGui::Button((label + "###b").c_str(), ImVec2(width, ImGui::GetFrameHeight() * 1.1f))) ImGui::OpenPopup("pick");
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 38, ImGui::GetFontSize() * 22));
    if (ImGui::BeginPopup("pick")) {
        if (ImGui::Button("Boş bırak")) {
            *code = 0;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        if (KeyCodeGrid("grid", code, true)) {
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------------------------------------------
// Tabs

void NoSelectionHint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void KeymapTab() {
    DeviceState& s = S();
    std::vector<uint8_t> sel = Selected();
    if (!BeginPage("keymap")) return;

    static const char* const kLayers[] = {"Fn0 · Temel katman", "Fn1", "Fn2", "Fn3"};
    Segmented("layer", kLayers, 4, &g_layer, ImGui::GetContentRegionAvail().x);
    ImGui::Spacing();
    ImGui::Spacing();
    uint16_t code = sel.empty() ? 0 : s.keys[g_anchor].layers[g_layer];
    if (KeyCodeGrid("keymap", &code, false) && !sel.empty()) {
        std::vector<KeyRecord> recs;
        for (uint8_t kv : sel) {
            s.keys[kv].layers[g_layer] = code;
            recs.push_back({kv, static_cast<uint8_t>(F_FN0 + g_layer), code});
        }
        g_kb.WriteKeyRecords(recs);
    }

    PageSide();
    BeginCard("keyinfo");
    CardTitle("Seçili tuş");
    if (sel.empty() || !s.keys[g_anchor].present) {
        NoSelectionHint("Klavye görselinden bir veya daha fazla tuş seçin, sonra soldan yeni görevini tıklayın.");
    } else {
        const KeyState& k = s.keys[g_anchor];
        BigText(KeyCodeLabel(k.layers[g_layer]).c_str());
        ImGui::TextDisabled("Fiziksel tuş: %s", KeyCodeLabel(g_anchor).c_str());
        ImGui::TextDisabled("Katman: Fn%d", g_layer);
        if (sel.size() > 1) ImGui::TextDisabled("%zu tuş seçili", sel.size());
        if (k.advType != ADV_NONE) ImGui::TextDisabled("Gelişmiş: %s", kAdvNames[std::min<int>(k.advType, 9)]);
        ImGui::Spacing();
        if (WideButton("Varsayılana döndür")) {
            std::vector<KeyRecord> recs;
            for (uint8_t kv : sel) {
                uint16_t def = g_layer == 0 ? kv : 0;
                s.keys[kv].layers[g_layer] = def;
                recs.push_back({kv, static_cast<uint8_t>(F_FN0 + g_layer), def});
            }
            g_kb.WriteKeyRecords(recs);
        }
    }
    EndCard();
    ImGui::Spacing();
    BeginCard("layerhelp");
    CardTitle("Katmanlar");
    NoSelectionHint("Fn1–Fn3 katmanları, klavyedeki ilgili Fn tuşu basılıyken geçerli olur. "
                    "Bir tuşu Fn katmanına atamak için “Fonksiyon” sekmesindeki Fn1–Fn3'ü kullanın.");
    EndCard();
    EndPage();
}

void PerformanceTab() {
    DeviceState& s = S();
    const int step = s.precision;
    std::vector<uint8_t> sel = Selected();
    if (!BeginPage("perf")) return;

    BeginCard("keys");
    CardTitle(sel.empty() ? "Seçili tuşlar" : sel.size() == 1 ? "Seçili tuş" : "Seçili tuşlar");
    if (sel.empty() || !s.keys[g_anchor].present) {
        NoSelectionHint("Ayarlamak için klavye görselinden tuş seçin. “Tümünü seç” ile bütün klavyeyi, "
                        "“WASD” ile oyun tuşlarını hızlıca seçebilirsiniz.");
    } else {
        KeyState& a = s.keys[g_anchor];
        auto modeOf = [&](uint8_t kv, uint8_t touch) {
            return static_cast<uint16_t>((touch << 4) | s.keys[kv].advType);
        };
        auto applyMode = [&](uint8_t touch) {
            std::vector<KeyRecord> recs;
            for (uint8_t kv : sel) {
                KeyState& k = s.keys[kv];
                k.touchMode = touch;
                recs.push_back({kv, F_MODE, modeOf(kv, touch)});
                k.db[0] = touch == TOUCH_GLOBAL ? s.globalActuation : a.db[0];
                recs.push_back({kv, F_DB0, k.db[0]});
                if (touch == TOUCH_RAPID) {
                    k.rtPress = a.rtPress;
                    k.rtRelease = a.rtRelease;
                    recs.push_back({kv, F_RT_PRESS, k.rtPress});
                    recs.push_back({kv, F_RT_RELEASE, k.rtRelease});
                }
            }
            g_kb.WriteKeyRecords(recs);
        };

        static const char* const kModes[] = {"Genel ayar", "Tek tuş", "Rapid Trigger"};
        int touch = a.touchMode;
        if (Segmented("mode", kModes, 3, &touch, ImGui::GetContentRegionAvail().x)) {
            applyMode(static_cast<uint8_t>(touch));
        }
        ImGui::Spacing();
        ImGui::Spacing();

        const uint16_t keyMax = s.KeyMaxTravel(a);
        if (ImGui::BeginTable("cols", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            if (touch == TOUCH_GLOBAL) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Genel basma noktasını kullanıyor (%.2f mm).", s.globalActuation / 1000.f);
                NoSelectionHint(buf);
            } else if (MmSlider(touch == TOUCH_RAPID ? "İlk tetikleme noktası" : "Basma noktası", &a.db[0], s.minTravel,
                                keyMax, step)) {
                std::vector<KeyRecord> recs;
                for (uint8_t kv : sel) {
                    s.keys[kv].db[0] = a.db[0];
                    recs.push_back({kv, F_MODE, modeOf(kv, s.keys[kv].touchMode)});
                    recs.push_back({kv, F_DB0, a.db[0]});
                }
                g_kb.WriteKeyRecords(recs);
            }
            if (touch == TOUCH_RAPID) {
                bool rt = MmSlider("RT basma hassasiyeti", &a.rtPress, s.minTravel, keyMax, step);
                rt |= MmSlider("RT bırakma hassasiyeti", &a.rtRelease, s.minTravel, keyMax, step);
                if (rt) {
                    std::vector<KeyRecord> recs;
                    for (uint8_t kv : sel) {
                        KeyState& k = s.keys[kv];
                        k.rtPress = a.rtPress;
                        k.rtRelease = a.rtRelease;
                        recs.push_back({kv, F_MODE, modeOf(kv, TOUCH_RAPID)});
                        recs.push_back({kv, F_RT_PRESS, k.rtPress});
                        recs.push_back({kv, F_RT_RELEASE, k.rtRelease});
                    }
                    g_kb.WriteKeyRecords(recs);
                }
            }

            ImGui::TableNextColumn();
            if (s.SignalDead()) {
                auto dead = [&](const char* label, uint16_t KeyState::*field, uint8_t f) {
                    if (MmSlider(label, &(a.*field), 0, 1000, 10)) {
                        std::vector<KeyRecord> recs;
                        for (uint8_t kv : sel) {
                            s.keys[kv].*field = a.*field;
                            recs.push_back({kv, f, a.*field});
                        }
                        g_kb.WriteKeyRecords(recs);
                    }
                };
                dead("Basma ölü bölgesi", &KeyState::deadPress, F_DEAD_PRESS);
                dead("Bırakma ölü bölgesi", &KeyState::deadRelease, F_DEAD_RELEASE);
            }
            if (s.SignalSwitch() && !s.switches.empty()) {
                int axis = a.axis < s.switches.size() ? a.axis : 0;
                ImGui::TextUnformatted("Switch tipi");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##axis", s.switches[axis].name.c_str())) {
                    for (size_t i = 0; i < s.switches.size(); ++i) {
                        char item[64];
                        snprintf(item, sizeof(item), "%s  ·  %.1f mm", s.switches[i].name.c_str(),
                                 s.switches[i].maxTravel / 1000.f);
                        if (ImGui::Selectable(item, axis == static_cast<int>(i))) {
                            std::vector<KeyRecord> recs;
                            for (uint8_t kv : sel) {
                                s.keys[kv].axis = static_cast<uint16_t>(i);
                                recs.push_back({kv, F_AXIS, static_cast<uint16_t>(i)});
                            }
                            g_kb.WriteKeyRecords(recs);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::EndTable();
        }
    }
    EndCard();

    PageSide();
    BeginCard("global");
    CardTitle("Genel");
    if (MmSlider("Genel basma noktası", &s.globalActuation, s.minTravel, s.maxTravel, step)) {
        g_kb.WriteGlobalDb();
        for (uint8_t kv : s.AllKeys()) {
            if (s.keys[kv].touchMode == TOUCH_GLOBAL) s.keys[kv].db[0] = s.globalActuation;
        }
    }
    if (!s.SignalDead()) {
        if (MmSlider("Basma ölü bölgesi", &s.globalDeadPress, 0, 1000, 10)) g_kb.WriteGlobalDb();
        if (MmSlider("Bırakma ölü bölgesi", &s.globalDeadRelease, 0, 1000, 10)) g_kb.WriteGlobalDb();
    }
    bool sr = s.mainLight.superResponse;
    if (ToggleRow("Super Response", &sr, "Gecikmeyi mümkün olduğunca düşürür; Rapid Trigger hassasiyeti biraz azalır.")) {
        s.mainLight.superResponse = sr;
        g_kb.WriteMainLight();
    }
    if (s.TopDeadSupported() && s.topDead >= 0) {
        bool td = s.topDead == 1;
        if (ToggleRow("Üst ölü bölge", &td)) {
            s.topDead = td ? 1 : 0;
            g_kb.Send(CmdPack(TOP_DEAD_SWITCH, td ? 1 : 0));
        }
    }
    EndCard();
    ImGui::Spacing();

    BeginCard("test");
    CardTitle("Tuş derinliği testi");
    const bool testing = g_kb.GetLiveMode() == LiveMode::TravelTest;
    if (testing) {
        uint16_t maxDepth = 0;
        for (uint8_t kv : s.AllKeys()) maxDepth = std::max(maxDepth, s.keys[kv].liveTravel);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f mm", maxDepth / 1000.f);
        ImGui::ProgressBar(std::min(1.f, maxDepth / 4000.f), ImVec2(-FLT_MIN, ImGui::GetFrameHeight()), buf);
        ImGui::Spacing();
        if (WideButton("Testi durdur")) g_kb.SetLiveMode(LiveMode::Off);
    } else {
        NoSelectionHint("Tuşlara bastıkça her tuşun derinliği klavye görselinde canlı görünür.");
        ImGui::Spacing();
        if (PrimaryButton("Testi başlat", ImVec2(-FLT_MIN, 0))) g_kb.SetLiveMode(LiveMode::TravelTest);
    }
    EndCard();
    EndPage();
}

void LoadAdvEdit(uint8_t kv) {
    const KeyState& k = S().keys[kv];
    g_adv = AdvEdit();
    g_adv.key = kv;
    g_adv.type = k.advType;
    for (int i = 0; i < 4; ++i) {
        g_adv.codes[i] = k.dks[i];
        g_adv.trps[i] = k.trps[i];
    }
    if (k.advType == ADV_DKS) {
        g_adv.dksDb1 = k.db[1] ? k.db[1] : 1400;
        g_adv.dksDb2 = k.db[2] ? k.db[2] : 3000;
    } else if (k.advType == ADV_MPT) {
        for (int i = 0; i < 3; ++i) g_adv.depths[i] = k.db[1 + i] ? k.db[1 + i] : g_adv.depths[i];
    }
    g_adv.delayMs = k.mtDelayMs ? k.mtDelayMs : 200;
    if (k.advType == ADV_RS || k.advType == ADV_SOCD) g_adv.partner = static_cast<uint8_t>(k.dks[0]);
    if (k.advType == ADV_SOCD && S().SocdVersion() == 3) {
        S().socd.valid = false;
        g_kb.Send(SocdV3Pack(false, kv, 0, 0, 0, 0, 0), 10);
    }
}

// Label column + control, used inside the advanced-key editors.
void FieldLabel(const char* label, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(width);
}

void AdvancedTab() {
    DeviceState& s = S();
    std::vector<uint8_t> sel = Selected();
    if (!BeginPage("adv")) return;

    if (sel.size() != 1) {
        BeginCard("hint");
        CardTitle("Gelişmiş tuşlar");
        NoSelectionHint("Gelişmiş fonksiyon atamak için klavye görselinden tek bir tuş seçin.");
        EndCard();
        PageSide();
        EndPage();
        return;
    }
    const uint8_t kv = sel[0];
    KeyState& k = s.keys[kv];
    if (g_adv.key != kv) LoadAdvEdit(kv);
    if (s.socd.valid && s.socd.key1 == kv && g_adv.type == ADV_SOCD) {
        g_adv.partner = s.socd.key2;
        g_adv.socdMode = s.socd.mode;
        g_adv.socdType = s.socd.type;
        g_adv.codes[0] = s.socd.val1;
        g_adv.codes[1] = s.socd.val2;
        s.socd.valid = false;
    }
    const bool wide = s.WideAdvanced();
    const int step = s.precision;
    const float fs = ImGui::GetFontSize();

    struct TypeDef {
        int type;
        const char* desc;
    };
    static const TypeDef kTypes[] = {
        {ADV_NONE, "Normal tuş"},          {ADV_DKS, "4 aşamada 4 ayrı tuş"},
        {ADV_MPT, "3 derinlikte 3 tuş"},   {ADV_MT, "Dokun / basılı tut"},
        {ADV_TGL, "Tıkla: basılı kalsın"}, {ADV_END, "Bırakınca tuş gönder"},
        {ADV_RS, "Derine basan kazanır"},  {ADV_SOCD, "Karşıt tuş önceliği"},
    };
    CardTitle("Fonksiyon");
    {
        const float gap = 8;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int perRow = 4;
        const ImVec2 size((avail - gap * (perRow - 1)) / perRow, fs * 2.9f);
        for (int i = 0; i < 8; ++i) {
            if (i % perRow) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            if (OptionCard("t", kAdvNames[kTypes[i].type], kTypes[i].desc, g_adv.type == kTypes[i].type, size)) {
                g_adv.type = kTypes[i].type;
            }
            ImGui::PopID();
            if (i % perRow == perRow - 1) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
        }
    }
    ImGui::Spacing();

    auto partnerPicker = [&]() {
        FieldLabel("Eş tuş", fs * 9);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s###partner", g_adv.partner ? KeyCodeLabel(g_adv.partner).c_str() : "— seç —");
        if (ImGui::Button(buf, ImVec2(fs * 7, ImGui::GetFrameHeight() * 1.1f))) g_pickTarget = &g_adv.partner;
        ImGui::SameLine();
        ImGui::TextDisabled(g_pickTarget == &g_adv.partner ? "Klavye görselinde eş tuşa tıklayın…"
                                                           : "Butona basıp klavyeden seçin");
    };

    if (g_adv.type != ADV_NONE) {
        BeginCard("editor");
        CardTitle("Ayarlar");
        const float lw = fs * 9;
        switch (g_adv.type) {
            case ADV_DKS: {
                static const char* const kNodes[] = {"↓1", "▬", "↓2", "▬", "↑2", "▬", "↑1"};
                static const uint8_t kBits[] = {0x01, 0x02, 0x04, 0x18, 0x20, 0x40, 0x80};
                if (ImGui::BeginTable("depths", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    MmSlider("1. derinlik", &g_adv.dksDb1, s.minTravel, s.maxTravel, step);
                    ImGui::TableNextColumn();
                    MmSlider("2. derinlik", &g_adv.dksDb2, s.minTravel, s.maxTravel, step);
                    ImGui::EndTable();
                }
                if (ImGui::BeginTable("dks", 8, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Tuş", ImGuiTableColumnFlags_WidthFixed, fs * 7.5f);
                    for (const char* n : kNodes) ImGui::TableSetupColumn(n, ImGuiTableColumnFlags_WidthFixed, fs * 2.2f);
                    ImGui::TableHeadersRow();
                    for (int r = 0; r < 4; ++r) {
                        ImGui::PushID(r);
                        ImGui::TableNextColumn();
                        KeyCodeButton("code", &g_adv.codes[r]);
                        for (int n = 0; n < 7; ++n) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(n);
                            bool on = (g_adv.trps[r] & kBits[n]) == kBits[n];
                            if (ImGui::Checkbox("##n", &on)) {
                                g_adv.trps[r] = on ? (g_adv.trps[r] | kBits[n]) : (g_adv.trps[r] & ~kBits[n]);
                            }
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                NoSelectionHint("↓1/↓2: 1./2. derinliğe basış  ·  ↑2/↑1: geri bırakış  ·  ▬: arada basılı tut");
                break;
            }
            case ADV_MPT:
                for (int i = 0; i < 3; ++i) {
                    ImGui::PushID(i);
                    char label[24];
                    snprintf(label, sizeof(label), "%d. derinlik", i + 1);
                    FieldLabel(label, lw);
                    KeyCodeButton("code", &g_adv.codes[i]);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    SliderMm("##d", &g_adv.depths[i], s.minTravel, s.maxTravel, step);
                    ImGui::PopID();
                }
                break;
            case ADV_MT:
                FieldLabel("Dokununca", lw);
                KeyCodeButton("tap", &g_adv.codes[1]);
                FieldLabel("Basılı tutunca", lw);
                KeyCodeButton("hold", &g_adv.codes[0]);
                [[fallthrough]];
            case ADV_TGL: {
                if (g_adv.type == ADV_TGL) {
                    FieldLabel("Tuş", lw);
                    KeyCodeButton("tgl", &g_adv.codes[0]);
                }
                FieldLabel("Basılı tutma süresi", lw);
                int d = g_adv.delayMs;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderInt("##delay", &d, 10, 1000, "%d ms")) {
                    g_adv.delayMs = static_cast<uint16_t>(std::lround(d / 10.0) * 10);
                }
                break;
            }
            case ADV_END:
                FieldLabel("Bırakınca gönder", lw);
                KeyCodeButton("end", &g_adv.codes[0]);
                break;
            case ADV_RS:
                partnerPicker();
                break;
            case ADV_SOCD: {
                partnerPicker();
                ImGui::Spacing();
                static const char* const kModes[] = {"Son basılan", "Bu tuş öncelikli", "Eş tuş öncelikli", "Nötr"};
                ImGui::TextUnformatted("İkisi birlikte basılınca");
                Segmented("socdmode", kModes, 4, &g_adv.socdMode, ImGui::GetContentRegionAvail().x);
                if (s.SocdVersion() == 3) {
                    ImGui::Spacing();
                    static const char* const kTypesV3[] = {"Tuş konumuna göre", "Tuş koduna göre"};
                    ImGui::TextUnformatted("Bağlama");
                    Segmented("socdtype", kTypesV3, 2, &g_adv.socdType, ImGui::GetContentRegionAvail().x);
                    if (g_adv.socdType == 1) {
                        ImGui::Spacing();
                        FieldLabel("Bu tuşun kodu", lw);
                        KeyCodeButton("v1", &g_adv.codes[0]);
                        FieldLabel("Eş tuşun kodu", lw);
                        KeyCodeButton("v2", &g_adv.codes[1]);
                    }
                }
                break;
            }
            default:
                break;
        }
        EndCard();
    }

    PageSide();
    BeginCard("keycard");
    CardTitle("Tuş");
    BigText(KeyCodeLabel(kv).c_str());
    ImGui::TextDisabled("Şu anki fonksiyon: %s", kAdvNames[std::min<int>(k.advType, 9)]);
    if (k.advType == ADV_MACRO) NoSelectionHint("Bu tuşa makro atanmış; Makrolar sekmesinden yönetilir.");
    ImGui::Spacing();

    const bool needsPartner = g_adv.type == ADV_RS || g_adv.type == ADV_SOCD;
    const bool canSave = g_adv.type != ADV_NONE && (!needsPartner || (g_adv.partner && g_adv.partner != kv));
    ImGui::BeginDisabled(!canSave || !Ready());
    if (PrimaryButton("Kaydet", ImVec2(-FLT_MIN, 0))) {
        const uint8_t p = g_adv.partner;
        switch (g_adv.type) {
            case ADV_DKS: g_kb.Send(DksPack(wide, kv, g_adv.codes, g_adv.trps, g_adv.dksDb1, g_adv.dksDb2)); break;
            case ADV_MPT: g_kb.Send(MptPack(wide, kv, g_adv.codes, g_adv.depths)); break;
            case ADV_MT: g_kb.Send(MtPack(wide, kv, g_adv.codes[0], g_adv.codes[1], g_adv.delayMs)); break;
            case ADV_TGL: g_kb.Send(TglPack(wide, kv, g_adv.codes[0], g_adv.delayMs)); break;
            case ADV_END: g_kb.Send(EndPack(wide, kv, g_adv.codes[0])); break;
            case ADV_RS: g_kb.Send(RsPack(kv, p)); break;
            case ADV_SOCD:
                if (s.SocdVersion() == 3) {
                    g_kb.Send(SocdV3Pack(true, kv, p, g_adv.codes[0], g_adv.codes[1],
                                         static_cast<uint8_t>(g_adv.socdType), static_cast<uint8_t>(g_adv.socdMode)));
                } else if (s.SocdVersion() == 2) {
                    g_kb.Send(SocdV2Pack(kv, p, static_cast<uint8_t>(g_adv.socdMode)));
                } else {
                    g_kb.Send(SocdV1Pack(kv, p));
                }
                break;
        }
        g_kb.Send(CmdPack(PROTOCOL_VERSION), 20);  // small gap before reading the result back
        g_kb.ReadAdvanced(kv);
        if (needsPartner) g_kb.ReadAdvanced(p);
        g_adv.key = 0;  // reload the editor from the read-back
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(k.advType == ADV_NONE || !Ready());
    if (WideButton("Fonksiyonu kaldır")) {
        std::vector<KeyRecord> recs = {{kv, F_MODE, static_cast<uint16_t>(k.touchMode << 4)}};
        uint8_t partner = static_cast<uint8_t>(k.dks[0]);
        if ((k.advType == ADV_RS || k.advType == ADV_SOCD) && partner && s.keys[partner].present &&
            s.keys[partner].advType == k.advType) {
            recs.push_back({partner, F_MODE, static_cast<uint16_t>(s.keys[partner].touchMode << 4)});
            s.keys[partner].advType = ADV_NONE;
        }
        k.advType = ADV_NONE;
        g_kb.WriteKeyRecords(recs);
        g_adv.key = 0;
    }
    ImGui::EndDisabled();
    EndCard();
    EndPage();
}

void SendMacroBinding(uint8_t kv, int macroIdx) {
    DeviceState& s = S();
    const Macro& m = g_macros[macroIdx];
    const bool v2 = s.MacroV2();
    const size_t chunk = v2 ? 9 : 14;
    const size_t steps = std::min<size_t>(m.events.size(), 64);
    for (size_t i = 0; i < steps; i += chunk) {
        g_kb.Send(MacroPack(v2, static_cast<uint16_t>(i), m.events.data() + i, std::min(chunk, steps - i)));
    }
    KeyState& k = s.keys[kv];
    k.advType = ADV_MACRO;
    uint16_t mode = static_cast<uint16_t>((k.touchMode << 4) | ADV_MACRO);
    if (v2) {
        g_kb.Send(KeyPack(true, std::vector<KeyRecord>{{kv, F_MODE, mode}}.data(), 1), 5);
        g_kb.Send(MacroModePack(true, kv, static_cast<uint16_t>(macroIdx), static_cast<uint8_t>(steps),
                                static_cast<uint8_t>(g_bindMode), static_cast<uint16_t>(g_bindRepeat),
                                static_cast<uint32_t>(g_bindDelay)));
    } else {
        std::vector<KeyRecord> recs = {{kv, F_MODE, mode},
                                       {kv, F_MACRO_ADDR, 256},
                                       {kv, F_MACRO_SIZE, static_cast<uint16_t>(steps)}};
        g_kb.WriteKeyRecords(recs);
    }
}

void MacrosTab() {
    DeviceState& s = S();
    const float fs = ImGui::GetFontSize();
    if (!ImGui::BeginTable("macros", 3, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, fs * 13);
    ImGui::TableSetupColumn("edit", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("bind", ImGuiTableColumnFlags_WidthFixed, fs * kSideWidth);

    // Library
    ImGui::TableNextColumn();
    BeginCard("library");
    CardTitle("Makro kütüphanesi");
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(g_recording);
    if (PrimaryButton("+ Yeni", ImVec2(half, 0))) {
        char name[32];
        snprintf(name, sizeof(name), "Makro %zu", g_macros.size() + 1);
        g_macros.push_back({name, {}});
        g_macroSel = static_cast<int>(g_macros.size()) - 1;
        SaveMacros();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_macroSel < 0);
    if (ImGui::Button("Sil", ImVec2(half, ImGui::GetFrameHeight() * 1.2f))) {
        g_macros.erase(g_macros.begin() + g_macroSel);
        g_macroSel = std::min<int>(g_macroSel, static_cast<int>(g_macros.size()) - 1);
        SaveMacros();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (g_macros.empty()) NoSelectionHint("Henüz makro yok.");
    for (int i = 0; i < static_cast<int>(g_macros.size()); ++i) {
        ImGui::PushID(i);
        char sub[32];
        snprintf(sub, sizeof(sub), "%zu adım", g_macros[i].events.size());
        if (OptionCard("m", g_macros[i].name.c_str(), sub, g_macroSel == i,
                       ImVec2(ImGui::GetContentRegionAvail().x, fs * 2.6f)) &&
            !g_recording) {
            g_macroSel = i;
        }
        ImGui::PopID();
    }
    EndCard();

    // Editor
    ImGui::TableNextColumn();
    const bool haveMacro = g_macroSel >= 0 && g_macroSel < static_cast<int>(g_macros.size());
    BeginCard("editor");
    CardTitle("Düzenle");
    if (!haveMacro) {
        NoSelectionHint("Soldan bir makro seçin veya yeni bir tane oluşturun.");
    } else {
        Macro& m = g_macros[g_macroSel];
        char name[64];
        snprintf(name, sizeof(name), "%s", m.name.c_str());
        const float recW = fs * 10;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - recW - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputTextWithHint("##name", "Makro adı", name, sizeof(name))) m.name = name;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveMacros();
        ImGui::SameLine();
        if (!g_recording) {
            if (PrimaryButton("● Klavyeden kaydet", ImVec2(recW, ImGui::GetFrameHeight()))) {
                m.events.clear();
                g_recording = true;
                g_lastRecordAt = 0;
            }
        } else if (DangerButton("■ Kaydı durdur", ImVec2(recW, ImGui::GetFrameHeight()))) {
            g_recording = false;
            SaveMacros();
        }
        if (g_recording) {
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Kayıt ediliyor… tuşlara basın (%zu / 64 adım)",
                               m.events.size());
        }
        ImGui::Spacing();

        if (ImGui::BeginTable("steps", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, fs * 14))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 1.6f);
            ImGui::TableSetupColumn("Tuş", ImGuiTableColumnFlags_WidthFixed, fs * 7.5f);
            ImGui::TableSetupColumn("Olay", ImGuiTableColumnFlags_WidthFixed, fs * 8);
            ImGui::TableSetupColumn("Önceki adımdan sonra", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, fs * 2);
            ImGui::TableHeadersRow();
            int removeAt = -1;
            for (int i = 0; i < static_cast<int>(m.events.size()); ++i) {
                MacroEvent& e = m.events[i];
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%d", i + 1);
                ImGui::TableNextColumn();
                if (KeyCodeButton("code", &e.code)) SaveMacros();
                ImGui::TableNextColumn();
                static const char* const kDir[] = {"Bas", "Bırak"};
                int dir = e.down ? 0 : 1;
                if (Segmented("dir", kDir, 2, &dir, fs * 7.5f)) {
                    e.down = dir == 0;
                    SaveMacros();
                }
                ImGui::TableNextColumn();
                int d = static_cast<int>(e.delayMs);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##ms", &d, 10, 100)) e.delayMs = static_cast<uint32_t>(std::clamp(d, 0, 0xFFFFFF));
                if (ImGui::IsItemDeactivatedAfterEdit()) SaveMacros();
                ImGui::TableNextColumn();
                if (ImGui::Button("×", ImVec2(fs * 1.6f, 0))) removeAt = i;
                ImGui::PopID();
            }
            if (removeAt >= 0) {
                m.events.erase(m.events.begin() + removeAt);
                SaveMacros();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(m.events.size() >= 64 || g_recording);
        if (ImGui::Button("+ Adım ekle", ImVec2(fs * 8, ImGui::GetFrameHeight() * 1.1f))) {
            m.events.push_back({0x04, true, m.events.empty() ? 0u : 20u});
            SaveMacros();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Süreler milisaniye. Her basışa bir bırakma ekleyin; en fazla 64 adım.");
    }
    EndCard();

    // Binding
    ImGui::TableNextColumn();
    BeginCard("bind");
    CardTitle("Tuşa ata");
    std::vector<uint8_t> sel = Selected();
    int bound = 0;
    for (uint8_t kv : s.AllKeys()) bound += s.keys[kv].advType == ADV_MACRO ? 1 : 0;
    const bool targetOk = sel.size() == 1;
    const bool already = targetOk && s.keys[sel[0]].advType == ADV_MACRO;
    if (targetOk) {
        BigText(KeyCodeLabel(sel[0]).c_str());
        if (already) ImGui::TextDisabled("Bu tuşta makro var");
    } else {
        NoSelectionHint("Klavye görselinden tek bir tuş seçin.");
    }
    ImGui::Spacing();
    if (s.MacroV2()) {
        static const char* const kModes[] = {"Bir kez çalıştır", "Tıkla: tekrarla / durdur", "Basılıyken tekrarla",
                                             "Basılıyken tekrarla, turu bitir"};
        ImGui::TextUnformatted("Çalışma şekli");
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            if (OptionCard("mode", kModes[i], nullptr, g_bindMode == i,
                           ImVec2(ImGui::GetContentRegionAvail().x, fs * 1.9f))) {
                g_bindMode = i;
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(g_bindMode == 0);
        ImGui::TextUnformatted("Tekrar sayısı");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##rep", &g_bindRepeat);
        g_bindRepeat = std::clamp(g_bindRepeat, 1, 65530);
        ImGui::TextUnformatted("Tekrar aralığı (ms)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##gap", &g_bindDelay);
        g_bindDelay = std::clamp(g_bindDelay, 1, 0xFFFFFF);
        ImGui::EndDisabled();
        ImGui::Spacing();
    }
    ImGui::BeginDisabled(!targetOk || !haveMacro || (haveMacro && g_macros[g_macroSel].events.empty()) ||
                         g_recording || !Ready() || (bound >= 8 && !already));
    if (PrimaryButton("Makroyu tuşa yükle", ImVec2(-FLT_MIN, 0))) SendMacroBinding(sel[0], g_macroSel);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!already || !Ready());
    if (WideButton("Tuştan makroyu kaldır")) {
        KeyState& k = s.keys[sel[0]];
        k.advType = ADV_NONE;
        g_kb.WriteKeyRecords({{sel[0], F_MODE, static_cast<uint16_t>(k.touchMode << 4)}});
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Makro tuşları: %d / 8", bound);
    EndCard();
    ImGui::EndTable();
}

// --- Lighting ---------------------------------------------------------------------------------------------------

const char* const kEffectNames[] = {
    "Dalgalanma",    "Gelgit",       "Hafif dalga",      "Dönen fırtına", "Şanslı gökkuşağı",
    "Parlayan gökkuşağı", "Pırıltı", "Kayan pencere",    "Dalga formu",   "Kayan gölgeler",
    "Sinüs",         "Akan bulutlar", "Çiçekler",        "Renk mozaiği",  "Yağmur",
    "Durmayan hareket", "Kar izi",   "Kar ve erik",      "Lazer",         "Su dalgası"};

// Effect picker card grid; returns true when a new effect was chosen.
bool EffectGrid(uint8_t* mode, int count, bool logo) {
    bool changed = false;
    const float fs = ImGui::GetFontSize();
    const float gap = 8;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((avail + gap) / (fs * 8.5f + gap)));
    const ImVec2 size((avail - gap * (perRow - 1)) / perRow, fs * 3.0f);
    for (int i = 0; i < count; ++i) {
        if (i % perRow) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        char title[64];
        if (logo) {
            snprintf(title, sizeof(title), "Stil %d", i + 1);
        } else {
            snprintf(title, sizeof(title), "M%d  %s", i + 1, kEffectNames[i]);
        }
        if (OptionCard("fx", title, nullptr, *mode == i + 1, size, std::fmod(i * 0.137f, 1.f)) && *mode != i + 1) {
            *mode = static_cast<uint8_t>(i + 1);
            changed = true;
        }
        if (!logo && ImGui::IsItemHovered() && ImGui::CalcTextSize(title).x > size.x - 20) ImGui::SetTooltip("%s", title);
        ImGui::PopID();
        if (i % perRow == perRow - 1) ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    }
    return changed;
}

void LightControls(const char* id, LightState& L, bool logo) {
    ImGui::PushID(id);
    bool changed = false;
    const float fs = ImGui::GetFontSize();

    int big = !L.on ? 0 : L.mode == 0 ? 1 : (logo || L.mode <= 20) ? 2 : 3;
    const char* const kBig[] = {"Kapalı", "Statik renk", "Efektler", "Tuş bazlı"};

    if (BeginPage("light")) {
        if (Segmented("mode", kBig, logo ? 3 : 4, &big, ImGui::GetContentRegionAvail().x)) {
            L.on = big != 0;
            if (big == 1) L.mode = 0;
            if (big == 2 && (L.mode == 0 || L.mode > (logo ? 4 : 20))) L.mode = 1;
            if (big == 3) L.mode = 21;
            changed = true;
        }
        ImGui::Spacing();
        ImGui::Spacing();

        if (big == 0) {
            NoSelectionHint("Işık kapalı. Açmak için yukarıdan bir mod seçin.");
        } else if (big == 1) {
            CardTitle("Renk seçin  ·  düzenlemek için çift tıklayın veya sağ tıklayın");
            const float tile = fs * 3.0f;
            static int editSlot = -1;
            for (int i = 0; i < 8; ++i) {
                if (i) ImGui::SameLine(0, fs * 0.7f);
                ImGui::PushID(i);
                int r = ColorTile("t", i < 7 ? L.colors[i] : 0, L.staticSlot == i, tile, i == 7);
                if (r >= 1 && L.staticSlot != i) {
                    L.staticSlot = static_cast<uint8_t>(i);
                    changed = true;
                }
                if (r == 2 && i < 7) {
                    editSlot = i;
                    ImGui::OpenPopup("edit");
                }
                if (editSlot == i && ColorEditPopup("edit", &L.colors[i])) changed = true;
                ImGui::PopID();
            }
            ImGui::Spacing();
            if (L.staticSlot == 7) {
                ImGui::TextDisabled("Gökkuşağı: klavyenin kendi renk geçişi.");
            } else {
                char hex[16];
                snprintf(hex, sizeof(hex), "#%06X", L.colors[L.staticSlot]);
                ImGui::TextDisabled("Renk %d  ·  %s", L.staticSlot + 1, hex);
                ImGui::SameLine();
                if (ImGui::SmallButton("Düzenle")) {
                    editSlot = L.staticSlot;
                    ImGui::OpenPopup("edit2");
                }
                if (editSlot == L.staticSlot && ColorEditPopup("edit2", &L.colors[L.staticSlot])) changed = true;
            }
        } else if (big == 2) {
            CardTitle(logo ? "Stil seçin" : "Efekt seçin");
            changed |= EffectGrid(&L.mode, logo ? 4 : 20, logo);
        } else if (big == 3) {
            DeviceState& s = S();
            static uint32_t paint = 0xFF0000;
            CardTitle("Fırça rengi");
            static const uint32_t kPalette[] = {0xFF0000, 0xFF6A00, 0xFFD000, 0x7CFF00, 0x00FF00, 0x00FFC8,
                                                0x00A0FF, 0x0000FF, 0x8000FF, 0xFF00C8, 0xFFFFFF, 0x000000};
            const float tile = fs * 2.0f;
            for (int i = 0; i < 12; ++i) {
                if (i) ImGui::SameLine(0, fs * 0.45f);
                ImGui::PushID(i);
                if (ColorTile("p", kPalette[i], paint == kPalette[i], tile) == 1) paint = kPalette[i];
                ImGui::PopID();
            }
            ImGui::SameLine(0, fs * 0.9f);
            bool custom = std::find(std::begin(kPalette), std::end(kPalette), paint) == std::end(kPalette);
            if (ColorTile("custom", paint, custom, tile) >= 1) ImGui::OpenPopup("brush");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Özel renk");
            ColorEditPopup("brush", &paint);

            ImGui::Spacing();
            std::vector<uint8_t> sel = Selected();
            ImGui::BeginDisabled(sel.empty() || !Ready());
            char apply[48];
            snprintf(apply, sizeof(apply), "Seçili %zu tuşa uygula", sel.size());
            if (PrimaryButton(apply, ImVec2(fs * 11, 0))) {
                for (uint8_t kv : sel) s.keys[kv].color = paint;
                g_kb.WriteKeyColors(sel);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!Ready());
            if (ImGui::Button("Tüm klavyeyi boya", ImVec2(fs * 9, ImGui::GetFrameHeight() * 1.2f))) {
                std::vector<uint8_t> all = s.AllKeys();
                for (uint8_t kv : all) s.keys[kv].color = paint;
                g_kb.WriteKeyColors(all);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(sel.size() != 1);
            if (ImGui::Button("Tuştan renk al", ImVec2(fs * 7.5f, ImGui::GetFrameHeight() * 1.2f))) {
                paint = s.keys[sel[0]].color;
            }
            ImGui::EndDisabled();
            NoSelectionHint("Klavye görselinde tuşları seçin (Ctrl ile çoklu seçim), sonra rengi uygulayın.");
        }

        PageSide();
        BeginCard("settings");
        CardTitle("Ayarlar");
        ImGui::BeginDisabled(big == 0);
        const float w = ImGui::GetContentRegionAvail().x;
        static const char* const kLevels[] = {"1", "2", "3", "4", "5"};

        ImGui::TextUnformatted("Parlaklık");
        int br = L.brightness;
        if (Segmented("br", kLevels, 5, &br, w)) {
            L.brightness = static_cast<uint8_t>(br);
            changed = true;
        }
        ImGui::Spacing();

        ImGui::BeginDisabled(big != 2);
        ImGui::TextUnformatted("Hız");
        int sp = L.speed;
        if (Segmented("sp", kLevels, 5, &sp, w)) {
            L.speed = static_cast<uint8_t>(sp);
            changed = true;
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Yön");
        static const char* const kDir[] = {"Normal", "Ters"};
        int dir = L.reverse ? 1 : 0;
        if (Segmented("dir", kDir, 2, &dir, w)) {
            L.reverse = dir == 1;
            changed = true;
        }
        ImGui::EndDisabled();

        if (!logo) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Uykuya geç");
            static const int kSleep[] = {1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 120, 0};
            char cur[24];
            snprintf(cur, sizeof(cur), L.sleepMinutes ? "%d dakika sonra" : "Hiçbir zaman", L.sleepMinutes);
            ImGui::SetNextItemWidth(w);
            if (ImGui::BeginCombo("##sleep", cur)) {
                for (int m : kSleep) {
                    char item[24];
                    snprintf(item, sizeof(item), m ? "%d dakika sonra" : "Hiçbir zaman", m);
                    if (ImGui::Selectable(item, L.sleepMinutes == m)) {
                        L.sleepMinutes = static_cast<uint8_t>(m);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::EndDisabled();
        EndCard();
        EndPage();
    }

    if (changed && Ready()) {
        if (logo) {
            g_kb.WriteLogoLight();
        } else {
            g_kb.WriteMainLight();
            if (L.on && L.mode == 21) g_kb.ReadKeyColors();
        }
    }
    ImGui::PopID();
}

void LightingTab() {
    DeviceState& s = S();
    if (!s.hasLogoLight) {
        LightControls("main", s.mainLight, false);
        return;
    }
    static int which = 0;
    static const char* const kWhich[] = {"Ana ışık", "Ortam ışığı"};
    Segmented("which", kWhich, 2, &which, ImGui::GetFontSize() * 16);
    ImGui::Spacing();
    if (which == 0) {
        LightControls("main", s.mainLight, false);
    } else {
        LightControls("logo", s.logoLight, true);
    }
}

// --- Device -----------------------------------------------------------------------------------------------------

void DeviceTab() {
    DeviceState& s = S();
    const float fs = ImGui::GetFontSize();
    if (!BeginPage("dev")) return;

    if (ImGui::BeginTable("devcols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        BeginCard("info");
        CardTitle("Cihaz bilgisi");
        auto row = [&](const char* k, const std::string& v) {
            ImGui::TextDisabled("%s", k);
            ImGui::SameLine(fs * 7);
            ImGui::TextUnformatted(v.empty() ? "—" : v.c_str());
        };
        char buf[64];
        row("Ad", s.name);
        row("Firmware", s.firmware);
        snprintf(buf, sizeof(buf), "%d.%d.%d", s.protocol / 10000, s.protocol / 100 % 100, s.protocol % 100);
        row("Protokol", s.protocol ? buf : "");
        row("Seri no", s.serial);
        snprintf(buf, sizeof(buf), "%04X:%04X", s.vid, s.pid);
        row("USB", buf);
        snprintf(buf, sizeof(buf), "%.3f mm adım  ·  en fazla %.2f mm", s.precision / 1000.f, s.maxTravel / 1000.f);
        row("Hassasiyet", buf);
        EndCard();

        ImGui::TableNextColumn();
        BeginCard("settings");
        CardTitle("Polling rate");
        const bool slowRates = s.keyType == 4;
        static const char* const kFast[] = {"8K", "4K", "2K", "1K", "500", "250", "125"};
        static const char* const kSlow[] = {"1000", "500", "250", "125"};
        int poll = s.pollingIndex;
        if (Segmented("poll", slowRates ? kSlow : kFast, slowRates ? 4 : 7, &poll, ImGui::GetContentRegionAvail().x)) {
            s.pollingIndex = poll;
            g_kb.Send(CmdPack(POLLING_RATE, static_cast<uint8_t>(poll)));
        }
        NoSelectionHint("Hz. Değiştirince klavye yeniden başlar; program otomatik yeniden bağlanır.");
        if (s.sysMode >= 0) {
            ImGui::Spacing();
            CardTitle("İşletim sistemi");
            static const char* const kSys[] = {"Windows", "macOS"};
            int sys = s.sysMode;
            if (Segmented("sys", kSys, 2, &sys, ImGui::GetContentRegionAvail().x)) {
                g_kb.Send(CmdPack(sys == 0 ? SET_SYS_WIN : SET_SYS_MAC), 100);
                g_kb.ReloadAll();
            }
        }
        EndCard();
        ImGui::EndTable();
    }

    ImGui::Spacing();
    BeginCard("reset");
    CardTitle("Fabrika ayarları");
    if (!g_confirmReset) {
        NoSelectionHint("Tuş atamaları, performans, gelişmiş tuşlar ve ışık ayarları klavyenin varsayılanlarına döner.");
        ImGui::Spacing();
        if (ImGui::Button("Fabrika ayarlarına dön…", ImVec2(fs * 13, ImGui::GetFrameHeight() * 1.2f))) {
            g_confirmReset = true;
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.55f, 0.35f, 1), "Emin misiniz? Bu işlem geri alınamaz.");
        ImGui::Spacing();
        if (DangerButton("Evet, sıfırla", ImVec2(fs * 9, 0))) {
            g_kb.Send(CmdPack(FACTORY_RESET), 1500);
            g_kb.ReloadAll();
            g_confirmReset = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Vazgeç", ImVec2(fs * 7, ImGui::GetFrameHeight() * 1.2f))) g_confirmReset = false;
    }
    EndCard();

    PageSide();
    BeginCard("calib");
    CardTitle("Kalibrasyon");
    const bool cal = g_kb.GetLiveMode() == LiveMode::Calibration;
    if (!cal) {
        NoSelectionHint("Tuşlar yanlış algılanıyorsa kalibrasyon yapın: başlattıktan sonra her tuşa sonuna kadar "
                        "basıp bırakın. Tamamlanan tuşlar klavye görselinde yeşile döner.");
        ImGui::Spacing();
        ImGui::BeginDisabled(!Ready());
        if (PrimaryButton("Kalibrasyonu başlat", ImVec2(-FLT_MIN, 0))) g_kb.SetLiveMode(LiveMode::Calibration);
        ImGui::EndDisabled();
    } else {
        int done = 0, total = 0;
        for (uint8_t kv : s.AllKeys()) {
            ++total;
            if (s.keys[kv].liveTravel + 10 >= s.KeyMaxTravel(s.keys[kv])) ++done;
        }
        char buf2[32];
        snprintf(buf2, sizeof(buf2), "%d / %d tuş", done, total);
        ImGui::ProgressBar(total ? done / static_cast<float>(total) : 0.f, ImVec2(-FLT_MIN, ImGui::GetFrameHeight()),
                           buf2);
        ImGui::Spacing();
        if (PrimaryButton("Kaydet ve bitir", ImVec2(-FLT_MIN, 0))) g_kb.SetLiveMode(LiveMode::Off);
    }
    EndCard();
    EndPage();
}

// ---------------------------------------------------------------------------------------------------------------

void TopBar() {
    DeviceState& s = S();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(s.name.empty() ? "Slice75 HE" : s.name.c_str());
    ImGui::SameLine();
    ImVec4 col = s.conn == ConnState::Ready ? ImVec4(0.4f, 0.85f, 0.5f, 1)
                 : s.conn == ConnState::Error || s.conn == ConnState::NoDevice ? ImVec4(0.95f, 0.45f, 0.4f, 1)
                                                                              : ImVec4(0.95f, 0.75f, 0.3f, 1);
    ImGui::TextColored(col, "●");
    ImGui::SameLine();
    ImGui::TextUnformatted(g_kb.StatusText().c_str());
    if (s.conn == ConnState::Loading && s.keysTotalPackets > 0) {
        ImGui::SameLine();
        ImGui::ProgressBar(s.keysLoadedPackets / static_cast<float>(s.keysTotalPackets),
                           ImVec2(ImGui::GetFontSize() * 8, 0));
    }

    float right = ImGui::GetContentRegionMax().x;
    float w = ImGui::GetFontSize() * (s.configId >= 0 ? 19 : 7);
    ImGui::SameLine(right - w);
    if (s.configId >= 0) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        char cur[16];
        snprintf(cur, sizeof(cur), "Profil %d", s.configId + 1);
        if (ImGui::BeginCombo("##profile", cur)) {
            for (int i = 0; i < 4; ++i) {
                char item[16];
                snprintf(item, sizeof(item), "Profil %d", i + 1);
                if (ImGui::Selectable(item, s.configId == i) && s.configId != i) {
                    s.configId = i;
                    g_kb.Send(CmdPack(CONFIG_ID, static_cast<uint8_t>(i)), 300);
                    g_kb.ReloadAll();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (s.conn == ConnState::NoDevice || s.conn == ConnState::Error) {
        if (ImGui::Button("Yeniden ara", ImVec2(ImGui::GetFontSize() * 7, 0))) g_kb.Rescan();
    } else {
        ImGui::BeginDisabled(s.conn != ConnState::Ready);
        if (ImGui::Button("Yeniden oku", ImVec2(ImGui::GetFontSize() * 7, 0))) g_kb.ReloadAll();
        ImGui::EndDisabled();
    }
}

}  // namespace

// -------------------------------------------------------------------------------------------------------------------
// Host interface

void AppInit(HWND hwnd) {
    g_hwnd = hwnd;
    LoadMacros();
    g_kb.Attach(hwnd, WM_APP_HID);
    g_kb.Rescan();
}

void AppShutdown() {
    g_kb.SetLiveMode(LiveMode::Off);
    g_kb.Disconnect();
}

void AppApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0;
    st.FrameRounding = 5;
    st.GrabRounding = 5;
    st.PopupRounding = 6;
    st.TabRounding = 5;
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 7);
    ImVec4* c = st.Colors;
    const ImVec4 accent(0.95f, 0.55f, 0.20f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.085f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.115f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.125f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.19f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.27f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.32f, 1);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.23f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.34f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.62f, 0.36f, 0.14f, 1);
    c[ImGuiCol_Header] = ImVec4(0.62f, 0.36f, 0.14f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.62f, 0.36f, 0.14f, 0.75f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.62f, 0.36f, 0.14f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.17f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(0.62f, 0.36f, 0.14f, 0.8f);
    c[ImGuiCol_TabSelected] = ImVec4(0.50f, 0.29f, 0.11f, 1);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.68f, 0.35f, 1);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.95f, 0.65f, 0.35f, 1);
    c[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.32f, 1);
}

void AppDraw() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    TopBar();
    ImGui::Separator();

    DeviceState& s = S();
    if (s.conn == ConnState::NoDevice || s.conn == ConnState::Error || s.conn == ConnState::Bootloader) {
        ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 4));
        ImGui::TextWrapped("Slice75 HE klavyeyi USB kablosuyla bağlayın. Program klavyeyi otomatik olarak bulur.\n\n"
                           "Tarayıcıda Chilkey sürücü sayfası açıksa kapatın; aynı anda yalnızca bir program "
                           "klavyeyle konuşmalıdır.");
        ImGui::End();
        return;
    }

    ImGui::BeginChild("kbd", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    DrawKeyboard();
    SelectionToolbar();
    ImGui::EndChild();

    if (ImGui::BeginTabBar("tabs")) {
        struct TabDef {
            const char* name;
            Tab tab;
            void (*fn)();
        };
        static const TabDef kTabs[] = {
            {"Tuş Atama", Tab::Keymap, KeymapTab},       {"Performans", Tab::Performance, PerformanceTab},
            {"Gelişmiş Tuşlar", Tab::Advanced, AdvancedTab}, {"Makrolar", Tab::Macros, MacrosTab},
            {"Işık", Tab::Lighting, LightingTab},         {"Cihaz", Tab::Device, DeviceTab},
        };
        for (const TabDef& t : kTabs) {
            if (ImGui::BeginTabItem(t.name)) {
                if (g_tab != t.tab) {
                    // Leaving a tab stops its live stream (calibration is saved on stop).
                    if (g_tab == Tab::Performance && g_kb.GetLiveMode() == LiveMode::TravelTest) {
                        g_kb.SetLiveMode(LiveMode::Off);
                    }
                    if (g_tab == Tab::Device && g_kb.GetLiveMode() == LiveMode::Calibration) {
                        g_kb.SetLiveMode(LiveMode::Off);
                    }
                    g_pickTarget = nullptr;
                    g_tab = t.tab;
                }
                ImGui::BeginChild("content", ImVec2(0, 0), ImGuiChildFlags_None);
                ImGui::BeginDisabled(s.conn != ConnState::Ready);
                t.fn();
                ImGui::EndDisabled();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void AppOnDeviceChange() {
    g_kb.Rescan();
    AppRequestRedraw();
}

void AppOnHidNotify() { g_kb.OnHidNotify(); }

bool AppOnTimer(UINT_PTR id) {
    if (id != kAppTickTimer) return false;
    ConnState before = S().conn;
    g_kb.Tick();
    if (S().conn != before) AppRequestRedraw();
    return true;
}

bool AppNeedsAnimation() { return false; }

bool AppOnKey(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_recording || g_macroSel < 0 || g_macroSel >= static_cast<int>(g_macros.size())) return false;
    const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    if (down && (lParam & (1 << 30))) return true;  // auto-repeat
    UINT scan = (lParam >> 16) & 0xFF;
    bool extended = (lParam & (1 << 24)) != 0;
    UINT vk = static_cast<UINT>(wParam);
    if (vk == VK_SHIFT) vk = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
    uint16_t code = VirtualKeyToHid(vk, extended);
    if (code == 0) return true;

    Macro& m = g_macros[g_macroSel];
    if (m.events.size() >= 64) return true;
    DWORD now = GetTickCount();
    uint32_t delay = g_lastRecordAt ? now - g_lastRecordAt : 0;
    g_lastRecordAt = now;
    m.events.push_back({code, down, delay});
    AppRequestRedraw();
    return true;
}
