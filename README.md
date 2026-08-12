# PowerModeTray

> [!NOTE]
> Vibe coded with Claude (Claude Code, Opus 5) in a single session, then
> manually acceptance tested. Read the source before you trust it.

A tray icon that cycles the Windows power mode overlay — the same setting as the
Settings › System › Power slider, without opening Settings. Single `.exe`, no
installer, no configuration, ~1.3 MB private working set.

Windows 10 1803 or later, or Windows 11.

![The tray icon in balanced mode, showing its tooltip](docs/screenshot.png)

## Using it

Run `PowerModeTray.exe`. The icon shows the current mode as a battery with one,
two, or three bars.

- **Left click** — cycle: best power efficiency → balanced → best performance.
- **Right click** — pick a mode directly, toggle **Launch on logon**, or quit.

Every change flashes the mode name in the middle of the screen for about a
second. The glyph follows your light/dark taskbar theme and updates when you
switch it.

**Launch on logon** writes `PowerModeTray` under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` and removes it when
unchecked. Nothing else is written outside that key — no settings file.

A second launch exits silently rather than adding a second icon.

On hardware or power policies that do not support overlays, the icon still
appears, the mode commands are greyed out, and the tooltip says so.

## Building

Visual Studio 2026 with the C++ workload. CMake ships with it, so nothing needs
to be on `PATH`:

```
build           :: Release -> build\Release\PowerModeTray.exe
build Debug
clean
```

Set `VS_ROOT` (or `CMAKE` directly) in the environment to build against a
different install. Underneath it is `cmake --preset vs2026` plus
`cmake --build build --config <cfg>`.

## Source layout

| | |
|---|---|
| `src/main.cpp` | window, message loop, tray icon, menu |
| `src/overlay.*` | the power mode API, isolated |
| `src/osd.*` | the on-screen mode notification |
| `src/autostart.*` | the Run key |
| `res/` | icons, string table, manifest |

The overlay API is undocumented and resolved from `powrprof.dll` at runtime, so
a Windows change degrades to disabled commands instead of a crash — and
`overlay.cpp` is the only file that would need fixing.

## License

MIT — see [LICENSE](LICENSE).
