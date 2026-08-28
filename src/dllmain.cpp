#include <windows.h>
#include "../include/BattleLogger.h"

namespace Hooks {
void Install();
void Uninstall();
}

// Entry для patcher_x86 / HD
// HD грузит DLL через LoadLibrary, patcher_x86 вызывает DllMain

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Маркер что DLL загрузилась (виден без дебаггера)
        HANDLE f = CreateFileA("Z:\\Games2\\HoM&M III by LC\\Logs\\BattleLogger_DllMain.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            const char* msg = "DLL_PROCESS_ATTACH\n";
            DWORD w; WriteFile(f, msg, (DWORD)strlen(msg), &w, nullptr);
            CloseHandle(f);
        }
        OutputDebugStringA("[BattleLogger] DLL_PROCESS_ATTACH\n");
        Hooks::Install();
    } else if (reason == DLL_PROCESS_DETACH) {
        Hooks::Uninstall();
        BattleLogger::Instance().CloseBattle("DLL unload");
    }
    return TRUE;
}

// Экспорт для patcher_x86.ini:
// [BattleLogger]
// dll="BattleLogger.dll"
// patch="BattleLogger_Init"
extern "C" __declspec(dllexport) void BattleLogger_Init() {
    OutputDebugStringA("[BattleLogger] BattleLogger_Init called by patcher_x86\n");
    HANDLE f = CreateFileA("Z:\\Games2\\HoM&M III by LC\\Logs\\BattleLogger_Init.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        const char* msg = "BattleLogger_Init called\n";
        DWORD w; WriteFile(f, msg, (DWORD)strlen(msg), &w, nullptr);
        CloseHandle(f);
    }
    // Patcher уже проинициализирован, можно ставить хуки повторно если нужно
}

// Для ручного теста без игры: rundll32 BattleLogger.dll,BattleLogger_Init
