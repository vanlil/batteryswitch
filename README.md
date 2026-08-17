# PowerModeTray

> [!NOTE]
> Vibe coded with Claude in a single session, then manually acceptance tested.
> Read the source before you trust it.

A tray icon that cycles the Windows power mode overlay — the Settings › System ›
Power slider, without opening Settings. Single `.exe`, no installer, no
configuration, ~1.3 MB private working set. Windows 10 1803 or later, or 11.

![The tray icon in balanced mode, showing its tooltip](docs/screenshot.png)

## Using it

Download `PowerModeTray.exe` from [Releases](../../releases/latest) and run it;
there is nothing to install. It is unsigned, so **SmartScreen** warns on first
run — choose **More info**, then **Run anyway**.

- **Left click** — cycle: best power efficiency → balanced → best performance.
- **Right click** — pick a mode directly, toggle **Launch on logon**, or quit.

The icon is a gauge whose needle points low, middle, or high, and follows your
light/dark taskbar theme. Every change flashes the mode name on screen briefly.

**Launch on logon** writes `PowerModeTray` under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` and removes it when
unchecked. Nothing else is written outside that key — no settings file.

A second launch exits silently. Where overlays are unsupported, the icon still
appears with its mode commands greyed out.

## Building

Visual Studio 2026 with the C++ workload; CMake ships with it, so nothing needs
to be on `PATH`. Set `VS_ROOT` or `CMAKE` to build against a different install.

```
build           :: Release -> build\Release\PowerModeTray.exe
build Debug
clean
```

## Source layout

`src/main.cpp` (window, message loop, tray, menu) · `src/overlay.*` (power mode
API) · `src/osd.*` (on-screen notification) · `src/autostart.*` (Run key) ·
`res/` (icons, strings, manifest).

The overlay API is undocumented and resolved from `powrprof.dll` at runtime, so
a Windows change degrades to disabled commands instead of a crash — and
`overlay.cpp` is the only file that would need fixing.

## License

MIT — see [LICENSE](LICENSE).
