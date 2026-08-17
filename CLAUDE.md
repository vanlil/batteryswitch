# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## PowerModeTray

Win32 tray utility (Windows 10 1803+ / 11) that cycles the Windows **power mode overlay**.
No installer, single .exe, target working set < 4 MB.

## Hard constraints

- Pure Win32 + COM-free. No MFC, ATL, Qt, WIL, STL containers in hot paths, no third-party deps.
- C++20, MSVC (`cl`), CMake ≥ 3.25. `/MT`, `/GR-`, `/EHsc-` (`/EHs-c-`), `/O1`, `/GS-` off only if measured.
- No CRT startup bloat: `wWinMain`, no iostreams, no `std::string` in the message loop.
- Single instance: named mutex `Local\PowerModeTray.SingleInstance`; second launch exits silently.
- UNICODE / _UNICODE only. Per-monitor DPI v2 via manifest (`dpiAwareness = PerMonitorV2`).
- Manifest requires `comctl32` v6 and `requestedExecutionLevel=asInvoker`. Never elevate.

## Power mode API (undocumented — isolate in `overlay.cpp`)

Resolve by name from `powrprof.dll` with `GetProcAddress`; never link statically:

```cpp
DWORD (WINAPI *PowerSetActiveOverlayScheme)(GUID overlay);
DWORD (WINAPI *PowerGetActualOverlayScheme)(GUID* out);   // user selection
DWORD (WINAPI *PowerGetEffectiveOverlayScheme)(GUID* out); // what the OS applies now
```

Overlay GUIDs:

| Mode | GUID |
|---|---|
| Best power efficiency | `961cc777-2547-4f9d-8174-7d86181b8a7a` |
| Balanced (recommended) | `GUID_NULL` (all zeros) |
| Best performance | `ded574b5-45a0-4f42-8737-46345c09c238` |

Failure modes to handle, not assume away:

- Any export missing → show the tray icon disabled with a tooltip, do not crash, do not fall back to
  classic schemes.
- `PowerSetActiveOverlayScheme` returns a Win32 error code (0 = success), not HRESULT.
- Overlays are unsupported on some desktops/OEM configs and under some AC/DC policies; a set can
  succeed while `PowerGetEffectiveOverlayScheme` reports something else. Icon reflects **actual**,
  not effective.
- Overlay changes broadcast **no** notification. `GUID_POWERSCHEME_PERSONALITY` does **not** fire for
  overlay changes. Resync by querying `PowerGetActualOverlayScheme` on `WM_ENTERIDLE`/menu open,
  on `WM_POWERBROADCAST` (`GUID_ACDC_POWER_SOURCE`), on `WM_WTSSESSION_CHANGE` unlock, and on a
  60 s timer at most. No tight polling.
- Undocumented ordinals/behaviour can change; keep every use behind `overlay.h` so one file is the
  blast radius of a Windows change.

## Behaviour

- Left click (`WM_LBUTTONUP` on `NIN_SELECT`): cycle efficiency → balanced → performance → efficiency.
- Right click: `TrackPopupMenuEx` with `TPM_RIGHTBUTTON`. Three radio-checked mode items
  (`CheckMenuRadioItem`), separator, `Launch on logon` (`MF_CHECKED` toggle), separator,
  `About...` (opens the project page via `ShellExecuteW`, URL from the string table), `Quit`.
  Call `SetForegroundWindow` before and post `WM_NULL` after, or the menu will not dismiss.
- Autostart: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value `PowerModeTray`, quoted
  full path from `GetModuleFileNameW`. Menu check state is read from the registry, not cached.
- OSD after every change: layered top-most window, `WS_EX_LAYERED | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT`, no taskbar entry, centered on the monitor under the
  cursor (`MonitorFromPoint` → `MONITORINFO.rcWork`). ~180×64 px at 96 DPI, scaled by DPI.
  `UpdateLayeredWindow` with a premultiplied 32-bit DIB; alpha 0.75 hold 1.2 s, then fade 250 ms.
  Reuse one window and one DIB for the lifetime of the process — do not create per notification.
  A rapid second click restarts the timer instead of stacking windows.

## Tray icon

- `Shell_NotifyIconW` with `NIF_GUID` and a fixed `guidItem` so position survives restarts.
  Handle the registered `TaskbarCreated` message and re-add the icon after Explorer restarts.
- Six embedded `.ico` resources: `{perf, balanced, eff} × {light, dark}`, each with 16/20/24/32 px
  frames. Load with `LoadIconWithScaleDown` at `GetSystemMetricsForDpi(SM_CXSMICON, dpi)`.
