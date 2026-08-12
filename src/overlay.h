#pragma once
#include <windows.h>

// Everything that touches the undocumented powrprof overlay exports lives behind
// this header. A Windows change to those exports must not require edits anywhere
// else in the program.

enum class PowerMode {
    Efficiency,
    Balanced,
    Performance,
    Unknown,
};

// Loads powrprof.dll and resolves the overlay exports. Returns false when the
// build of Windows does not export them; the program still runs, with the mode
// commands disabled.
bool OverlayInit();
void OverlayShutdown();

bool OverlayAvailable();

// The user's selection, not what the OS is currently applying. The icon and the
// menu reflect this.
PowerMode OverlayGetActual();

// Returns true only when the export reported success (0). A success here does
// not guarantee the OS honours the overlay.
bool OverlaySet(PowerMode mode);

// efficiency -> balanced -> performance -> efficiency
PowerMode OverlayNextMode(PowerMode current);
