#include "overlay.h"

// Undocumented, present since Windows 10 1803. Resolved by name at runtime so a
// missing export degrades to "commands disabled" instead of a load failure.
typedef DWORD(WINAPI* PFN_PowerSetActiveOverlayScheme)(GUID overlay);
typedef DWORD(WINAPI* PFN_PowerGetActualOverlayScheme)(GUID* out);
typedef DWORD(WINAPI* PFN_PowerGetEffectiveOverlayScheme)(GUID* out);

namespace {

// 961cc777-2547-4f9d-8174-7d86181b8a7a
const GUID kOverlayEfficiency = {
    0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
// ded574b5-45a0-4f42-8737-46345c09c238
const GUID kOverlayPerformance = {
    0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};
// Balanced is GUID_NULL.
const GUID kOverlayBalanced = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

HMODULE g_powrprof = nullptr;
PFN_PowerSetActiveOverlayScheme g_set = nullptr;
PFN_PowerGetActualOverlayScheme g_getActual = nullptr;
PFN_PowerGetEffectiveOverlayScheme g_getEffective = nullptr;

const GUID* GuidFor(PowerMode mode) {
    switch (mode) {
        case PowerMode::Efficiency:  return &kOverlayEfficiency;
        case PowerMode::Balanced:    return &kOverlayBalanced;
        case PowerMode::Performance: return &kOverlayPerformance;
        default:                     return nullptr;
    }
}

PowerMode ModeFor(const GUID& g) {
    if (IsEqualGUID(g, kOverlayEfficiency))  return PowerMode::Efficiency;
    if (IsEqualGUID(g, kOverlayPerformance)) return PowerMode::Performance;
    if (IsEqualGUID(g, kOverlayBalanced))    return PowerMode::Balanced;
    return PowerMode::Unknown;
}

}  // namespace

bool OverlayInit() {
    if (g_powrprof) return OverlayAvailable();

    g_powrprof = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_powrprof) return false;

    g_set = reinterpret_cast<PFN_PowerSetActiveOverlayScheme>(
        GetProcAddress(g_powrprof, "PowerSetActiveOverlayScheme"));
    g_getActual = reinterpret_cast<PFN_PowerGetActualOverlayScheme>(
        GetProcAddress(g_powrprof, "PowerGetActualOverlayScheme"));
    g_getEffective = reinterpret_cast<PFN_PowerGetEffectiveOverlayScheme>(
        GetProcAddress(g_powrprof, "PowerGetEffectiveOverlayScheme"));

    if (!OverlayAvailable()) {
        // Keep the module loaded but treat the feature as absent; unloading here
        // would only matter if we intended to retry, and we do not.
        return false;
    }
    return true;
}

void OverlayShutdown() {
    if (g_powrprof) {
        FreeLibrary(g_powrprof);
        g_powrprof = nullptr;
    }
    g_set = nullptr;
    g_getActual = nullptr;
    g_getEffective = nullptr;
}

bool OverlayAvailable() {
    // PowerGetEffectiveOverlayScheme is not required for correct behaviour: the
    // UI reflects the actual (user-selected) overlay, never the effective one.
    return g_set != nullptr && g_getActual != nullptr;
}

PowerMode OverlayGetActual() {
    if (!g_getActual) return PowerMode::Unknown;
    GUID current = {};
    const DWORD err = g_getActual(&current);
    if (err != ERROR_SUCCESS) return PowerMode::Unknown;
    return ModeFor(current);
}

bool OverlaySet(PowerMode mode) {
    if (!g_set) return false;
    const GUID* guid = GuidFor(mode);
    if (!guid) return false;
    // Win32 error code, not an HRESULT.
    return g_set(*guid) == ERROR_SUCCESS;
}

PowerMode OverlayNextMode(PowerMode current) {
    switch (current) {
        case PowerMode::Efficiency:  return PowerMode::Balanced;
        case PowerMode::Balanced:    return PowerMode::Performance;
        case PowerMode::Performance: return PowerMode::Efficiency;
        // An overlay we do not recognise (OEM custom mode) cycles back to a
        // known state rather than sticking.
        default:                     return PowerMode::Balanced;
    }
}
