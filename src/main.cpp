#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wtsapi32.h>

#include "resource.h"
#include "overlay.h"
#include "osd.h"
#include "autostart.h"

namespace {

const wchar_t kWindowClass[] = L"PowerModeTray.Main";
const wchar_t kMutexName[] = L"Local\\PowerModeTray.SingleInstance";
const wchar_t kThemeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
const wchar_t kThemeValue[] = L"SystemUsesLightTheme";

// Fixed so the icon keeps its position across restarts.
const GUID kTrayIconGuid = {
    0x7c1f4b2e, 0x9a63, 0x4d51, {0xb2, 0x0e, 0x5f, 0x8c, 0x31, 0xa7, 0x64, 0xd9}};

// Declared in the SDK as an extern GUID that would require linking; the value is
// stable and copied here to keep the link surface at zero extra libraries.
// {5d3e9a59-e9d5-4b00-a6bd-ff34ff516548}
const GUID kGuidAcDcPowerSource = {
    0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};

const UINT WM_TRAYICON = WM_APP + 1;
const UINT_PTR kTimerResync = 1;
const UINT kResyncMs = 60 * 1000;  // upper bound on drift, not a poll loop

HINSTANCE g_instance = nullptr;
HWND g_wnd = nullptr;
HMENU g_menu = nullptr;
HICON g_icon = nullptr;
HANDLE g_singleInstance = nullptr;
HPOWERNOTIFY g_powerNotify = nullptr;
HKEY g_themeKey = nullptr;
HANDLE g_themeEvent = nullptr;
UINT g_taskbarCreated = 0;
bool g_iconAdded = false;
bool g_useGuidIcon = true;
PowerMode g_mode = PowerMode::Unknown;

const wchar_t* Str(UINT id, wchar_t* buf, int cch) {
    if (LoadStringW(g_instance, id, buf, cch) == 0) buf[0] = L'\0';
    return buf;
}

UINT StringIdForMode(PowerMode mode) {
    switch (mode) {
        case PowerMode::Efficiency:  return IDS_MODE_EFF;
        case PowerMode::Balanced:    return IDS_MODE_BALANCED;
        case PowerMode::Performance: return IDS_MODE_PERF;
        default:                     return IDS_MODE_UNKNOWN;
    }
}

// SystemUsesLightTheme == 1 means a light taskbar, which needs the dark glyph.
bool SystemUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kThemeKey, kThemeValue, RRF_RT_REG_DWORD, &type,
                     &value, &size) != ERROR_SUCCESS) {
        return false;  // default to the dark taskbar Windows ships with
    }
    return value != 0;
}

UINT IconIdFor(PowerMode mode, bool lightTaskbar) {
    switch (mode) {
        case PowerMode::Efficiency:
            return lightTaskbar ? IDI_EFF_DARK : IDI_EFF_LIGHT;
        case PowerMode::Performance:
            return lightTaskbar ? IDI_PERF_DARK : IDI_PERF_LIGHT;
        default:
            return lightTaskbar ? IDI_BALANCED_DARK : IDI_BALANCED_LIGHT;
    }
}

void FillIconData(NOTIFYICONDATAW* nid) {
    ZeroMemory(nid, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = g_wnd;
    if (g_useGuidIcon) {
        nid->uFlags = NIF_GUID;
        nid->guidItem = kTrayIconGuid;
    } else {
        nid->uID = 1;
    }
}

bool LoadTrayIcon() {
    const UINT dpi = GetDpiForWindow(g_wnd);
    const int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : 96);
    const int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi ? dpi : 96);

    HICON icon = nullptr;
    const HRESULT hr = LoadIconWithScaleDown(
        g_instance, MAKEINTRESOURCEW(IconIdFor(g_mode, SystemUsesLightTheme())), cx, cy,
        &icon);
    if (FAILED(hr) || !icon) return false;

    HICON previous = g_icon;
    g_icon = icon;
    if (previous) DestroyIcon(previous);
    return true;
}

