#include "osd.h"

namespace {

const wchar_t kClassName[] = L"PowerModeTray.Osd";

// Logical size at 96 DPI, per the design.
const int kBaseWidth = 180;
const int kBaseHeight = 64;
const int kBaseRadius = 8;
const int kBasePointSize = 11;

// The DIB is allocated once at the largest DPI we scale to and never resized;
// UpdateLayeredWindow only ever reads the top-left w*h sub-rect. 250% covers
// every shipping Windows scale factor, and costs 450*160*4 = 288 KB of a
// commit that is only touched up to the size actually drawn.
const UINT kMaxDpi = 240;
const int kMaxWidth = MulDiv(kBaseWidth, kMaxDpi, 96);
const int kMaxHeight = MulDiv(kBaseHeight, kMaxDpi, 96);

const COLORREF kPanelColor = RGB(0x1f, 0x1f, 0x1f);  // must not be pure black
const COLORREF kTextColor = RGB(0xf2, 0xf2, 0xf2);
const BYTE kHoldAlpha = 191;  // 0.75
const UINT kHoldMs = 1200;
const UINT kFadeMs = 250;
const UINT kFadeTickMs = 16;

const UINT_PTR kTimerHold = 1;
const UINT_PTR kTimerFade = 2;

typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);

HWND g_wnd = nullptr;
HDC g_memDc = nullptr;
HBITMAP g_dib = nullptr;
HBITMAP g_oldBitmap = nullptr;
DWORD* g_bits = nullptr;
HFONT g_font = nullptr;
UINT g_fontDpi = 0;
HMODULE g_shcore = nullptr;
PFN_GetDpiForMonitor g_getDpiForMonitor = nullptr;

int g_width = 0;
int g_height = 0;
ULONGLONG g_fadeStart = 0;

UINT DpiForPoint(POINT pt) {
    if (g_getDpiForMonitor) {
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        UINT x = 0, y = 0;
        if (SUCCEEDED(g_getDpiForMonitor(mon, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y)) && x != 0)
            return x;
    }
    return GetDpiForSystem();
}

bool EnsureFont(UINT dpi) {
    if (g_font && g_fontDpi == dpi) return true;

    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(kBasePointSize, static_cast<int>(dpi), 72);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;  // ClearType would fringe on the panel
    lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);

    HFONT font = CreateFontIndirectW(&lf);
    if (!font) return false;

    if (g_font) DeleteObject(g_font);
    g_font = font;
    g_fontDpi = dpi;
    return true;
}

// GDI text and fills leave the alpha byte at zero. The panel is the only opaque
// area, and it is the only place with a non-zero RGB, so alpha is recovered
// from the colour. Values are premultiplied by construction: alpha is 0 or 255.
void ApplyAlpha(int w, int h) {
    for (int y = 0; y < h; ++y) {
        DWORD* row = g_bits + static_cast<size_t>(y) * kMaxWidth;
        for (int x = 0; x < w; ++x) {
            if (row[x] & 0x00ffffff) row[x] |= 0xff000000;
        }
    }
}

