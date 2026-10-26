#include "bot_ai.hpp"

namespace fd::bot_ai {

namespace be = fd::battle;

const char* difficulty_name(Difficulty d) { return d == Difficulty::Easy ? "Easy" : d == Difficulty::Normal ? "Normal" : "Hard"; }

Side choose_starting_side() { return rand_index(2) == 0 ? Side::Player : Side::Bot; }

std::vector<DeckEntry> build_bot_deck(const CardEngine& engine) {
    std::vector<std::string> ids;
    for (const auto& c : engine.cards()) ids.push_back(c.id);
    shuffle_vec(ids);                                  // random.sample(all_ids, 20)
    std::vector<DeckEntry> deck;
    for (int i = 0; i < DECK_UNIQUE_CARDS && i < static_cast<int>(ids.size()); ++i)
        for (int k = 0; k < COPIES_PER_CARD; ++k) deck.push_back({ids[i], BOT_CARD_LEVEL});
    shuffle_vec(deck);
    return deck;
}

std::vector<int> bot_mulligan_indices(const std::vector<CardInstance>& hand, int max_cost_keep) {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i)
        if (hand[i].cost > max_cost_keep) out.push_back(i);
    return out;
}

std::string bot_trap_decision(be::DuelState&, Side, const std::vector<CardInstance>& eligible) {
    return eligible.empty() ? std::string() : eligible.front().instance_id;
}

static std::vector<int> playable_indices(const be::DuelState& st, Side side) {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(st.hand[side].size()); ++i)
        if (be::can_play(st, side, i).ok) out.push_back(i);
    return out;
}
static int card_value(const CardInstance& c) { return c.atk + c.hp; }
static std::vector<std::string> attacker_ids(const be::DuelState& st, Side side, bool only_ready) {
    std::vector<std::string> ids;
    for (const auto& c : st.field[side])
        if (!only_ready || c.can_attack_this_turn) ids.push_back(c.instance_id);
    return ids;
}

// A card that only pays off when there is already a team on the board (buffs, per-monster effects).
static bool needs_board(const be::DuelState& st, Side side, const CardInstance& c) {
    if (!st.field[side].empty()) return false;
    const Card* base = st.card_engine->get_card(c.card_id);
    if (!base) return false;
    for (const auto& ab : base->abilities) {
        if (ab.on != Timing::Summon && ab.on != Timing::Spell) continue;
        for (const auto& op : ab.ops)
            if (op.kind == OpKind::BuffAlliesAtk || op.kind == OpKind::BuffAlliesHp || op.kind == OpKind::SacForTeam ||
                op.kind == OpKind::DmgPerMonster || op.kind == OpKind::ShieldPerMonster) return true;
    }
    return false;
}
// Spends leftover Insight on Graveyard effects (each use banishes the card).
static void use_grave_effects(be::DuelState& st, Side side, double chance) {
    for (int guard = 0; guard < 8 && !st.game_over; ++guard) {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(st.graveyard[side].size()); ++i)
            if (be::can_activate_grave(st, side, i).ok) { pick = i; break; }
        if (pick < 0 || rand_unit() >= chance) return;
        if (!be::activate_grave(st, side, pick).ok) return;
    }
}

// ---------------------------------------------------------------- Low diff
static void play_phase_easy(be::DuelState& st, Side side) {
    for (int i = 0; i < 30; ++i) {
        if (rand_unit() < 0.25) return;
        auto options = playable_indices(st, side);
        if (options.empty()) return;
        int idx = options[rand_index(options.size())];
        if (!be::play_card(st, side, idx).ok) return;
    }
}
static void attack_phase_easy(be::DuelState& st, Side side) {
    const Side opp = other(side);
    for (const auto& id : attacker_ids(st, side, false)) {
        if (rand_unit() < 0.2) continue;
        if (!be::can_attack(st, side, id).ok) continue;
        const auto& enemy = st.field[opp];
        std::string target = "face";
        if (!enemy.empty() && rand_unit() < 0.5) target = enemy[rand_index(enemy.size())].instance_id;
        be::attack(st, side, id, target);
    }
}

