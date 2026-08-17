# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## PowerModeTray

Win32 tray utility (Windows 10 1803+ / 11) that cycles the Windows **power mode overlay**.
No installer, single .exe, target working set < 4 MB.

## Hard constraints

- Pure Win32, COM-free. No MFC, ATL, Qt, WIL, STL containers in hot paths, no third-party deps.
- C++20, MSVC, CMake ≥ 3.25. `/MT`, `/GR-`, `/EHs-c-`, `/O1`. `/GS-` off only if measured.
- `wWinMain`, no iostreams, no `std::string` in the message loop.
- UNICODE only. Per-monitor DPI v2 via manifest. `asInvoker` — never elevate.
- Single instance: named mutex `Local\PowerModeTray.SingleInstance`; second launch exits silently.

## Power mode API (undocumented — isolate in `overlay.cpp`)

Resolve by name from `powrprof.dll` with `GetProcAddress`; never link statically:

```cpp
DWORD (WINAPI *PowerSetActiveOverlayScheme)(GUID overlay);
DWORD (WINAPI *PowerGetActualOverlayScheme)(GUID* out);    // user selection
DWORD (WINAPI *PowerGetEffectiveOverlayScheme)(GUID* out); // what the OS applies now
```

| Mode | GUID |
|---|---|
| Best power efficiency | `961cc777-2547-4f9d-8174-7d86181b8a7a` |
| Balanced | `GUID_NULL` (all zeros) |
| Best performance | `ded574b5-45a0-4f42-8737-46345c09c238` |

- Returns a Win32 error code (0 = success), not an HRESULT.
- Any export missing → tray icon stays, mode commands greyed, `IDS_UNAVAILABLE` tooltip. Never crash,
  never fall back to classic schemes.
- Overlays are unsupported on some desktops/OEM configs; a set can succeed while
  `PowerGetEffectiveOverlayScheme` disagrees. The UI reflects **actual**, not effective.
- Keep every use behind `overlay.h`: one file is the blast radius of a Windows change.

### Resync

Overlay changes broadcast nothing — `GUID_POWERSCHEME_PERSONALITY` does not fire for them. The power
service mirrors the active overlay into `HKLM\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes`
(`ActiveOverlay{Ac,Dc}PowerScheme`), so a `RegNotifyChangeKeyValue` watch there is the primary signal
(`KEY_NOTIFY` suffices, no elevation). Use it only as a *change signal* and re-read through
`PowerGetActualOverlayScheme` — parsing those values makes the AC/DC split your problem. Backstops:
menu open, `WM_ENTERIDLE`, `WM_POWERBROADCAST` (`GUID_ACDC_POWER_SOURCE`), `WM_WTSSESSION_CHANGE`
unlock, 60 s timer. No tight polling.

## Behaviour

- Left click (`NIN_SELECT`): cycle efficiency → balanced → performance → efficiency.
- Right click: `TrackPopupMenuEx` with `TPM_RIGHTBUTTON`. Three radio-checked mode items
  (`CheckMenuRadioItem`), separator, `Launch on logon` (`MF_CHECKED` toggle), separator, `About...`
  (`ShellExecuteW`, URL from the string table), `Quit`. Call `SetForegroundWindow` before and post
  `WM_NULL` after, or the menu will not dismiss.
- Autostart: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `PowerModeTray`, quoted full
  path from `GetModuleFileNameW`. Menu check state read from the registry, never cached.
- OSD after every change: layered top-most window (`WS_EX_LAYERED | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT`), no taskbar entry, centered on the monitor under the cursor
  (`MonitorFromPoint` → `MONITORINFO.rcWork`), ~180×64 px at 96 DPI and DPI-scaled.
  `UpdateLayeredWindow` with a premultiplied 32-bit DIB; alpha 0.75 hold 1.2 s, then fade 250 ms. One
  window and one DIB for the process lifetime; a rapid second click restarts the timer instead of
  stacking windows.

## Tray icon