bool Render(const wchar_t* text, int w, int h, UINT dpi) {
    if (!EnsureFont(dpi)) return false;

    ZeroMemory(g_bits, static_cast<size_t>(kMaxWidth) * kMaxHeight * sizeof(DWORD));

    const int radius = MulDiv(kBaseRadius, static_cast<int>(dpi), 96) * 2;
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    if (!rgn) return false;
    HBRUSH brush = CreateSolidBrush(kPanelColor);
    if (!brush) {
        DeleteObject(rgn);
        return false;
    }
    FillRgn(g_memDc, rgn, brush);
    DeleteObject(brush);
    DeleteObject(rgn);

    HGDIOBJ oldFont = SelectObject(g_memDc, g_font);
    SetBkMode(g_memDc, TRANSPARENT);
    SetTextColor(g_memDc, kTextColor);
    RECT rc = {0, 0, w, h};
    DrawTextW(g_memDc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(g_memDc, oldFont);

    GdiFlush();
    ApplyAlpha(w, h);
    return true;
}

bool Blend(BYTE alpha, const POINT* topLeft, const SIZE* size) {
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = alpha;
    bf.AlphaFormat = AC_SRC_ALPHA;

    if (!topLeft) {
        // Alpha-only update: every other parameter may be NULL.
        return UpdateLayeredWindow(g_wnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0,
                                   &bf, ULW_ALPHA) != FALSE;
    }
    POINT src = {0, 0};
    POINT dst = *topLeft;
    SIZE sz = *size;
    return UpdateLayeredWindow(g_wnd, nullptr, &dst, &sz, g_memDc, &src, 0, &bf,
                               ULW_ALPHA) != FALSE;
}

void Hide() {
    KillTimer(g_wnd, kTimerHold);
    KillTimer(g_wnd, kTimerFade);
    ShowWindow(g_wnd, SW_HIDE);
}

LRESULT CALLBACK OsdWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kTimerHold) {
                KillTimer(wnd, kTimerHold);
                g_fadeStart = GetTickCount64();
                if (!SetTimer(wnd, kTimerFade, kFadeTickMs, nullptr)) Hide();
                return 0;
            }
            if (wp == kTimerFade) {
                const ULONGLONG elapsed = GetTickCount64() - g_fadeStart;
                if (elapsed >= kFadeMs) {
                    Hide();
                    return 0;
                }
                const BYTE alpha = static_cast<BYTE>(
                    kHoldAlpha - (kHoldAlpha * elapsed) / kFadeMs);
                if (!Blend(alpha, nullptr, nullptr)) Hide();
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

bool OsdInit(HINSTANCE instance) {
    // GetDpiForMonitor lives in shcore.dll; resolved by name to keep the import
    // table (and the failure surface) small.
    g_shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_shcore) {
        g_getDpiForMonitor = reinterpret_cast<PFN_GetDpiForMonitor>(
            GetProcAddress(g_shcore, "GetDpiForMonitor"));
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OsdWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    g_wnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kClassName, L"", WS_POPUP, 0, 0, kMaxWidth, kMaxHeight, nullptr, nullptr,
        instance, nullptr);
    if (!g_wnd) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    g_memDc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!g_memDc) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kMaxWidth;
    bi.bmiHeader.biHeight = -kMaxHeight;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    g_dib = CreateDIBSection(g_memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!g_dib || !bits) return false;
    g_bits = static_cast<DWORD*>(bits);
    g_oldBitmap = static_cast<HBITMAP>(SelectObject(g_memDc, g_dib));
    return true;
}

void OsdShutdown() {
    if (g_wnd) {
        Hide();
        DestroyWindow(g_wnd);
        g_wnd = nullptr;
    }
    if (g_memDc) {
        if (g_oldBitmap) SelectObject(g_memDc, g_oldBitmap);
        DeleteDC(g_memDc);
        g_memDc = nullptr;
    }
    if (g_dib) {
        DeleteObject(g_dib);
        g_dib = nullptr;
        g_bits = nullptr;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    if (g_shcore) {
        FreeLibrary(g_shcore);
        g_shcore = nullptr;
        g_getDpiForMonitor = nullptr;
    }
}

void OsdShow(const wchar_t* text) {
    if (!g_wnd || !g_bits || !text) return;

    POINT cursor = {};
    if (!GetCursorPos(&cursor)) cursor.x = cursor.y = 0;

    UINT dpi = DpiForPoint(cursor);
    if (dpi == 0) dpi = 96;
    if (dpi > kMaxDpi) dpi = kMaxDpi;

    g_width = MulDiv(kBaseWidth, static_cast<int>(dpi), 96);
    g_height = MulDiv(kBaseHeight, static_cast<int>(dpi), 96);

    MONITORINFO mi = {sizeof(mi)};
    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) work = mi.rcWork;

    POINT topLeft = {
        work.left + ((work.right - work.left) - g_width) / 2,
        work.top + ((work.bottom - work.top) - g_height) / 2,
    };
    SIZE size = {g_width, g_height};

    if (!Render(text, g_width, g_height, dpi)) return;

    // Restart the dwell rather than stacking a second notification.
    KillTimer(g_wnd, kTimerHold);
    KillTimer(g_wnd, kTimerFade);

    if (!Blend(kHoldAlpha, &topLeft, &size)) return;

    SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (!SetTimer(g_wnd, kTimerHold, kHoldMs, nullptr)) Hide();
}
