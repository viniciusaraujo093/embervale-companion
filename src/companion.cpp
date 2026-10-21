#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "companion/localization.h"

// Integration is intentionally not implemented: no hooks or game memory access.
extern "C" __declspec(dllexport) int CompanionVersion() noexcept { return 3; }
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        OutputDebugStringW(L"[Embervale Companion] v0.3 module loaded (diagnostic only).\n");
    return TRUE;
}
