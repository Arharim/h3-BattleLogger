#include "../include/BattleLogger.h"
#include <windows.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

BattleLogger& BattleLogger::Instance() {
    static BattleLogger inst;
    return inst;
}

static std::string Timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

bool BattleLogger::OpenNewBattle(const std::string& mapName) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (isOpen_) return false;

    // Логи в папке игры рядом с Heroes3 HD.exe (поддерживает оба пути с &)
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path logDir = fs::path(exePath).parent_path() / "Logs";
    // Фолбэк если не удалось
    if (logDir.empty()) {
        logDir = fs::current_path() / "Logs";
    }
    // Для совместимости со старой SOD папкой
    if (!fs::exists(logDir)) {
        fs::path alt = fs::path("Z:/Games2/HoMM 3 SOD/Logs");
        if (fs::exists(alt)) logDir = alt;
    }
    fs::create_directories(logDir);

    baseName_ = "Battle_" + Timestamp() + "_" + std::to_string(++battleId_);
    if (!mapName.empty()) baseName_ += "_" + mapName;

    auto jsonPath = logDir / (baseName_ + ".jsonl");
    auto txtPath  = logDir / (baseName_ + ".txt");

    jsonl_.open(jsonPath, std::ios::out | std::ios::trunc);
    txt_.open(txtPath, std::ios::out | std::ios::trunc);

    isOpen_ = jsonl_.is_open() && txt_.is_open();
    if (isOpen_) {
        txt_ << "=== HoMM3 Battle Log === " << baseName_ << "\n";
        if (!mapName.empty()) txt_ << "Map: " << mapName << "\n";
        txt_.flush();
    }
    return isOpen_;
}

void BattleLogger::CloseBattle(const std::string& result) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!isOpen_) return;
    BattleEvent ev{};
    ev.type = "battle_end";
    ev.extra = result;
    // пишем без рекурсии на мьютекс
    jsonl_ << ToJson(ev) << "\n";
    txt_ << ToHuman(ev) << "\n";
    txt_ << "=== Battle End: " << result << " ===\n";
    jsonl_.close();
    txt_.close();
    isOpen_ = false;
}

void BattleLogger::Log(const BattleEvent& ev) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!isOpen_) return;
    jsonl_ << ToJson(ev) << "\n";
    txt_ << ToHuman(ev) << "\n";
    jsonl_.flush();
    txt_.flush();
}

void BattleLogger::LogBattleStart(int heroAtkId, int heroDefId) {
    BattleEvent ev{};
    ev.type = "battle_start";
    ev.extra = "HeroAtk=" + std::to_string(heroAtkId) + " HeroDef=" + std::to_string(heroDefId);
    Log(ev);
}

void BattleLogger::LogAttack(const char* atkName, int atkCount, const char* defName, int defCount, int dmg, int killed, int luck) {
    BattleEvent ev{};
    ev.type = "attack";
    ev.attacker = std::string(atkName) + " x" + std::to_string(atkCount);
    ev.defender = std::string(defName) + " x" + std::to_string(defCount);
    ev.damage = dmg;
    ev.killed = killed;
    ev.luckRoll = luck;
    Log(ev);
}

void BattleLogger::LogRound(int round) {
    BattleEvent ev{};
    ev.type = "round";
    ev.round = round;
    Log(ev);
}

std::string BattleLogger::EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string BattleLogger::ToJson(const BattleEvent& ev) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"tick\":" << ev.tick << ",";
    oss << "\"round\":" << ev.round << ",";
    oss << "\"type\":\"" << EscapeJson(ev.type) << "\"";
    if (!ev.attacker.empty()) oss << ",\"attacker\":\"" << EscapeJson(ev.attacker) << "\"";
    if (!ev.defender.empty()) oss << ",\"defender\":\"" << EscapeJson(ev.defender) << "\"";
    if (ev.damage >= 0) oss << ",\"damage\":" << ev.damage;
    if (ev.killed >= 0) oss << ",\"killed\":" << ev.killed;
    if (ev.luckRoll >= 0) oss << ",\"luck\":" << ev.luckRoll;
    if (ev.moraleRoll >= 0) oss << ",\"morale\":" << ev.moraleRoll;
    if (!ev.extra.empty()) oss << ",\"extra\":\"" << EscapeJson(ev.extra) << "\"";
    oss << "}";
    return oss.str();
}

std::string BattleLogger::ToHuman(const BattleEvent& ev) {
    std::ostringstream oss;
    if (ev.type == "battle_start") oss << "[START] " << ev.extra;
    else if (ev.type == "battle_end") oss << "[END] " << ev.extra;
    else if (ev.type == "round") oss << "\n-- Round " << ev.round << " --";
    else if (ev.type == "attack") {
        oss << "[ATTACK] " << ev.attacker << " -> " << ev.defender
            << " | dmg=" << ev.damage << " killed=" << ev.killed;
        if (ev.luckRoll >= 0) oss << (ev.luckRoll ? " LUCK!" : " no luck");
    } else {
        oss << "[" << ev.type << "] " << ev.attacker;
        if (!ev.defender.empty()) oss << " -> " << ev.defender;
        if (!ev.extra.empty()) oss << " " << ev.extra;
    }
    return oss.str();
}
