#pragma once
#include <windows.h>
#include <cstddef>

// A background fixture must not reject another application's foreground change.
// This classifies an observation only; it does not establish a continuous history.
template<std::size_t Count>
static bool ViForegroundOutsideOwned(DWORD observedOwner, const DWORD (&owned)[Count]) {
    for (DWORD owner : owned) if (owner && owner == observedOwner) return false;
    return true;
}
