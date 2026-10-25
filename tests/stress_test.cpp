// Randomised stress test: hundreds of bot-vs-bot matches with every card, effect, Trap and Graveyard
// effect live and event recording switched on. After every turn it checks the rules' invariants:
//   * LP never above 20, Shield never negative, Insight never negative
//   * at most 5 monsters / 3 Set Traps per side, every monster on the field has HP > 0
//   * the 80 cards of the two decks are always somewhere (nothing created or lost by an effect)
//   * the last recorded event's snapshot matches the real table (so animations never end on a wrong picture)
// Build it with  -fsanitize=address,undefined  for a memory-safety run (see README).
// Usage: stress_test [data_dir] [matches]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "battle_engine.hpp"
#include "bot_ai.hpp"
#include "card_engine.hpp"

using namespace fd;
namespace be = fd::battle;

static int g_bad = 0;
static int g_match = 0;

static void expect(bool ok, const char* what, const be::DuelState& st) {
    if (ok) return;
    if (++g_bad <= 25) std::printf("INVARIANT BROKEN: %s (match %d, turn %d)\n", what, g_match, st.turn_number);
}
static int total_cards(const be::DuelState& st) {
    int n = 0;
    for (Side s : {Side::Player, Side::Bot})
        n += static_cast<int>(st.deck[s].size() + st.hand[s].size() + st.field[s].size() + st.traps[s].size() +
                              st.graveyard[s].size() + st.banished[s].size());
    return n;
}
static void check(const be::DuelState& st) {
    for (Side s : {Side::Player, Side::Bot}) {
        expect(st.lp[s] <= be::STARTING_LP, "LP above 20", st);
        expect(st.shield[s] >= 0 && st.insight[s] >= 0, "negative Shield or Insight", st);
        expect(static_cast<int>(st.field[s].size()) <= be::FIELD_LIMIT, "more than 5 monsters", st);
        expect(static_cast<int>(st.traps[s].size()) <= be::TRAP_ZONE_LIMIT, "more than 3 Set Traps", st);
        for (const auto& c : st.field[s]) expect(c.hp > 0 && c.atk >= 0, "monster with HP <= 0 left on the field", st);
    }
    expect(total_cards(st) == 80, "card count changed", st);
    expect(st.chain_depth == 0, "trigger chain left open", st);
    expect(!st.game_over || st.winner != be::Winner::None, "game over without a winner", st);
    if (!st.events.empty()) {
        const be::Board& a = st.events.back().after;
        const bool same = a.lp.player == st.lp.player && a.lp.bot == st.lp.bot && a.shield.player == st.shield.player &&
                          a.shield.bot == st.shield.bot && a.insight.player == st.insight.player && a.insight.bot == st.insight.bot &&
                          a.hand.player.size() == st.hand.player.size() && a.hand.bot.size() == st.hand.bot.size() &&
                          a.field.player.size() == st.field.player.size() && a.field.bot.size() == st.field.bot.size() &&
                          a.traps.player.size() == st.traps.player.size() && a.traps.bot.size() == st.traps.bot.size() &&
                          a.graveyard.player.size() == st.graveyard.player.size() && a.graveyard.bot.size() == st.graveyard.bot.size() &&
                          a.banished.player.size() == st.banished.player.size() && a.banished.bot.size() == st.banished.bot.size() &&
                          a.deck_count.player == static_cast<int>(st.deck.player.size()) && a.deck_count.bot == static_cast<int>(st.deck.bot.size());
        expect(same, "last event snapshot differs from the real table", st);
    }
}

int main(int argc, char** argv) {
    const std::string data = argc > 1 ? argv[1] : "data";
    const int matches = argc > 2 ? std::atoi(argv[2]) : 600;
    CardEngine engine(data);
    const bot_ai::Difficulty levels[3] = {bot_ai::Difficulty::Easy, bot_ai::Difficulty::Normal, bot_ai::Difficulty::Hard};
    long turns = 0, events = 0;
    int wins_p = 0, wins_b = 0, draws = 0;
    for (g_match = 0; g_match < matches; ++g_match) {
        seed_rng(static_cast<std::uint64_t>(70000 + g_match));
        const Side first = bot_ai::choose_starting_side();
        auto st = be::start_duel(bot_ai::build_bot_deck(engine), bot_ai::build_bot_deck(engine), engine, first);
        st.trap_resolver = bot_ai::bot_trap_decision;
        st.record = true;
        be::mulligan(st, Side::Player, bot_ai::bot_mulligan_indices(st.hand.player));
        be::mulligan(st, Side::Bot, bot_ai::bot_mulligan_indices(st.hand.bot));
        be::begin_match(st, first);
        for (int i = 0; i < 300 && !st.game_over; ++i) {
            const Side who = st.active_side;
            bot_ai::take_turn(st, who, levels[(g_match + (who == Side::Player ? 0 : 1) + i / 7) % 3]);
            ++turns;
            events += static_cast<long>(st.events.size());
            check(st);
            st.events.clear();
        }
        expect(st.game_over, "match did not finish", st);
        if (st.winner == be::Winner::Player) ++wins_p; else if (st.winner == be::Winner::Bot) ++wins_b; else ++draws;
    }
    std::printf("stress_test: %d matches, %ld turns, %ld recorded events, results {first bot: %d, second bot: %d, draw: %d}\n",
                matches, turns, events, wins_p, wins_b, draws);
    std::printf("%s (%d invariant violations)\n", g_bad ? "STRESS TEST FAILED." : "STRESS TEST PASSED.", g_bad);
    return g_bad ? 1 : 0;
}
