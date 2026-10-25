#pragma once
// The core Duel Mode game engine. Never reads input or draws anything -- it only mutates a plain
// DuelState and returns results, so the same functions drive the human UI, the bot AI and the
// unit tests (tests/dev_checks.cpp).
//
// Zones per side: deck, hand, field (Monsters, max 5), traps (Set face-down, max 3),
// graveyard (destroyed/used cards), banished (permanently removed cards).
// Traps resolve through the injected `trap_resolver` callback, which keeps this module free
// of any input/output of its own.
//
// When `record` is true every visible change is also appended to `events` together with a
// snapshot of the table after that change. The UI replays those events as animations; the
// engine itself never waits for them.
#include <functional>
#include "card_engine.hpp"
#include "duel_cards.hpp"

namespace fd::battle {

constexpr int STARTING_LP = 20;
constexpr int MAX_INSIGHT = 10;
constexpr int FIELD_LIMIT = 5;
constexpr int TRAP_ZONE_LIMIT = 3;
constexpr int OPENING_HAND = 5;
constexpr int TURN_LIMIT = 30;

enum class Winner { None, Player, Bot, Draw };

// Everything the table draws, copied after each recorded event.
struct Board {
    PerSide<int> lp{STARTING_LP, STARTING_LP}, shield{0, 0}, insight{0, 0}, max_insight{0, 0}, deck_count{0, 0};
    PerSide<std::vector<CardInstance>> hand, field, traps;
    PerSide<std::vector<GraveEntry>> graveyard, banished;
    int turn_number = 1;
    Side active_side = Side::Player;
};

// side = the side the event happens to (owner of the card / who is hurt / who acts).
enum class Ev {
    Draw,        // card drawn from the deck                          (from "deck")
    ToHand,      // card added to a hand: search, bounce, recover      (from "deck" | "field" | "gy"; target_id = old field id)
    Summon,      // monster arrives on the field                      (from "hand" | "gy" | "enemy-gy"; flag = Special Summon)
    SpellCast,   // spell leaves the hand and activates               (text = card text)
    SetTrap,     // trap Set face-down
    TrapFlip,    // trap turns face-up and activates                  (text = card text)
    Ability,     // an effect of `card` activates                     (text = effect text; from = timing label)
    Attack,      // from == "declare": attack announced; "strike": the blow lands   (target_id = defender id or "face")
    Damage,      // monster takes `amount` damage
    FaceDamage,  // `side` loses `amount` LP; text = Shield absorbed  (flag = shield absorbed something)
    ShieldGain, Heal, Insight,   // amount
    Buff, Debuff,                // text = "+2 ATK"
    Freeze, Silence, Reveal,     // status marks
    Destroy,     // card destroyed                                    (from "field" | "trap")
    Banish,      // card banished                                     (from "field" | "gy" | "spell")
    ToGrave,     // card goes to a Graveyard                          (from "spell" | "trap" | "deck" | "hand")
    CoinFlip,    // flag = heads
    Negate,      // text = what was negated
    TurnStart,   // amount = turn number
    GameOver     // flag = player won
};

struct Event {
    Ev type = Ev::Ability;
    Side side = Side::Player;
    CardInstance card;            // the card the event is about (copy)
    std::string target_id;
    std::string text;
    std::string from;
    int amount = 0;
    bool flag = false;
    std::size_t log_size = 0;     // how many log lines existed when it happened (keeps the log in step with animations)
    Board after;                  // the table right after this event
};

struct DuelState;
// Called whenever a Trap could be triggered. Returns the instance_id of the Trap to activate, or "" for none.
using TrapResolver = std::function<std::string(DuelState&, Side defender_side, const std::vector<CardInstance>& eligible)>;

struct DuelState {
    PerSide<int> lp{STARTING_LP, STARTING_LP};
    PerSide<int> shield{0, 0}, insight{0, 0}, max_insight{0, 0};
    PerSide<std::vector<DeckEntry>> deck;
    PerSide<std::vector<CardInstance>> hand, field, traps;
    PerSide<std::vector<GraveEntry>> graveyard, banished;
    PerSide<bool> traps_blocked{false, false};      // "block_traps": that side's Traps are off until the turn ends
    int turn_number = 1;
    Side active_side = Side::Player;
    std::vector<std::string> log;
    bool game_over = false;
    Winner winner = Winner::None;
    int next_instance_id = 1;
    bool very_first_turn_done = false;
    int chain_depth = 0;                            // guards against endless trigger chains
    bool record = false;                            // the UI turns this on to receive `events`
    std::vector<Event> events;
    TrapResolver trap_resolver;
    const CardEngine* card_engine = nullptr;
};

struct Action { bool ok; std::string reason; };

DuelState new_state();
Board snapshot(const DuelState& st);
// Both decks must contain exactly 40 cards. Draws opening hands but does NOT begin turn 1 --
// call begin_match() after any mulligan decisions.
DuelState start_duel(const std::vector<DeckEntry>& player_deck, const std::vector<DeckEntry>& bot_deck,
                     const CardEngine& engine, Side starting_side = Side::Player);
void begin_match(DuelState& st, Side starting_side);
void mulligan(DuelState& st, Side side, std::vector<int> hand_indices_to_return);

void draw_card(DuelState& st, Side side);
void begin_turn(DuelState& st, Side side);
void end_turn(DuelState& st);

Action can_play(const DuelState& st, Side side, int hand_index);
Action play_card(DuelState& st, Side side, int hand_index);   // plays a Monster/Spell or Sets a Trap
Action can_attack(const DuelState& st, Side side, const std::string& attacker_instance_id);
// target: "face" or an enemy monster's instance_id
Action attack(DuelState& st, Side side, const std::string& attacker_instance_id, const std::string& target);

// Graveyard effects: banish a card from your own Graveyard and pay its Insight cost to use its effect.
Action can_activate_grave(const DuelState& st, Side side, int grave_index);
Action activate_grave(DuelState& st, Side side, int grave_index);
int grave_effect_count(const DuelState& st, Side side);     // Graveyard cards that carry an effect at all
int usable_grave_count(const DuelState& st, Side side);     // ...of which the side can use right now

void apply_face_damage(DuelState& st, Side side, int amount);
void check_win(DuelState& st);

CardInstance* find_in_field(DuelState& st, Side side, const std::string& instance_id);
const CardInstance* find_in_field(const DuelState& st, Side side, const std::string& instance_id);

}  // namespace fd::battle
