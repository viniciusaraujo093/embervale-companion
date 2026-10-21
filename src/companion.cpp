#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Prova de conceito: DLL independente, sem hooks, injecao ou acesso a memoria do jogo.
// O carregamento pelo Enshrouded ainda NAO foi implementado.
extern "C" __declspec(dllexport) int CompanionVersion() noexcept {
    return 1;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        OutputDebugStringW(L"[Embervale Companion] Modulo carregado (teste).\n");
    }
    return TRUE;
}
