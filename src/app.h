// Interface between the Win32/D3D host (main.cpp) and the configurator UI (app.cpp).
#pragma once

#include <windows.h>

constexpr UINT WM_APP_HID = WM_APP + 1;  // posted by the HID reader thread
constexpr UINT_PTR kAppTickTimer = 2;    // 200 ms housekeeping tick, forwarded to AppOnTimer

void AppInit(HWND hwnd);
void AppShutdown();
void AppApplyStyle();
void AppDraw();
void AppOnDeviceChange();
void AppOnHidNotify();
bool AppOnTimer(UINT_PTR id);
bool AppNeedsAnimation();
// Raw keyboard messages (WM_KEYDOWN/UP, WM_SYSKEYDOWN/UP); returns true to swallow them (macro recording).
bool AppOnKey(UINT msg, WPARAM wParam, LPARAM lParam);

// Implemented by the host: keeps frames coming for a short while.
void AppRequestRedraw();
