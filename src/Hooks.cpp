#include "../include/BattleLogger.h"
#include "patcher_x86.hpp"
#include <windows.h>
#include "h3api/H3Managers/H3CombatManager.hpp"

// Адреса для SoD 3.2 Complete (HD-mode не сдвигает Combat)
// Сверено с H3API + sodSP. Если игра HotA - оффсеты те же для базовой логики.
namespace Addrs {
// BattleMgr @ 0x699420 в H3API, но хуки ставим на функции:
constexpr _ptr_ BattleInit   = 0x473950; // BattleMgr::Init - старт боя
constexpr _ptr_ BattleEnd    = 0x475F90; // BattleMgr::Finish
constexpr _ptr_ BattleRound  = 0x4E0B90; // начало раунда
constexpr _ptr_ CombatAttack = 0x4438D0; // melee attack
constexpr _ptr_ CombatShoot  = 0x443A00; // shoot
constexpr _ptr_ DamageCalc   = 0x5A3A10; // calc damage
}

namespace Hooks {

static PatcherInstance* g_PI = nullptr;
static HANDLE g_PollThread = nullptr;
static volatile bool g_Running = false;

// Адрес CombatManager в SoD/HD (H3API: 0x699420)
static constexpr uintptr_t COMBAT_MGR_PTR = 0x699420;
static constexpr uintptr_t COMBAT_MGR_PTR_HD = 0x699420; // тот же

static DWORD WINAPI PollThread(LPVOID) {
    bool wasInBattle = false;
    bool needZero = false;
    int lastTurn = -1;
    void* lastActive = nullptr;
    int prevAlive[2][21] = {};
    bool prevInit = false;
    HANDLE mf = CreateFileA("Z:\\Games2\\HoM&M III by LC\\Logs\\PollThread.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (mf != INVALID_HANDLE_VALUE) { const char* m="PollThread started\n"; DWORD w; WriteFile(mf,m,(DWORD)strlen(m),&w,nullptr); CloseHandle(mf); }
    while (g_Running) {
        h3::H3CombatManager* mgr = nullptr;
        if (!IsBadReadPtr((void*)COMBAT_MGR_PTR, 4)) mgr = *(h3::H3CombatManager**)COMBAT_MGR_PTR;
        bool inBattle = (mgr != nullptr && !IsBadReadPtr(mgr, sizeof(h3::H3CombatManager)));
        if (needZero && !inBattle) needZero=false;
        if (inBattle && !wasInBattle && !needZero) {
            BattleLogger::Instance().OpenNewBattle("poll");
            BattleLogger::Instance().LogBattleStart(-1,-1);
            Sleep(500);
            if (!IsBadReadPtr(mgr, sizeof(h3::H3CombatManager))) {
                for (int side=0; side<2; ++side) for (int i=0;i<21;++i) {
                    auto &st = mgr->stacks[side][i];
                    if (IsBadReadPtr(&st, sizeof(st))) continue;
                    if (st.type >=0 && st.type < 200 && st.numberAlive>0 && st.numberAlive<100000) {
                        BattleEvent ev{}; ev.type="stack"; ev.attacker="side"+std::to_string(side)+" slot"+std::to_string(i);
                        ev.extra="type="+std::to_string(st.type)+" count="+std::to_string(st.numberAlive)+" pos="+std::to_string(st.position);
                        ev.tick=GetTickCount();
                        BattleLogger::Instance().Log(ev);
                    }
                    prevAlive[side][i]=st.numberAlive;
                }
                prevInit=true;
            }
            lastTurn = mgr ? mgr->turn : -1;
            lastActive = mgr ? mgr->activeStack : nullptr;
            wasInBattle = true;
        } else if (inBattle && wasInBattle) {
            if (IsBadReadPtr(mgr, sizeof(h3::H3CombatManager))) { Sleep(50); continue; }
            if (mgr->turn != lastTurn) { BattleLogger::Instance().LogRound(mgr->turn); lastTurn=mgr->turn; }
            if (mgr->activeStack != lastActive && mgr->activeStack) {
                auto* st = (h3::H3CombatCreature*)mgr->activeStack;
                if (!IsBadReadPtr(st, sizeof(h3::H3CombatCreature))) {
                    BattleEvent ev{}; ev.type="active"; ev.attacker="type "+std::to_string(st->type)+" x"+std::to_string(st->numberAlive);
                    ev.extra="side "+std::to_string(mgr->currentActiveSide)+" pos "+std::to_string(st->position);
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                }
                lastActive=mgr->activeStack;
            }
            if (prevInit) {
                for(int s=0;s<2;++s) for(int i=0;i<21;++i) {
                    int cur = mgr->stacks[s][i].numberAlive;
                    if (cur != prevAlive[s][i] && !IsBadReadPtr(&mgr->stacks[s][i], sizeof(h3::H3CombatCreature))) {
                        int killed = prevAlive[s][i] - cur;
                        if (killed>0) {
                            BattleEvent ev{}; ev.type="killed";
                            ev.attacker="side"+std::to_string(s)+" slot"+std::to_string(i)+" type"+std::to_string(mgr->stacks[s][i].type);
                            ev.killed=killed; ev.extra="alive "+std::to_string(cur);
                            ev.tick=GetTickCount();
                            BattleLogger::Instance().Log(ev);
                        }
                        prevAlive[s][i]=cur;
                    }
                }
            }
            if (mgr->finished) {
                BattleLogger::Instance().CloseBattle("finished");
                wasInBattle=false; needZero=true; lastTurn=-1; lastActive=nullptr; prevInit=false;
                continue;
            }
        } else if (!inBattle && wasInBattle) {
            BattleLogger::Instance().CloseBattle("poll_end");
            wasInBattle=false; lastTurn=-1; lastActive=nullptr; prevInit=false;
        }
        Sleep(50);
    }
    return 0;
}

// LoHook хук: вызывается перед оригинальным кодом
int __stdcall OnBattleStart(LoHook* h, HookContext* c) {
    HANDLE mf = CreateFileA("Z:\\Games2\\HoM&M III by LC\\Logs\\Hook_BattleStart_Lo.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (mf != INVALID_HANDLE_VALUE) { const char* m="OnBattleStart Lo\n"; DWORD w; WriteFile(mf,m,(DWORD)strlen(m),&w,nullptr); CloseHandle(mf); }
    BattleLogger::Instance().OpenNewBattle("hook_test_lo");
    BattleLogger::Instance().LogBattleStart(0, 0);
    return EXEC_DEFAULT;
}

// HiHook для BattleInit (thiscall) - пробуем как альтернативу LoHook
int __stdcall OnBattleStartHi(HiHook* h, void* battleMgr) {
    HANDLE mf = CreateFileA("Z:\\Games2\\HoM&M III by LC\\Logs\\Hook_BattleStart_Hi.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (mf != INVALID_HANDLE_VALUE) { const char* m="OnBattleStart Hi\n"; DWORD w; WriteFile(mf,m,(DWORD)strlen(m),&w,nullptr); CloseHandle(mf); }
    BattleLogger::Instance().OpenNewBattle("hook_test_hi");
    // вызываем оригинал
    auto orig = (int (__thiscall*)(void*))h->GetDefaultFunc();
    int ret = orig(battleMgr);
    return ret;
}

int __stdcall OnBattleEnd(LoHook* h, HookContext* c) {
    BattleLogger::Instance().CloseBattle("battle_end");
    return EXEC_DEFAULT;
}

int __stdcall OnRound(LoHook* h, HookContext* c) {
    // c->esi обычно содержит номер раунда, но логируем как инкремент
    static int round = 0;
    round++;
    BattleLogger::Instance().LogRound(round);
    return EXEC_DEFAULT;
}

int __stdcall OnAttack(LoHook* h, HookContext* c) {
    BattleEvent ev{};
    ev.type = "attack";
    ev.attacker = "unknown";
    ev.extra = "hook 0x4438D0";
    ev.tick = GetTickCount();
    BattleLogger::Instance().Log(ev);
    return EXEC_DEFAULT;
}

int __stdcall OnDamageCalc(LoHook* h, HookContext* c) {
    // 0x5A3A10 - вызывается только при реальной атаке, не на Wait
    BattleEvent ev{};
    ev.type = "damage_calc";
    ev.tick = GetTickCount();
    // Пытаемся достать damage из стека: c->esi/c->edi часто содержат attacker/defender
    // Для MVP логируем факт вызова
    ev.extra = "0x5A3A10";
    BattleLogger::Instance().Log(ev);
    return EXEC_DEFAULT;
}

void Install() {
    // Пробуем получить Patcher (требует что patcher_x86.dll уже загружен HD)
    Patcher* patcher = GetPatcher();
    if (!patcher) {
        OutputDebugStringA("[BattleLogger] GetPatcher() == nullptr, hooks not installed (standalone test)\n");
        // Фолбэк для теста без игры - просто открываем лог чтобы проверить ядро
        BattleLogger::Instance().OpenNewBattle("standalone_test");
        return;
    }
    g_PI = patcher->CreateInstance("BattleLogger");
    if (!g_PI) {
        // Уже создан - берем существующий
        g_PI = patcher->GetInstance("BattleLogger");
        if (!g_PI) return;
    }

    // Ставим хуки
    g_PI->WriteLoHook(Addrs::BattleInit, OnBattleStart);
    g_PI->WriteHiHook(Addrs::BattleInit, SPLICE_, EXTENDED_, THISCALL_, (void*)OnBattleStartHi);
    // g_PI->WriteLoHook(Addrs::BattleEnd, OnBattleEnd); // крэш 0x475F90
    // g_PI->WriteLoHook(Addrs::CombatAttack, OnAttack); // крэш 0x4438D4 на wait
    g_PI->WriteLoHook(Addrs::DamageCalc, OnDamageCalc); // безопасно, только на урон

    // Polling fallback для HD (если хуки не триггерят)
    g_Running = true;
    g_PollThread = CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);

    OutputDebugStringA("[BattleLogger] Hooks installed + poll thread\n");
}

void Uninstall() {
    g_Running = false;
    if (g_PollThread) { WaitForSingleObject(g_PollThread, 500); CloseHandle(g_PollThread); g_PollThread=nullptr; }
    BattleLogger::Instance().CloseBattle("DLL unload");
}

} // namespace Hooks