- `Shell_NotifyIconW` with `NIF_GUID` and a fixed `guidItem` so position survives restarts. Handle the
  registered `TaskbarCreated` message and re-add after Explorer restarts.
- Six embedded `.ico` resources, `{perf, balanced, eff} × {light, dark}`, each with 16/20/24/32 px
  frames. Load with `LoadIconWithScaleDown` at `GetSystemMetricsForDpi(SM_CXSMICON, dpi)`.
- The art is a speedometer: an arc open at the bottom with a needle pointing low, middle, or high.
  Monochrome, so the glyph follows the taskbar theme rather than the mode. Treat the files as source;
  a replacement keeps the names and the frame set. At 16 px a needle long enough to touch the arc
  merges into a closed blob, and a centre hub does the same — the hub is drawn only at 20 px and up.
- Theme: `HKCU\...\Themes\Personalize\SystemUsesLightTheme` (1 → dark glyph on light taskbar). Watch
  with `RegNotifyChangeKeyValue`; also reload on `WM_SETTINGCHANGE` / `WM_DPICHANGED`.
- Tooltip = current mode name, from the string table.

## Layout and build

`src/` → `main.cpp` (window/message loop, tray, menu), `overlay.cpp/.h` (powrprof shim),
`osd.cpp/.h` (layered window), `autostart.cpp/.h` (registry); `res/` → `.rc`, icons, manifest.

CMake is not on `PATH`, so use the top-level scripts rather than calling `cmake` directly:

```
build           :: Release -> build\Release\PowerModeTray.exe
build Debug     :: Debug   -> build\Debug\PowerModeTray.exe
clean           :: removes build\
```

`build.cmd` configures on first run only and holds `VS_ROOT` and `CMAKE` as overridable variables; no
other path is hard-coded. `CMakeLists.txt` strips the `/EHsc` and `/O2` CMake injects by default — do
not reintroduce them. The manifest is embedded via the `.rc`, so the link uses `/MANIFEST:NO`.

## Verification

No unit test framework, by design — it would be the first third-party dependency, and the logic worth
testing is almost all Win32 side effects.

- The build stays clean at `/W4`.
- `dumpbin /dependents` lists neither `powrprof.dll` nor `shcore.dll`; both are `GetProcAddress`-only.
- Private working set under 4 MB (`Get-Counter "\Process(PowerModeTray)\Working Set - Private"`).
- Manual acceptance by the user.

## Invariants

- **State flows one way.** `g_mode` is a cache of `PowerGetActualOverlayScheme`, never a source of
  truth. Every path that changes it calls `UpdateTrayIcon()`.
- **The message loop is not `GetMessage`.** `RunMessageLoop()` uses `MsgWaitForMultipleObjectsEx` to
  wait on the two `RegNotifyChangeKeyValue` events (theme, overlay) alongside the queue. The handle
  array is built once and each watch is optional. Another waitable handle means extending that array,
  not spawning a thread.
- **Degraded mode is first-class.** A missing export or a failed `OsdInit` costs only that feature;
  neither may take down the tray icon.
- **The tray icon has a fallback identity.** `NIF_GUID` is bound to the exe path, so `AddTrayIcon()`
  retries with a `uID` key when the add fails (typically after the binary moved).
- **One OSD DIB, allocated for the worst case.** 450×160 (240 DPI cap) once, drawn into the top-left
  sub-rect with `psize` passed to `UpdateLayeredWindow`. DIB alpha is only 0 or 255; the hold and fade
  come from `BLENDFUNCTION.SourceConstantAlpha`, so fading never redraws.
- **Costs on the books:** one 60 s `WM_TIMER`, two short-lived OSD timers while visible, two events +
  two open `HKEY`s (theme, overlay), one 288 KB DIB. No threads.

## Rules for Claude

- Never add a dependency, a thread, or a timer without saying what it costs in working set.
- All strings in the resource string table, none hard-coded in code.
- Check every Win32 return value; no silent failure paths, no `assert`-only handling in release.
- Do not "improve" the design by adding settings files, telemetry, updaters, or a config UI.
