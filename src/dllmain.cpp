#include <windows.h>
#include "../include/BattleLogger.h"

namespace Hooks {
void Install();
void Uninstall();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringA("[BattleLogger] DLL_PROCESS_ATTACH\n");
        Hooks::Install();
    } else if (reason == DLL_PROCESS_DETACH) {
        Hooks::Uninstall();
    }
    return TRUE;
}

// Экспорт для patcher_x86.ini (альтернативная загрузка без HD-пака)
extern "C" __declspec(dllexport) void BattleLogger_Init() {
    OutputDebugStringA("[BattleLogger] BattleLogger_Init called by patcher_x86\n");
}
