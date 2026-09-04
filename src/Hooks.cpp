#include "../include/BattleLogger.h"
#include "../include/Names.h"
#include "patcher_x86.hpp"
#include <windows.h>
#include "h3api/H3Managers/H3CombatManager.hpp"
#include "h3api/H3Heroes/H3Hero.hpp"

// Адреса для SoD 3.2 Complete (HD-mode не сдвигает Combat)
// Сверено с H3API + sodSP. Если игра HotA - оффсеты те же для базовой логики.
namespace Addrs {
// BattleMgr @ 0x699420 в H3API, но хуки ставим на функции:
constexpr _ptr_ ReportDamage = 0x469670; // CombatManager::ReportDamageDone (H3API)
constexpr _ptr_ CastSpell    = 0x5A0140; // CombatManager::CastSpell (H3API)
}

namespace Hooks {

static PatcherInstance* g_PI = nullptr;
static HANDLE g_PollThread = nullptr;
static volatile bool g_Running = false;

// Адрес CombatManager в SoD/HD (H3API: 0x699420)
static constexpr uintptr_t COMBAT_MGR_PTR = 0x699420;

// CP1251 -> UTF-8 с авто-детектом: строки игры в CP1251, но имена героев
// под HD уже UTF-8. Валидный UTF-8 проходит как есть, остальное конвертится.
static std::string ToUtf8(const char* s) {
    if (!s) return "";
    // валидный UTF-8? (MB_ERR_INVALID_CHARS отсеет CP1251-кириллицу)
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0) > 0)
        return std::string(s);
    int wlen = MultiByteToWideChar(1251, 0, s, -1, nullptr, 0);
    if (wlen <= 0) return std::string(s);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(1251, 0, s, -1, &w[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return std::string(s);
    std::string u(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &u[0], ulen, nullptr, nullptr);
    if (!u.empty() && u.back() == '\0') u.pop_back();
    return u;
}

// HD хранит имена героев с тегами цвета: "{~o}Имя}" -> "Имя"
static std::string StripNameTags(std::string s) {
    size_t pos;
    while ((pos = s.find("{~")) != std::string::npos) {
        size_t end = s.find('}', pos);
        if (end == std::string::npos) { s.erase(pos); break; }
        s.erase(pos, end - pos + 1);
    }
    s.erase(std::remove(s.begin(), s.end(), '{'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '}'), s.end());
    return s;
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
    bool prevFinished = false;
    bool autoCombatPrev = false;
    int lastTurn = -1;
    void* lastActive = nullptr;
    int prevAlive[2][21] = {};
    int prevHpLost[2][21] = {};
    bool prevInit = false;
    bool snapshotDone = false;
    int lastLoggedAction = -1;
    int lastRng = 0;
    // стартовое состояние героев для дельты на battle_end
    struct HeroSnap { int level; int exp; int mana; bool valid; };
    HeroSnap heroSnap[2] = {{0,0,0,false},{0,0,0,false}};
    int wallsAlivePrev[18]; bool wallsInit = false;
    for (int i=0;i<18;++i) wallsAlivePrev[i] = -1;
    while (g_Running) {
        h3::H3CombatManager* mgr = nullptr;
        if (!IsBadReadPtr((void*)COMBAT_MGR_PTR, 4)) mgr = *(h3::H3CombatManager**)COMBAT_MGR_PTR;
        bool inBattle = (mgr != nullptr && !IsBadReadPtr(mgr, sizeof(h3::H3CombatManager)));
        if (inBattle && !wasInBattle) {
            // после finished менеджер жив с finished=true - ждем сброса (replay) или нового боя
            if (prevFinished && mgr->finished) { Sleep(20); continue; }
            BattleLogger::Instance().OpenNewBattle("poll");
            BattleLogger::Instance().LogBattleStart(-1,-1);
            lastTurn = mgr->turn;
            lastActive = mgr->activeStack;
            wasInBattle = true;
            prevFinished = false;
            autoCombatPrev = false;
            snapshotDone = false;
            lastLoggedAction = -1;
            wallsInit = false;
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
                // снапшот героев: имя, статы, мана, стартовая армия
                for (int side=0; side<2; ++side) {
                    auto* hero = mgr->hero[side];
                    if (!hero || IsBadReadPtr(hero, sizeof(h3::H3Hero))) continue;
                    BattleEvent ev{}; ev.type="hero";
                    ev.attacker = StripNameTags(ToUtf8(hero->name));
                    ev.extra = "side="+std::to_string(side)
                        +" id="+std::to_string(hero->id)
                        +" level="+std::to_string(hero->level)
                        +" atk="+std::to_string(hero->primarySkill[0])
                        +" def="+std::to_string(hero->primarySkill[1])
                        +" power="+std::to_string(hero->primarySkill[2])
                        +" knowledge="+std::to_string(hero->primarySkill[3])
                        +" mana="+std::to_string(hero->spellPoints);
                    // стартовый состав армии героя (до боя)
                    std::string army;
                    for (int i=0;i<7;++i) {
                        if (hero->army.type[i] > 0 && hero->army.count[i] > 0) {
                            army += (army.empty() ? "" : ", ") + std::string(names::Creature(hero->army.type[i])) + " x" + std::to_string(hero->army.count[i]);
                        }
                    }
                    ev.extra += " army=[" + army + "]";
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                    heroSnap[side] = { hero->level, hero->experience, hero->spellPoints, true };
                }
                // осада: тир обороны по числу заряженных башен, тип из менеджера и ров
                if (mgr->siegeKind >= 0) {
                    int towersLoaded = 0;
                    for (int i=0;i<3;++i) {
                        if (!IsBadReadPtr(&mgr->towers[i], sizeof(mgr->towers[i])) && mgr->towers[i].monDefLoaded != nullptr)
                            towersLoaded++;
                    }
                    BattleEvent sv{}; sv.type="siege";
                    sv.extra=std::string("siegeKind=")+std::to_string(mgr->siegeKind)
                        +" towersLoaded="+std::to_string(towersLoaded)
                        +" moat="+std::to_string(mgr->hasMoat);
                    sv.tick=GetTickCount();
                    BattleLogger::Instance().Log(sv);
                }
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
            // стены форта (осады): дифф после инициализации армий
            if (!wallsInit) {
                if (!IsBadReadPtr(mgr->fortWallsAlive, sizeof(mgr->fortWallsAlive))) {
                    for (int i=0;i<18;++i) wallsAlivePrev[i] = mgr->fortWallsAlive[i];
                    wallsInit = true;
                }
            } else if (!IsBadReadPtr(mgr->fortWallsAlive, sizeof(mgr->fortWallsAlive))) {
                for (int i=0;i<18;++i) {
                    int alive = mgr->fortWallsAlive[i];
                    if (alive != wallsAlivePrev[i]) {
                        BattleEvent ev{}; ev.type="wall";
                        ev.attacker="section "+std::to_string(i);
                        ev.extra="alive "+std::to_string(wallsAlivePrev[i])+"->"+std::to_string(alive)
                            +" hp="+std::to_string(mgr->fortWallsHp[i]);
                        ev.tick=GetTickCount();
                        BattleLogger::Instance().Log(ev);
                        wallsAlivePrev[i]=alive;
                    }
                }
            }
            // авто-бой (Q / быстрый бой): помечаем в логе, что дальше идет симуляция
            {
                bool ac = (mgr->autoCombat != 0);
                if (ac != autoCombatPrev) {
                    BattleEvent ev{}; ev.type="auto_combat";
                    ev.extra = ac ? "on - rest of battle is simulated" : "off";
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                    autoCombatPrev = ac;
                }
            }
            if (mgr->finished) {
                // дельта героев: опыт, уровень, мана
                for (int side=0; side<2; ++side) {
                    auto* hero = mgr->hero[side];
                    if (!heroSnap[side].valid || !hero || IsBadReadPtr(hero, sizeof(h3::H3Hero))) continue;
                    BattleEvent ev{}; ev.type="hero_delta";
                    ev.attacker = StripNameTags(ToUtf8(hero->name));
                    ev.extra = "side="+std::to_string(side)
                        +" exp "+std::to_string(heroSnap[side].exp)+"->"+std::to_string(hero->experience)
                        +" (+"+std::to_string(hero->experience - heroSnap[side].exp)+")"
                        +" level "+std::to_string(heroSnap[side].level)+"->"+std::to_string(hero->level)
                        +" mana "+std::to_string(heroSnap[side].mana)+"->"+std::to_string(hero->spellPoints);
                    ev.tick=GetTickCount();
                    BattleLogger::Instance().Log(ev);
                }
                BattleLogger::Instance().CloseBattle("finished");
                wasInBattle=false; prevFinished=true; lastTurn=-1; lastActive=nullptr; prevInit=false; snapshotDone=false; lastLoggedAction=-1;
                heroSnap[0]=heroSnap[1]={0,0,0,false};
                continue;
            }
        } else if (!inBattle && wasInBattle) {
            BattleLogger::Instance().CloseBattle("poll_end");
            wasInBattle=false; lastTurn=-1; lastActive=nullptr; prevInit=false; snapshotDone=false; lastLoggedAction=-1;
            prevFinished=false;
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
    ev.attacker = ToUtf8(attackerName) + " x" + std::to_string(numAttackers);
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

// CastSpell (0x5A0140, THISCALL): все касты в бою, обе стороны.
// (HiHook*, this, spell_id, hex_ix, cast_type_012, hex2_ix, skill_level, spell_power)
void __stdcall OnCastSpell(HiHook* h, void* mgr, int spell_id, int hex_ix, int cast_type, int hex2_ix, int skill_level, int spell_power) {
    auto* m = (h3::H3CombatManager*)mgr;
    BattleEvent ev{};
    ev.type = "spell_cast";
    ev.attacker = std::string(names::Spell(spell_id)) + " side " + std::to_string(m ? m->currentActiveSide : -1);
    ev.extra = "spell_id=" + std::to_string(spell_id)
        + " hex=" + std::to_string(hex_ix)
        + " hex2=" + std::to_string(hex2_ix)
        + " cast_type=" + std::to_string(cast_type)
        + " skill=" + std::to_string(skill_level)
        + " power=" + std::to_string(spell_power);
    ev.tick = GetTickCount();
    BattleLogger::Instance().Log(ev);
    auto orig = (void(__fastcall*)(void*, void*, int, int, int, int, int, int))h->GetDefaultFunc();
    orig(mgr, nullptr, spell_id, hex_ix, cast_type, hex2_ix, skill_level, spell_power);
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
    // CastSpell: все касты в бою, включая вражеского героя
    g_PI->WriteHiHook(Addrs::CastSpell, SPLICE_, EXTENDED_, THISCALL_, (void*)OnCastSpell);
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