// Tooltip and glyph always describe the actual (user-selected) overlay.
bool UpdateTrayIcon() {
    if (!g_iconAdded) return false;
    if (!LoadTrayIcon()) return false;

    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    nid.uFlags |= NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = g_icon;
    Str(OverlayAvailable() ? StringIdForMode(g_mode) : IDS_UNAVAILABLE, nid.szTip,
        ARRAYSIZE(nid.szTip));
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

bool AddTrayIcon() {
    if (!LoadTrayIcon()) return false;

    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    nid.uFlags |= NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_icon;
    Str(OverlayAvailable() ? StringIdForMode(g_mode) : IDS_UNAVAILABLE, nid.szTip,
        ARRAYSIZE(nid.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        // A NIF_GUID registration is bound to the exe path: after the binary is
        // moved, the shell refuses the add. Fall back to an id-keyed icon, which
        // loses position persistence but keeps the program usable.
        if (!g_useGuidIcon) return false;
        g_useGuidIcon = false;
        FillIconData(&nid);
        nid.uFlags |= NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = g_icon;
        Str(OverlayAvailable() ? StringIdForMode(g_mode) : IDS_UNAVAILABLE, nid.szTip,
            ARRAYSIZE(nid.szTip));
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    }

    NOTIFYICONDATAW ver;
    FillIconData(&ver);
    ver.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &ver);

    g_iconAdded = true;
    return true;
}

void RemoveTrayIcon() {
    if (!g_iconAdded) return;
    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_iconAdded = false;
}

// Overlay changes broadcast nothing, so state is pulled at the few moments it
// can have changed behind our back.
void ResyncFromSystem() {
    if (!OverlayAvailable()) return;
    const PowerMode actual = OverlayGetActual();
    if (actual == g_mode) return;
    g_mode = actual;
    UpdateTrayIcon();
}

void ApplyMode(PowerMode mode) {
    if (!OverlayAvailable() || mode == PowerMode::Unknown) return;
    if (!OverlaySet(mode)) return;

    g_mode = OverlayGetActual();
    UpdateTrayIcon();

    wchar_t text[128];
    OsdShow(Str(StringIdForMode(g_mode), text, ARRAYSIZE(text)));
}

bool BuildMenu() {
    g_menu = CreatePopupMenu();
    if (!g_menu) return false;

    wchar_t text[128];
    if (!AppendMenuW(g_menu, MF_STRING, IDM_MODE_EFF,
                     Str(IDS_MODE_EFF, text, ARRAYSIZE(text))))
        return false;
    if (!AppendMenuW(g_menu, MF_STRING, IDM_MODE_BALANCED,
                     Str(IDS_MODE_BALANCED, text, ARRAYSIZE(text))))
        return false;
    if (!AppendMenuW(g_menu, MF_STRING, IDM_MODE_PERF,
                     Str(IDS_MODE_PERF, text, ARRAYSIZE(text))))
        return false;
    if (!AppendMenuW(g_menu, MF_SEPARATOR, 0, nullptr)) return false;
    if (!AppendMenuW(g_menu, MF_STRING, IDM_AUTOSTART,
                     Str(IDS_MENU_AUTOSTART, text, ARRAYSIZE(text))))
        return false;
    if (!AppendMenuW(g_menu, MF_SEPARATOR, 0, nullptr)) return false;
    if (!AppendMenuW(g_menu, MF_STRING, IDM_QUIT,
                     Str(IDS_MENU_QUIT, text, ARRAYSIZE(text))))
        return false;
    return true;
}

void ShowContextMenu(int x, int y) {
    if (!g_menu) return;

    ResyncFromSystem();  // menu open is one of the resync points

    UINT checked = IDM_MODE_BALANCED;
    switch (g_mode) {
        case PowerMode::Efficiency:  checked = IDM_MODE_EFF; break;
        case PowerMode::Performance: checked = IDM_MODE_PERF; break;
        case PowerMode::Balanced:    checked = IDM_MODE_BALANCED; break;
        default:                     checked = 0; break;
    }
    if (checked) {
        CheckMenuRadioItem(g_menu, IDM_MODE_EFF, IDM_MODE_PERF, checked, MF_BYCOMMAND);
    }

    const UINT modeFlags = OverlayAvailable() ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);
    EnableMenuItem(g_menu, IDM_MODE_EFF, MF_BYCOMMAND | modeFlags);
    EnableMenuItem(g_menu, IDM_MODE_BALANCED, MF_BYCOMMAND | modeFlags);
    EnableMenuItem(g_menu, IDM_MODE_PERF, MF_BYCOMMAND | modeFlags);

    CheckMenuItem(g_menu, IDM_AUTOSTART,
                  MF_BYCOMMAND | (AutostartIsEnabled() ? MF_CHECKED : MF_UNCHECKED));

    // Without these two the menu will not dismiss on an outside click.
    SetForegroundWindow(g_wnd);
    TrackPopupMenuEx(g_menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, x, y, g_wnd,
                     nullptr);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
}

bool ArmThemeWatch() {
    if (!g_themeKey || !g_themeEvent) return false;
    ResetEvent(g_themeEvent);
    return RegNotifyChangeKeyValue(g_themeKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                                   g_themeEvent, TRUE) == ERROR_SUCCESS;
}

