#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>

// Ядро логгера: пишет одновременно txt (человек) и jsonl (машина)
// Потокобезопасно, буферизует и флешит в конце раунда/боя

struct BattleEvent {
    uint32_t tick = 0;          // GetTickCount
    int round = 0;
    int turn = 0;               // 0 = attacker, 1 = defender
    std::string type;           // "battle_start", "attack", "shoot", "cast", "morale", "luck", "death", "battle_end"
    std::string attacker;       // "Pikeman x15"
    std::string defender;
    int damage = -1;
    int killed = -1;
    int luckRoll = -1;          // -1 = нет броска, 0/1
    int moraleRoll = -1;
    std::string extra;          // доп. инфа для txt
};

class BattleLogger {
public:
    static BattleLogger& Instance();

    bool OpenNewBattle(const std::string& mapName = "");
    void CloseBattle(const std::string& result = "");

    void Log(const BattleEvent& ev);

    // Удобные хелперы
    void LogBattleStart(int heroAtkId, int heroDefId);
    void LogAttack(const char* atkName, int atkCount, const char* defName, int defCount, int dmg, int killed, int luck);
    void LogRound(int round);

private:
    BattleLogger() = default;
    std::string EscapeJson(const std::string& s);
    std::string ToJson(const BattleEvent& ev);
    std::string ToHuman(const BattleEvent& ev);

    std::mutex mtx_;
    std::ofstream jsonl_;
    std::ofstream txt_;
    std::string baseName_;
    int battleId_ = 0;
    bool isOpen_ = false;
};
