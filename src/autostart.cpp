#include "autostart.h"

#include <windows.h>

namespace {

// Registry paths, not UI text: deliberately not in the string table.
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kValueName[] = L"PowerModeTray";

// "C:\path\PowerModeTray.exe" plus quotes and terminator.
bool QuotedModulePath(wchar_t* out, DWORD cch) {
    if (cch < 4) return false;
    out[0] = L'"';
    const DWORD n = GetModuleFileNameW(nullptr, out + 1, cch - 3);
    if (n == 0 || n >= cch - 3) return false;  // failure or truncation
    out[1 + n] = L'"';
    out[2 + n] = L'\0';
    return true;
}

}  // namespace

bool AutostartIsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD bytes = 0;
    const LSTATUS st = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &bytes);
    RegCloseKey(key);

    // Presence is what the menu reflects; the stored path is rewritten on every
    // enable, so a stale path is corrected rather than reported as "off".
    return st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool AutostartSet(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;

    LSTATUS st;
    if (enable) {
        wchar_t path[MAX_PATH + 3];
        if (!QuotedModulePath(path, ARRAYSIZE(path))) {
            RegCloseKey(key);
            return false;
        }
        const DWORD bytes = static_cast<DWORD>((lstrlenW(path) + 1) * sizeof(wchar_t));
        st = RegSetValueExW(key, kValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(path), bytes);
    } else {
        st = RegDeleteValueW(key, kValueName);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;  // already off
    }

    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}
