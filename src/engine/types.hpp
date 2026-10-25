#pragma once
// Shared data types for the Faust's Deck engine (C++ port of the Python modules).
// Nothing in src/engine depends on raylib, so the whole rule set can be
// unit-tested headlessly (see tests/dev_checks.cpp).
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fd {

enum class Side { Player = 0, Bot = 1 };
inline Side other(Side s) { return s == Side::Player ? Side::Bot : Side::Player; }
inline const char* side_key(Side s) { return s == Side::Player ? "player" : "bot"; }   // lower-case, as used in log text
inline std::string side_cap(Side s) { return s == Side::Player ? "Player" : "Bot"; }   // Python's side.capitalize()

// One value per side, indexable by Side (state.lp[Side::Player]).
template <class T>
struct PerSide {
    T player{}, bot{};
    T& operator[](Side s) { return s == Side::Player ? player : bot; }
    const T& operator[](Side s) const { return s == Side::Player ? player : bot; }
};

enum class CardType { Monster, Spell, Trap };
enum class Trigger { None, OnAttack, OnSummon, OnSpell };      // when a Set Trap may be activated
// When an ability happens. Grave abilities are used from the Graveyard on your own turn.
enum class Timing { Summon, Spell, Trap, Death, Mill, Attack, TurnStart, TurnEnd, Grave };

// Every card effect is a list of these building blocks (see "abilities" in data/cards.json).
enum class OpKind {
    DmgFace, DmgSelf, DmgRandom, DmgAll, DmgPerMonster, DmgPerBanished,
    DestroyStrongest, BanishStrongest, BounceStrongest, FreezeStrongest, SilenceStrongest,
    DestroyTraps, RevealTraps, BlockTraps, BanishGy, DiscardEnemy,
    Shield, Heal, ShieldPerMonster, Draw, DrawIfBehind, Insight, PayLp,
    SearchMonster, SearchSpell, SearchTrap, MillSelf, ReviveStrongest, ReviveCostLe, StealGy,
    BuffAlliesAtk, BuffAlliesHp, BuffSelfAtk, Indulge, SacForTeam, ShareDraw,
    Coin, OneOf, ReturnSelf,
    NegateAttack, ShieldFromAttacker, LeechAttacker, TBounce, TDestroy, TWeaken, TNegateEffect, TDmgSummoner, NegateSpell
};
constexpr int kOpCount = 48;

struct Lv3 {                                   // a number that grows with the card's level (Lv 1 / 2 / 3)
    int v[3] = {0, 0, 0};
    int at(int level) const { return v[std::max(1, std::min(3, level)) - 1]; }
};
struct AbilityOp {
    OpKind kind = OpKind::DmgFace;
    Lv3 n, m, fb;                              // n = main amount, m = second amount / cost limit, fb = fallback face damage
    int k = 0;
    std::vector<AbilityOp> a, b;               // nested ops (Coin: heads = a, tails = b; OneOf: the options = a)
};
struct Ability { Timing on = Timing::Summon; int grave_cost = 0; std::vector<AbilityOp> ops; };

struct School {
    std::string id, name, effect_type, core_belief, description;
};

struct Card {                       // one entry of data/cards.json
    std::string id, school_id, scenario;
    std::array<std::string, 4> choices;   // A, B, C, D
    char correct = 'A';
    std::string explanation;
    int difficulty = 1;
    std::string collectible, tag;         // tag = short effect label shown on the card face
    CardType card_type = CardType::Monster;
    int cost = 0, atk = 0, hp = 0;
    bool rush = false;                    // may attack the turn it is summoned
    Trigger trigger = Trigger::None;      // only meaningful for Traps
    std::vector<Ability> abilities;
};

struct DeckEntry { std::string card_id; int level = 1; };   // (card_id, level) tuple in Python

struct CardInstance {               // a battle-ready card
    std::string instance_id, card_id, name, school_id;
    CardType card_type = CardType::Monster;
    Trigger trigger = Trigger::None;
    int level = 1, cost = 0, base_atk = 0, atk = 0, max_hp = 0, hp = 0, pending_penalty = 0;
    bool can_attack_this_turn = false, revived = false, face_down = false;
    bool frozen = false, silenced = false, revealed = false;
};

struct GraveEntry {                 // compact record kept in the Graveyard / banished zone
    std::string card_id, name, school_id;
    CardType card_type = CardType::Monster;
    int level = 1, atk = 0, hp = 0;
    bool revived = false;
};

struct Deck { std::string name; std::vector<std::string> cards; };

struct Profile {
    std::string student_id, name, class_name, date_joined;
    std::map<std::string, int> wisdom;               // school_id -> 0..100
    std::vector<std::string> unlocked_schools;
    int lifetime_points = 0;
    std::map<std::string, int> collection;           // card_id -> level (1..3)
    std::vector<Deck> decks;                         // insertion-ordered, like a Python dict
    std::string active_deck;                         // empty == None
};

struct SessionRec { std::string session_id, student_id, datetime; int total_score = 0, rounds_played = 0; };
struct ResponseRec {
    std::string log_id, session_id, card_id, school_id, student_choice;
    bool is_correct = false;
    double time_taken_sec = 0.0;
    std::string timestamp;
};
struct LogData { std::vector<SessionRec> sessions; std::vector<ResponseRec> responses; };

// ---------------------------------------------------------------- RNG helpers
inline std::mt19937_64& rng() {
    static std::mt19937_64 engine{
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        (static_cast<std::uint64_t>(std::random_device{}()) << 32)};
    return engine;
}
inline void seed_rng(std::uint64_t s) { rng().seed(s); }
inline int rand_int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng()); }          // inclusive
inline double rand_unit() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
inline std::size_t rand_index(std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng()); }
template <class T> void shuffle_vec(std::vector<T>& v) { std::shuffle(v.begin(), v.end(), rng()); }

// -------------------------------------------------------------- string helpers
inline std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
inline std::string to_upper(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}
inline std::string now_formatted(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tmv);
    return buf;
}

}  // namespace fd
