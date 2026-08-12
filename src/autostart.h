#pragma once

// HKCU Run entry. State is read from the registry on every query so an external
// edit (or a user removing the entry in Task Manager) is reflected immediately.

bool AutostartIsEnabled();
bool AutostartSet(bool enable);