// -------------------------------------------------------------- Midd diff
static void play_phase_greedy(be::DuelState& st, Side side) {
    for (int i = 0; i < 30; ++i) {
        auto options = playable_indices(st, side);
        if (options.empty()) return;
        int best = options[0];
        for (int idx : options) if (st.hand[side][idx].cost > st.hand[side][best].cost) best = idx;
        if (!be::play_card(st, side, best).ok) return;
    }
}
static const CardInstance* safe_trade(const CardInstance& attacker, const std::vector<CardInstance>& enemy_field) {
    for (const auto& d : enemy_field) {
        bool kills = attacker.atk >= d.hp;
        bool survives = d.atk < attacker.hp;
        if (kills && survives) return &d;
    }
    return nullptr;
}
static void attack_phase_normal(be::DuelState& st, Side side) {
    const Side opp = other(side);
    for (const auto& id : attacker_ids(st, side, true)) {
        if (!be::can_attack(st, side, id).ok) continue;
        const CardInstance* attacker = be::find_in_field(st, side, id);
        const CardInstance* trade = safe_trade(*attacker, st.field[opp]);
        std::string target = trade ? trade->instance_id : std::string("face");
        be::attack(st, side, id, target);
    }
}

// ---------------------------------------------------------------- Max diff
static void play_phase_hard(be::DuelState& st, Side side) {
    for (int i = 0; i < 30; ++i) {
        auto options = playable_indices(st, side);
        if (options.empty()) return;
        std::vector<int> ready;                       // hold back team-buff cards until there is a team to buff
        for (int idx : options) if (!needs_board(st, side, st.hand[side][idx])) ready.push_back(idx);
        const auto& pool = ready.empty() ? options : ready;
        int best = pool[0];
        for (int idx : pool) if (st.hand[side][idx].cost > st.hand[side][best].cost) best = idx;
        if (!be::play_card(st, side, best).ok) return;
    }
}
static const CardInstance* best_value_trade(const CardInstance& attacker, const std::vector<CardInstance>& enemy_field) {
    const CardInstance* best_defender = nullptr;
    int best_diff = -1;
    for (const auto& d : enemy_field) {
        if (attacker.atk < d.hp) continue;
        bool attacker_dies = d.atk >= attacker.hp;
        int diff = card_value(d) - (attacker_dies ? card_value(attacker) : 0);
        if (diff > best_diff) { best_diff = diff; best_defender = &d; }
    }
    return (best_defender && best_diff >= 0) ? best_defender : nullptr;
}
static void attack_phase_hard(be::DuelState& st, Side side) {
    const Side opp = other(side);
    auto ids = attacker_ids(st, side, true);
    int total_atk = 0;
    for (const auto& id : ids) total_atk += be::find_in_field(st, side, id)->atk;
    if (total_atk >= st.lp[opp] + st.shield[opp]) {          // lethal: everything goes face
        for (const auto& id : ids)
            if (be::can_attack(st, side, id).ok) be::attack(st, side, id, "face");
        return;
    }
    for (const auto& id : ids) {
        if (!be::can_attack(st, side, id).ok) continue;
        const CardInstance* attacker = be::find_in_field(st, side, id);
        const CardInstance* trade = best_value_trade(*attacker, st.field[opp]);
        std::string target = trade ? trade->instance_id : std::string("face");
        be::attack(st, side, id, target);
    }
}

void take_turn(be::DuelState& st, Side side, Difficulty d) {
    if (st.active_side != side || st.game_over) return;
    if (d == Difficulty::Easy) { play_phase_easy(st, side); use_grave_effects(st, side, 0.5); attack_phase_easy(st, side); }
    else if (d == Difficulty::Normal) { play_phase_greedy(st, side); use_grave_effects(st, side, 1.0); attack_phase_normal(st, side); }
    else { play_phase_hard(st, side); use_grave_effects(st, side, 1.0); attack_phase_hard(st, side); }
    if (!st.game_over) be::end_turn(st);
}

}  // namespace fd::bot_ai
