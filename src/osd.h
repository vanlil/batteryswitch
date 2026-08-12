#pragma once
#include <windows.h>

// One layered window and one DIB for the lifetime of the process. OsdShow only
// redraws and restarts the timers; a rapid second call never stacks windows.

bool OsdInit(HINSTANCE instance);
void OsdShutdown();

// text is borrowed for the duration of the call.
void OsdShow(const wchar_t* text);