bool StartThemeWatch() {
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kThemeKey, 0, KEY_NOTIFY, &g_themeKey) !=
        ERROR_SUCCESS) {
        g_themeKey = nullptr;
        return false;  // WM_SETTINGCHANGE remains as the coarse fallback
    }
    g_themeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_themeEvent) {
        RegCloseKey(g_themeKey);
        g_themeKey = nullptr;
        return false;
    }
    return ArmThemeWatch();
}

void StopThemeWatch() {
    if (g_themeEvent) {
        CloseHandle(g_themeEvent);
        g_themeEvent = nullptr;
    }
    if (g_themeKey) {
        RegCloseKey(g_themeKey);
        g_themeKey = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
        // Explorer restarted; our icon went with it.
        g_iconAdded = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_TRAYICON:
            switch (LOWORD(lp)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    ApplyMode(OverlayNextMode(OverlayGetActual()));
                    return 0;
                case WM_CONTEXTMENU:
                    ShowContextMenu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
                    return 0;
                default:
                    return 0;
            }

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDM_MODE_EFF:      ApplyMode(PowerMode::Efficiency); return 0;
                case IDM_MODE_BALANCED: ApplyMode(PowerMode::Balanced); return 0;
                case IDM_MODE_PERF:     ApplyMode(PowerMode::Performance); return 0;
                case IDM_AUTOSTART:     AutostartSet(!AutostartIsEnabled()); return 0;
                case IDM_QUIT:          DestroyWindow(wnd); return 0;
                default:                break;
            }
            break;

        case WM_ENTERIDLE:
            if (wp == MSGF_MENU) ResyncFromSystem();
            break;

        case WM_TIMER:
            if (wp == kTimerResync) {
                ResyncFromSystem();
                return 0;
            }
            break;

        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) {
                const POWERBROADCAST_SETTING* s =
                    reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (s && IsEqualGUID(s->PowerSetting, kGuidAcDcPowerSource))
                    ResyncFromSystem();
            }
            return TRUE;

        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON) ResyncFromSystem();
            return 0;

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            UpdateTrayIcon();
            return 0;

        case WM_DPICHANGED:
            UpdateTrayIcon();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int RunMessageLoop() {
    for (;;) {
        const DWORD count = g_themeEvent ? 1 : 0;
        const DWORD result = MsgWaitForMultipleObjectsEx(
            count, count ? &g_themeEvent : nullptr, INFINITE, QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);

        if (count && result == WAIT_OBJECT_0) {
            UpdateTrayIcon();
            ArmThemeWatch();
            continue;
        }
        if (result == WAIT_OBJECT_0 + count) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }
        return 1;  // WAIT_FAILED: nothing sensible left to wait on
    }
}

void Cleanup() {
    if (g_powerNotify) {
        UnregisterPowerSettingNotification(g_powerNotify);
        g_powerNotify = nullptr;
    }
    if (g_wnd) {
        WTSUnRegisterSessionNotification(g_wnd);
        KillTimer(g_wnd, kTimerResync);
    }
    RemoveTrayIcon();
    StopThemeWatch();
    if (g_menu) {
        DestroyMenu(g_menu);
        g_menu = nullptr;
    }
    if (g_icon) {
        DestroyIcon(g_icon);
        g_icon = nullptr;
    }
    OsdShutdown();
    OverlayShutdown();
    if (g_singleInstance) {
        ReleaseMutex(g_singleInstance);
        CloseHandle(g_singleInstance);
        g_singleInstance = nullptr;
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_instance = instance;

    g_singleInstance = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!g_singleInstance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_singleInstance);
        g_singleInstance = nullptr;
        return 0;  // a second launch exits silently
    }

    OverlayInit();  // absence is a degraded mode, not a failure

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        Cleanup();
        return 1;
    }

    // A real (never shown) top-level window: HWND_MESSAGE windows do not receive
    // the TaskbarCreated broadcast or WM_SETTINGCHANGE.
    g_wnd = CreateWindowExW(0, kWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            instance, nullptr);
    if (!g_wnd) {
        Cleanup();
        return 1;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (g_taskbarCreated) ChangeWindowMessageFilterEx(g_wnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);

    if (!OsdInit(instance)) {
        // The OSD is cosmetic; losing it must not cost the tray icon.
        OsdShutdown();
    }

    g_mode = OverlayGetActual();

    if (!BuildMenu() || !AddTrayIcon()) {
        Cleanup();
        return 1;
    }

    StartThemeWatch();
    g_powerNotify = RegisterPowerSettingNotification(g_wnd, &kGuidAcDcPowerSource,
                                                     DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(g_wnd, NOTIFY_FOR_THIS_SESSION);
    SetTimer(g_wnd, kTimerResync, kResyncMs, nullptr);

    const int code = RunMessageLoop();
    Cleanup();
    return code;
}
