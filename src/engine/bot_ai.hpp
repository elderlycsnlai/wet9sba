#pragma once
// Builds a Duel opponent deck and drives the bot's turns (and its Mulligan and Trap decisions)
// at one of three difficulty levels. Difficulty is ONLY about decision quality: every difficulty
// uses the same card level and the same fair coin-flip for who goes first.
#include "battle_engine.hpp"

namespace fd::bot_ai {

constexpr int BOT_CARD_LEVEL = 2;
constexpr int DECK_UNIQUE_CARDS = 20;
constexpr int COPIES_PER_CARD = 2;

enum class Difficulty { Easy, Normal, Hard };
const char* difficulty_name(Difficulty d);   // "Easy" / "Normal" / "Hard"

Side choose_starting_side();
std::vector<DeckEntry> build_bot_deck(const CardEngine& engine);
std::vector<int> bot_mulligan_indices(const std::vector<CardInstance>& hand, int max_cost_keep = 5);
// Greedy: always activate the first eligible Set trap -- a dormant Trap has no value.
std::string bot_trap_decision(battle::DuelState& st, Side side, const std::vector<CardInstance>& eligible);

void take_turn(battle::DuelState& st, Side side, Difficulty d);
inline void take_bot_turn(battle::DuelState& st, Difficulty d) { take_turn(st, Side::Bot, d); }

}  // namespace fd::bot_ai
