#include "../include/BattleLogger.h"
#include "../include/Names.h"
#include "patcher_x86.hpp"
#include <windows.h>
#include "h3api/H3Managers/H3CombatManager.hpp"

// Адреса для SoD 3.2 Complete (HD-mode не сдвигает Combat)
// Сверено с H3API + sodSP. Если игра HotA - оффсеты те же для базовой логики.
namespace Addrs {
// BattleMgr @ 0x699420 в H3API, но хуки ставим на функции:
constexpr _ptr_ ReportDamage = 0x469670; // CombatManager::ReportDamageDone (H3API)
}

namespace Hooks {

static PatcherInstance* g_PI = nullptr;
static HANDLE g_PollThread = nullptr;
static volatile bool g_Running = false;

// Адрес CombatManager в SoD/HD (H3API: 0x699420)
static constexpr uintptr_t COMBAT_MGR_PTR = 0x699420;

// CP1251 -> UTF-8: имена атакующих из ReportDamageDone в кодировке игры
static std::string Cp1251ToUtf8(const char* s) {
    if (!s) return "";
    std::string out;
    out.reserve(strlen(s) * 2);
    for (const unsigned char c : std::string(s)) {
        unsigned int cp = 0xFFFD;
        if (c < 0x80) { out += (char)c; continue; }
        else if (c == 0xA8) cp = 0x0401;      // Ё
        else if (c == 0xB8) cp = 0x0451;      // ё
        else if (c == 0xA9) cp = 0x00A9;      // (c)
        else if (c == 0xAE) cp = 0x00AE;      // (r)
        else if (c == 0xAB) cp = 0x00AB;      // <<
        else if (c == 0xBB) cp = 0x00BB;      // >>
        else if (c == 0xB9) cp = 0x2116;      // №
        else if (c == 0x85) cp = 0x2026;      // ...
        else if (c == 0x96) cp = 0x2013;
        else if (c == 0x97) cp = 0x2014;
        else if (c == 0x91) cp = 0x2018;
        else if (c == 0x92) cp = 0x2019;
        else if (c == 0x93) cp = 0x201C;
        else if (c == 0x94) cp = 0x201D;
        else if (c >= 0xC0) cp = 0x0410 + (c - 0xC0); // А-Я а-я
        if (cp < 0x80) { out += (char)cp; }
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

static const char* ActionName(int a) {
    switch (a) {
        case 0: return "cancel";
        case 1: return "cast_spell";
        case 2: return "walk";
        case 3: return "defend";
        case 4: return "retreat";
        case 5: return "surrender";
        case 6: return "walk_attack";
        case 7: return "shoot";
        case 8: return "wait";
        case 9: return "catapult";
        case 10: return "monster_spell";
        case 11: return "first_aid_tent";
        case 12: return "nothing";
        default: return "unknown";
    }
}

static DWORD WINAPI PollThread(LPVOID) {
    bool wasInBattle = false;
    bool needZero = false;
    int lastTurn = -1;
    void* lastActive = nullptr;
    int prevAlive[2][21] = {};
    int prevHpLost[2][21] = {};
    bool prevInit = false;
    bool snapshotDone = false;
    int lastLoggedAction = -1;
    int lastRng = 0;
    while (g_Running) {
        h3::H3CombatManager* mgr = nullptr;
        if (!IsBadReadPtr((void*)COMBAT_MGR_PTR, 4)) mgr = *(h3::H3CombatManager**)COMBAT_MGR_PTR;
        bool inBattle = (mgr != nullptr && !IsBadReadPtr(mgr, sizeof(h3::H3CombatManager)));
        if (needZero && !inBattle) needZero=false;
        if (inBattle && !wasInBattle && !needZero) {
            BattleLogger::Instance().OpenNewBattle("poll");
            BattleLogger::Instance().LogBattleStart(-1,-1);
            lastTurn = mgr->turn;
            lastActive = mgr->activeStack;
            wasInBattle = true;
            snapshotDone = false;
            lastLoggedAction = -1;
        } else if (inBattle && wasInBattle) {
            if (IsBadReadPtr(mgr, sizeof(h3::H3CombatManager))) { Sleep(50); continue; }
            // отложенный снапшот армий: ждем первый ход (стеки уже заполнены)
            if (!snapshotDone && mgr->activeStack != nullptr) {
                for (int side=0; side<2; ++side) for (int i=0;i<21;++i) {
                    auto &st = mgr->stacks[side][i];
                    if (IsBadReadPtr(&st, sizeof(st))) continue;
                    if (st.type > 0 && st.numberAlive > 0) {
                        BattleEvent ev{}; ev.type="stack"; ev.attacker=std::string(names::Creature(st.type))+" x"+std::to_string(st.numberAlive)+" side"+std::to_string(side)+" slot"+std::to_string(i);
                        ev.extra="type="+std::to_string(st.type)+" count="+std::to_string(st.numberAlive)+" pos="+std::to_string(st.position)+" luck="+std::to_string(st.luck)+" morale="+std::to_string(st.morale);
                        ev.tick=GetTickCount();
                        BattleLogger::Instance().Log(ev);
                    }
                    prevAlive[side][i]=st.numberAlive;
                    prevHpLost[side][i]=st.healthLost;
                }
                prevInit = true;
                snapshotDone = true;
            }
            if (mgr->turn != lastTurn) {
                // сброс счетчика = конец тактической фазы (turn тикает при расстановке)
                if (mgr->turn < lastTurn) {
                    BattleEvent ev{}; ev.type="tactics_end";
                    ev.extra="turn "+std::to_string(lastTurn)+"->"+std::to_string(mgr->turn);
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                }
                BattleLogger::Instance().LogRound(mgr->turn);
                lastTurn=mgr->turn;
            }
            // действия: ловим любое изменение action на ненулевое (actionUndergoing слишком короткий для 50мс)
            if (mgr->action != lastLoggedAction) {
                if (mgr->action != 0) {
                    auto* atk = (h3::H3CombatCreature*)mgr->activeStack;
                if (atk && !IsBadReadPtr(atk, sizeof(h3::H3CombatCreature))) {
                    BattleEvent ev{}; ev.type="action";
                    ev.attacker=std::string(names::Creature(atk->type))+" x"+std::to_string(atk->numberAlive)+" side "+std::to_string(mgr->currentActiveSide)+" pos "+std::to_string(atk->position);
                    // резолв цели: стек на гексу actionTarget
                    std::string tgt = "hex "+std::to_string(mgr->actionTarget);
                    int th = mgr->actionTarget;
                    if (th >= 0 && th < 187) {
                        for (int s=0;s<2;++s) for (int i=0;i<21;++i) {
                            auto &tst = mgr->stacks[s][i];
                            if (IsBadReadPtr(&tst, sizeof(tst))) continue;
                            if (tst.numberAlive > 0 && tst.position == th) {
                                tgt = std::string(names::Creature(tst.type))+" x"+std::to_string(tst.numberAlive)+" side "+std::to_string(s)+" pos "+std::to_string(th);
                                s=2; break;
                            }
                        }
                    }
                    ev.defender = tgt;
                    ev.extra = "action="+std::to_string(mgr->action)+"("+ActionName(mgr->action)+") param="+std::to_string(mgr->actionParameter);
                    // для кастов - имя заклинания
                    if (mgr->action == 1 && mgr->actionParameter >= 0) {
                        ev.extra += std::string(" spell=") + names::Spell(mgr->actionParameter);
                    }
                    ev.tick = GetTickCount();
                    BattleLogger::Instance().Log(ev);
                }
                }
                lastLoggedAction = mgr->action;
            }
            if (mgr->activeStack != lastActive && mgr->activeStack) {
                auto* st = (h3::H3CombatCreature*)mgr->activeStack;
                if (!IsBadReadPtr(st, sizeof(h3::H3CombatCreature))) {
                    BattleEvent ev{}; ev.type="active"; ev.attacker=std::string(names::Creature(st->type))+" x"+std::to_string(st->numberAlive);
                    ev.extra="side "+std::to_string(mgr->currentActiveSide)+" pos "+std::to_string(st->position)+" luck="+std::to_string(st->luck)+" morale="+std::to_string(st->morale)+" hpLost="+std::to_string(st->healthLost);
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                    // RNG state: логируем только изменения (0x41C3A0 - предварительный адрес)
                    int rng = 0; if (!IsBadReadPtr((void*)0x41C3A0,4)) rng = *(int*)0x41C3A0;
                    if (rng != lastRng) {
                        BattleEvent re{}; re.type="rng"; re.extra="state="+std::to_string(rng); re.tick=ev.tick;
                        BattleLogger::Instance().Log(re);
                        lastRng = rng;
                    }
                }
                lastActive=mgr->activeStack;
            }
            if (prevInit) {
                for(int s=0;s<2;++s) for(int i=0;i<21;++i) {
                    auto &st = mgr->stacks[s][i];
                    if (IsBadReadPtr(&st, sizeof(st))) continue;
                    int cur = st.numberAlive;
                    int curHpLost = st.healthLost;
                    if ((cur != prevAlive[s][i] || curHpLost != prevHpLost[s][i])) {
                        // урон = дельта hpLost + полные существа
                        int hpDelta = curHpLost - prevHpLost[s][i];
                        int killed = prevAlive[s][i] - cur;
                        if (killed > 0) {
                            BattleEvent ev{}; ev.type="killed";
                            ev.attacker=std::string(names::Creature(st.type))+" side"+std::to_string(s)+" slot"+std::to_string(i);
                            ev.killed=killed; ev.extra="alive "+std::to_string(cur);
                            ev.tick=GetTickCount();
                            BattleLogger::Instance().Log(ev);
                        }
                        if (hpDelta != 0) {
                            BattleEvent ev{}; ev.type="damage";
                            ev.defender=std::string(names::Creature(st.type))+" side"+std::to_string(s)+" slot"+std::to_string(i);
                            ev.damage = hpDelta + (killed > 0 ? killed * st.baseHP : 0);
                            ev.killed = killed;
                            ev.extra="hpLost "+std::to_string(curHpLost)+"/"+std::to_string(st.baseHP)+" alive "+std::to_string(cur);
                            ev.tick=GetTickCount();
                            BattleLogger::Instance().Log(ev);
                        }
                        prevAlive[s][i]=cur;
                        prevHpLost[s][i]=curHpLost;
                    }
                }
            }
            if (mgr->finished) {
                BattleLogger::Instance().CloseBattle("finished");
                wasInBattle=false; needZero=true; lastTurn=-1; lastActive=nullptr; prevInit=false; snapshotDone=false; lastLoggedAction=-1;
                continue;
            }
        } else if (!inBattle && wasInBattle) {
            BattleLogger::Instance().CloseBattle("poll_end");
            wasInBattle=false; lastTurn=-1; lastActive=nullptr; prevInit=false; snapshotDone=false; lastLoggedAction=-1;
        }
        Sleep(20);
    }
    return 0;
}

// ReportDamageDone (0x469670, THISCALL): пишет в игровой боевой лог.
// Сюда попадают точные цифры урона, которые игра показывает игроку.
// EXTENDED_ THISCALL_: (HiHook*, this, attackerName, numAttackers, damageDone, target, killedCount)
int __stdcall OnReportDamage(HiHook* h, void* mgr, const char* attackerName, int numAttackers, int damageDone, void* target, int killedCount) {
    BattleEvent ev{};
    ev.type = "report";
    ev.attacker = Cp1251ToUtf8(attackerName) + " x" + std::to_string(numAttackers);
    ev.damage = damageDone;
    ev.killed = killedCount;
    auto* tgt = (h3::H3CombatCreature*)target;
    if (tgt && !IsBadReadPtr(tgt, sizeof(h3::H3CombatCreature))) {
        ev.defender = std::string(names::Creature(tgt->type)) + " x" + std::to_string(tgt->numberAlive)
            + " side " + std::to_string(tgt->side) + " pos " + std::to_string(tgt->position);
    }
    ev.tick = GetTickCount();
    BattleLogger::Instance().Log(ev);
    // вызываем оригинал (thiscall через fastcall-трюк: this -> первый арг, edx -> заглушка)
    auto orig = (int(__fastcall*)(void*, void*, const char*, int, int, void*, int))h->GetDefaultFunc();
    return orig(mgr, nullptr, attackerName, numAttackers, damageDone, target, killedCount);
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
    // ReportDamageDone: точные броски урона из игрового лога
    g_PI->WriteHiHook(Addrs::ReportDamage, SPLICE_, EXTENDED_, THISCALL_, (void*)OnReportDamage);
    // Хуки 0x473950/0x475F90/0x4438D0 в HD_SOD не работают или крэшат - убраны,
    // старт/конец боя ловит PollThread через CombatManager

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