- Theme: read `HKCU\...\Themes\Personalize\SystemUsesLightTheme` (1 → dark glyph on light taskbar).
  Watch with `RegNotifyChangeKeyValue` (async event, no polling thread spin) and on
  `WM_SETTINGCHANGE` / `WM_DPICHANGED` reload the icon.
- Tooltip = current mode name, from the string table.

## Layout

`src/` → `main.cpp` (window/message loop, tray, menu), `overlay.cpp/.h` (powrprof shim),
`osd.cpp/.h` (layered window), `autostart.cpp/.h` (registry), `res/` (`.rc`, icons, manifest).

## Build

CMake is not on `PATH`, so use the top-level scripts rather than calling `cmake` directly:

```
build           :: Release -> build\Release\PowerModeTray.exe
build Debug     :: Debug   -> build\Debug\PowerModeTray.exe
clean           :: removes build\ (leaves the checked-in icons alone)
```

`build.cmd` configures on first run only and holds the two tool paths as overridable variables —
`VS_ROOT` (default `C:\Program Files\Microsoft Visual Studio\18\Community`) and `CMAKE`, derived
from it. Point either at a different install to build with another toolchain; no other path is
hard-coded. The underlying invocation is `cmake --preset vs2026` (Visual Studio 18 2026, x64) then
`cmake --build build --config <cfg>`.

`CMakeLists.txt` strips the `/EHsc` and `/O2` CMake injects by default — do not reintroduce them.
The manifest is embedded via the `.rc`, so the link uses `/MANIFEST:NO`.

The six `res/icons/*.ico` are final art: a speedometer — an arc open at the bottom with a needle
pointing low, middle, or high for efficiency, balanced, and performance — monochrome so the glyph
follows the taskbar theme rather than the mode. Needle *angle* carries the state, which is why this
survives 16 px where counting bars did not. Treat them as source. Any replacement keeps the file
names and the 16/20/24/32 frame set. Two things the 16 px frame will punish: a needle long enough to
touch the arc (the two merge into a closed blob) and a centre hub at all (same problem) — the hub is
drawn only at 20 px and above.

## Verification

No unit test framework, by design — a test harness would be the first third-party dependency and
the logic worth testing is almost all Win32 side effects. Verification is:

- the build must stay clean at `/W4` (warnings are the closest thing to a test suite here),
- `dumpbin /dependents` must not list `powrprof.dll` or `shcore.dll`; both are `GetProcAddress`-only,
- private working set under 4 MB (`(Get-Counter "\Process(PowerModeTray)\Working Set - Private")`),
- manual acceptance by the user.

## Cross-cutting behaviour worth knowing before editing

- **State flows one way.** `g_mode` in `main.cpp` is a cache of `PowerGetActualOverlayScheme`, never
  a source of truth. Every path that changes it calls `UpdateTrayIcon()`. Because overlay changes
  broadcast nothing, `ResyncFromSystem()` is called from five places (menu open, `WM_ENTERIDLE`,
  AC/DC broadcast, session unlock, 60 s timer) — that redundancy is deliberate, not leftover.
- **The message loop is not `GetMessage`.** `RunMessageLoop()` uses `MsgWaitForMultipleObjectsEx` so
  the `RegNotifyChangeKeyValue` theme event is waited on alongside the queue. Adding another waitable
  handle means extending the array there, not spawning a thread.
- **Degraded mode is a first-class path.** A missing export leaves the program running with mode
  commands greyed out and the `IDS_UNAVAILABLE` tooltip. A failed `OsdInit` likewise loses only the
  OSD. Neither is allowed to take down the tray icon.
- **The tray icon has a fallback identity.** `NIF_GUID` registration is bound to the exe path, so
  `AddTrayIcon()` retries with a `uID` key when the add fails (typically after the binary moved).
- **One OSD DIB, allocated for the worst case.** `osd.cpp` allocates 450×160 (240 DPI cap) once and
  draws into the top-left sub-rect, passing `psize` to `UpdateLayeredWindow` — that is how "one DIB
  for the lifetime" survives per-monitor DPI. Alpha in the DIB is only 0 or 255; the 0.75 hold and
  the fade come from `BLENDFUNCTION.SourceConstantAlpha`, so fading never redraws.
- **Costs currently on the books:** one 60 s `WM_TIMER`, two short-lived OSD timers while visible,
  one event + one open `HKEY` for the theme watch, one 288 KB DIB. No threads.

## Rules for Claude

- Never add a dependency, a thread, or a timer without saying what it costs in working set.
- All strings in the resource string table, none hard-coded in code.
- Check every Win32 return value; no silent failure paths, no `assert`-only handling in release.
- Do not "improve" the design by adding settings files, telemetry, updaters, or a config UI.
