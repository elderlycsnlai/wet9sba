#include "battle_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
namespace fd::battle {

using duel_cards::graveyard_entry;
using duel_cards::make_instance;

// ------------------------------------------------------------------ plumbing
Board snapshot(const DuelState& st) {
    Board b;
    b.lp = st.lp; b.shield = st.shield; b.insight = st.insight; b.max_insight = st.max_insight;
    b.deck_count.player = static_cast<int>(st.deck.player.size());
    b.deck_count.bot = static_cast<int>(st.deck.bot.size());
    b.hand = st.hand; b.field = st.field; b.traps = st.traps;
    b.graveyard = st.graveyard; b.banished = st.banished;
    b.turn_number = st.turn_number; b.active_side = st.active_side;
    return b;
}

static void log(DuelState& st, std::string text) { st.log.push_back(std::move(text)); }
static std::string num(int v) { return std::to_string(v); }
static Winner winner_of(Side s) { return s == Side::Player ? Winner::Player : Winner::Bot; }
static const Card* card_of(const DuelState& st, const std::string& id) { return st.card_engine->get_card(id); }
static std::string new_instance_id(DuelState& st, Side side) {
    int n = st.next_instance_id++;
    return std::string(1, side == Side::Player ? 'p' : 'b') + std::to_string(n);
}

static void emit(DuelState& st, Ev type, Side side, const CardInstance* card = nullptr, std::string text = {}, int amount = 0,
                 bool flag = false, std::string from = {}, std::string target = {}) {
    if (!st.record) return;
    Event e;
    e.type = type; e.side = side;
    if (card) e.card = *card;
    e.text = std::move(text); e.amount = amount; e.flag = flag; e.from = std::move(from); e.target_id = std::move(target);
    e.log_size = st.log.size();
    e.after = snapshot(st);
    st.events.push_back(std::move(e));
}

static void end_game(DuelState& st, Winner w, const std::string& msg) {
    if (st.game_over) return;
    st.game_over = true;
    st.winner = w;
    log(st, msg);
    emit(st, Ev::GameOver, Side::Player, nullptr, msg, 0, w == Winner::Player);
}

DuelState new_state() { return DuelState{}; }

void check_win(DuelState& st) {
    if (st.game_over) return;
    if (st.lp.player <= 0 && st.lp.bot <= 0) end_game(st, Winner::Draw, "Both sides reach 0 LP at once -- it's a draw!");
    else if (st.lp.player <= 0) end_game(st, Winner::Bot, "Player's Life Points reach 0. Bot wins!");
    else if (st.lp.bot <= 0) end_game(st, Winner::Player, "Bot's Life Points reach 0. Player wins!");
}

// ---------------------------------------------------------------- lookups
CardInstance* find_in_field(DuelState& st, Side side, const std::string& id) {
    for (auto& c : st.field[side]) if (c.instance_id == id) return &c;
    return nullptr;
}
const CardInstance* find_in_field(const DuelState& st, Side side, const std::string& id) {
    for (const auto& c : st.field[side]) if (c.instance_id == id) return &c;
    return nullptr;
}
static CardInstance* find_in_traps(DuelState& st, Side side, const std::string& id) {
    for (auto& c : st.traps[side]) if (c.instance_id == id) return &c;
    return nullptr;
}
static void remove_from_field(DuelState& st, Side side, const std::string& id) {
    auto& f = st.field[side];
    f.erase(std::remove_if(f.begin(), f.end(), [&](const CardInstance& c) { return c.instance_id == id; }), f.end());
}
static CardInstance* strongest_of(std::vector<CardInstance>& f) {          // highest ATK, then highest HP
    CardInstance* best = nullptr;
    for (auto& c : f) if (!best || c.atk > best->atk || (c.atk == best->atk && c.hp > best->hp)) best = &c;
    return best;
}
static const CardInstance* weakest_other(const std::vector<CardInstance>& f, const std::string& skip_id) {
    const CardInstance* best = nullptr;
    for (const auto& c : f) {
        if (c.instance_id == skip_id) continue;
        if (!best || c.atk + c.hp < best->atk + best->hp) best = &c;
    }
    return best;
}
static int grave_value(const DuelState& st, const GraveEntry& g) {
    const Card* c = card_of(st, g.card_id);
    if (!c) return 0;
    const auto s = duel_cards::stats_at_level(*c, g.level);
    return s.atk + s.hp;
}
// Best Graveyard Monster that may still be revived (max_cost 0 = any cost). -1 if none.
static int best_grave_monster(const DuelState& st, const std::vector<GraveEntry>& gy, int max_cost) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(gy.size()); ++i) {
        const GraveEntry& g = gy[i];
        if (g.card_type != CardType::Monster || g.revived) continue;
        if (max_cost > 0) {
            const Card* c = card_of(st, g.card_id);
            if (!c || duel_cards::stats_at_level(*c, g.level).cost > max_cost) continue;
        }
        if (best < 0 || grave_value(st, g) >= grave_value(st, gy[best])) best = i;
    }
    return best;
}

// ------------------------------------------------------- life, shield, cards
void apply_face_damage(DuelState& st, Side side, int amount) {
    if (amount <= 0) return;
    int absorbed = std::min(st.shield[side], amount);
    st.shield[side] -= absorbed;
    int remaining = amount - absorbed;
    if (absorbed) log(st, side_cap(side) + "'s Shield absorbs " + num(absorbed) + " damage.");
    if (remaining) {
        st.lp[side] -= remaining;
        log(st, side_cap(side) + " takes " + num(remaining) + " damage (" + num(st.lp[side]) + " LP left).");
    }
    emit(st, Ev::FaceDamage, side, nullptr, num(absorbed), remaining, absorbed > 0);
    check_win(st);
}

static int heal(DuelState& st, Side side, int amount) {
    int healed = std::max(0, std::min(amount, STARTING_LP - st.lp[side]));
    st.lp[side] += healed;
    if (healed) {
        log(st, side_cap(side) + " recovers " + num(healed) + " LP (" + num(st.lp[side]) + " LP now).");
        emit(st, Ev::Heal, side, nullptr, "", healed);
    }
    return healed;
}
static void add_shield(DuelState& st, Side side, int amount) {
    if (amount <= 0) return;
    st.shield[side] += amount;
    log(st, side_cap(side) + " gains " + num(amount) + " Shield.");
    emit(st, Ev::ShieldGain, side, nullptr, "", amount);
}

void draw_card(DuelState& st, Side side) {
    if (st.game_over) return;
    auto& deck = st.deck[side];
    if (deck.empty()) {
        end_game(st, winner_of(other(side)), side_cap(side) + " has no cards left to draw and loses by fatigue!");
        return;
    }
    DeckEntry e = deck.back();
    deck.pop_back();
    const Card* base = card_of(st, e.card_id);
    st.hand[side].push_back(make_instance(*base, e.level, new_instance_id(st, side)));   // no hand size limit
    emit(st, Ev::Draw, side, &st.hand[side].back(), "", 0, false, "deck");
}

// Forward declarations: effects, triggers and destruction call each other.
struct Ctx;
static void run_ops(DuelState& st, Ctx& cx, const std::vector<AbilityOp>& ops);
static void trigger_abilities(DuelState& st, Side side, const CardInstance& inst, Timing t);

static void destroy_monster(DuelState& st, Side side, CardInstance inst) {   // by value: callers pass refs into the vector we erase from
    remove_from_field(st, side, inst.instance_id);
    st.graveyard[side].push_back(graveyard_entry(inst));
    log(st, inst.name + " is destroyed.");
    emit(st, Ev::Destroy, side, &inst, "", 0, false, "field");
    trigger_abilities(st, side, inst, Timing::Death);
}
static void banish_monster(DuelState& st, Side side, CardInstance inst) {
    remove_from_field(st, side, inst.instance_id);
    st.banished[side].push_back(graveyard_entry(inst));
    log(st, inst.name + " is banished -- removed from the game entirely.");
    emit(st, Ev::Banish, side, &inst, "", 0, false, "field");
}
static void bounce_monster(DuelState& st, Side owner, CardInstance inst) {
    remove_from_field(st, owner, inst.instance_id);
    const Card* base = card_of(st, inst.card_id);
    st.hand[owner].push_back(make_instance(*base, inst.level, new_instance_id(st, owner)));
    log(st, inst.name + " is returned to " + side_key(owner) + "'s hand.");
    emit(st, Ev::ToHand, owner, &st.hand[owner].back(), "", 0, false, "field", inst.instance_id);
}
static void hurt_monster(DuelState& st, Side side, const std::string& id, int amount, bool kill_now = true) {
    CardInstance* c = find_in_field(st, side, id);
    if (!c || amount <= 0) return;
    c->hp -= amount;
    log(st, c->name + " takes " + num(amount) + " damage.");
    emit(st, Ev::Damage, side, c, "", amount);
    if (kill_now && c->hp <= 0) destroy_monster(st, side, *c);
}
static bool special_summon(DuelState& st, Side side, const GraveEntry& g, const char* from) {
    if (static_cast<int>(st.field[side].size()) >= FIELD_LIMIT) {
        log(st, side_cap(side) + "'s field is full -- " + g.name + " can't be Special Summoned.");
        return false;
    }
    CardInstance inst = make_instance(*card_of(st, g.card_id), g.level, new_instance_id(st, side));
    inst.revived = true;                       // a revived monster can never be revived again
    inst.can_attack_this_turn = false;
    st.field[side].push_back(inst);
    log(st, g.name + " is Special Summoned to " + side_key(side) + "'s field!");
    emit(st, Ev::Summon, side, &st.field[side].back(), "", 0, true, from);
    return true;
}
static void discard_from_hand(DuelState& st, Side owner, int idx) {
    CardInstance inst = st.hand[owner][idx];
    st.hand[owner].erase(st.hand[owner].begin() + idx);
    st.graveyard[owner].push_back(graveyard_entry(inst));
    log(st, side_cap(owner) + " discards " + inst.name + ".");
    emit(st, Ev::ToGrave, owner, &inst, "", 0, false, "hand");
    trigger_abilities(st, owner, inst, Timing::Mill);
}
static void mill(DuelState& st, Side side, int count) {
    for (int i = 0; i < count && !st.game_over; ++i) {
        if (st.deck[side].empty()) break;
        DeckEntry e = st.deck[side].back();
        st.deck[side].pop_back();
        CardInstance tmp = make_instance(*card_of(st, e.card_id), e.level, new_instance_id(st, side));
        st.graveyard[side].push_back(graveyard_entry(tmp));
        log(st, tmp.name + " is sent from " + side_key(side) + "'s deck to the Graveyard.");
        emit(st, Ev::ToGrave, side, &tmp, "", 0, false, "deck");
        trigger_abilities(st, side, tmp, Timing::Mill);
    }
}
static void search_deck(DuelState& st, Side side, CardType type, int max_cost, const std::string& who) {
    std::vector<int> pool;
    for (int i = 0; i < static_cast<int>(st.deck[side].size()); ++i) {
        const DeckEntry& e = st.deck[side][i];
        const Card* c = card_of(st, e.card_id);
        if (!c || c->card_type != type) continue;
        if (max_cost > 0 && duel_cards::stats_at_level(*c, e.level).cost > max_cost) continue;
        pool.push_back(i);
    }
    if (pool.empty()) { log(st, who + ": no matching card left in the deck."); return; }
    const int idx = pool[rand_index(pool.size())];
    DeckEntry e = st.deck[side][idx];
    st.deck[side].erase(st.deck[side].begin() + idx);
    st.hand[side].push_back(make_instance(*card_of(st, e.card_id), e.level, new_instance_id(st, side)));
    log(st, who + ": " + st.hand[side].back().name + " is added to " + side_key(side) + "'s hand.");
    emit(st, Ev::ToHand, side, &st.hand[side].back(), "", 0, false, "deck");
}

// ------------------------------------------------------------ effect engine
struct Ctx {
    Side side = Side::Player;      // controller of the card whose ability is running
    CardInstance src;              // that card (a copy)
    int level = 1;
    Side actor = Side::Player;     // trap context: the side that triggered the Trap
    std::string target_id;         // trap context: the attacking / summoned monster
    int atk_value = 0;             // trap context: the attacker's ATK
    bool negated = false, banish_spell = false;
    int banished = 0;              // cards banished by the current ability
};

static void exec_op(DuelState& st, Ctx& cx, const AbilityOp& op) {
    if (st.game_over) return;
    const Side me = cx.side, opp = other(me);
    const int lv = cx.level;
    if (!duel_cards::op_active(op, lv)) return;
    const int n = op.n.at(lv), m = op.m.at(lv), fb = op.fb.at(lv);
    const std::string who = cx.src.name;
    const std::string opp_k = side_key(opp);
    auto fallback = [&](const std::string& why) {
        if (fb > 0) {
            log(st, who + ": " + why + " -- " + num(fb) + " damage hits " + opp_k + "'s face instead.");
            apply_face_damage(st, opp, fb);
        } else {
            log(st, who + ": " + why + ".");
        }
    };
    switch (op.kind) {
        case OpKind::DmgFace:
            log(st, who + ": " + num(n) + " damage to " + opp_k + "'s face.");
            apply_face_damage(st, opp, n);
            break;
        case OpKind::DmgSelf:
            log(st, who + ": " + side_key(me) + " takes " + num(n) + " damage.");
            apply_face_damage(st, me, n);
            break;
        case OpKind::DmgRandom: {
            std::vector<std::string> targets{"face"};
            for (const auto& c : st.field[opp]) targets.push_back(c.instance_id);
            const std::string pick = targets[rand_index(targets.size())];
            if (pick == "face") {
                log(st, who + ": " + num(n) + " random damage hits " + opp_k + "'s face!");
                apply_face_damage(st, opp, n);
            } else {
                log(st, who + ": " + num(n) + " random damage hits an enemy monster!");
                hurt_monster(st, opp, pick, n);
            }
            break;
        }
        case OpKind::DmgAll: {
            std::vector<std::string> ids;
            for (const auto& c : st.field[opp]) ids.push_back(c.instance_id);
            if (ids.empty()) { log(st, who + ": no enemy monsters to hit."); break; }
            log(st, who + ": " + num(n) + " damage to every enemy monster.");
            for (const auto& id : ids) hurt_monster(st, opp, id, n);
            break;
        }
        case OpKind::DmgPerMonster: {
            const int dmg = n * static_cast<int>(st.field[me].size());
            log(st, who + ": " + num(dmg) + " damage (" + num(n) + " per monster) to " + opp_k + "'s face.");
            apply_face_damage(st, opp, dmg);
            break;
        }
        case OpKind::DmgPerBanished:
            if (cx.banished > 0) {
                log(st, who + ": " + num(op.k * cx.banished) + " damage for the banished cards.");
                apply_face_damage(st, opp, op.k * cx.banished);
            }
            break;
        case OpKind::DestroyStrongest:
            if (CardInstance* t = strongest_of(st.field[opp])) { CardInstance copy = *t; log(st, who + ": " + copy.name + " is destroyed."); destroy_monster(st, opp, copy); }
            else fallback("no enemy monster to destroy");
            break;
        case OpKind::BanishStrongest:
            if (CardInstance* t = strongest_of(st.field[opp])) { CardInstance copy = *t; banish_monster(st, opp, copy); }
            else fallback("no enemy monster to banish");
            break;
        case OpKind::BounceStrongest:
            if (CardInstance* t = strongest_of(st.field[opp])) { CardInstance copy = *t; bounce_monster(st, opp, copy); }
            else fallback("no enemy monster to return");
            break;
        case OpKind::FreezeStrongest:
            if (CardInstance* t = strongest_of(st.field[opp])) {
                t->frozen = true;
                log(st, who + ": " + t->name + " is frozen -- it can't attack on its next turn.");
                emit(st, Ev::Freeze, opp, t);
            } else log(st, who + ": no enemy monster to freeze.");
            break;
        case OpKind::SilenceStrongest:
            if (CardInstance* t = strongest_of(st.field[opp])) {
                t->silenced = true;
                log(st, who + ": " + t->name + " is silenced -- its abilities are negated.");
                emit(st, Ev::Silence, opp, t);
            } else log(st, who + ": no enemy monster to silence.");
            break;
        case OpKind::DestroyTraps: {
            int destroyed = 0;
            for (int i = 0; i < n && !st.traps[opp].empty(); ++i) {
                const std::size_t idx = rand_index(st.traps[opp].size());
                CardInstance t = st.traps[opp][idx];
                st.traps[opp].erase(st.traps[opp].begin() + static_cast<std::ptrdiff_t>(idx));
                st.graveyard[opp].push_back(graveyard_entry(t));
                t.face_down = false;
                log(st, who + ": the Set Trap " + t.name + " is destroyed!");
                emit(st, Ev::Destroy, opp, &t, "", 0, true, "trap");
                ++destroyed;
            }
            if (!destroyed) log(st, who + ": the enemy has no Set Traps to destroy.");
            break;
        }
        case OpKind::RevealTraps:
            if (!st.traps[opp].empty()) {
                for (auto& t : st.traps[opp]) t.revealed = true;
                log(st, who + ": " + opp_k + "'s Set Traps are revealed.");
                emit(st, Ev::Reveal, opp);
            }
            break;
        case OpKind::BlockTraps:
            st.traps_blocked[opp] = true;
            log(st, who + ": " + opp_k + "'s Set Traps can't be activated this turn.");
            break;
        case OpKind::BanishGy: {
            int count = 0;
            for (int i = 0; i < n && !st.graveyard[opp].empty(); ++i) {
                int best = 0;
                for (int j = 0; j < static_cast<int>(st.graveyard[opp].size()); ++j) {
                    const GraveEntry& g = st.graveyard[opp][j];
                    const GraveEntry& b = st.graveyard[opp][best];
                    const int vj = (g.card_type == CardType::Monster ? 100 : 0) + grave_value(st, g);
                    const int vb = (b.card_type == CardType::Monster ? 100 : 0) + grave_value(st, b);
                    if (vj >= vb) best = j;
                }
                GraveEntry g = st.graveyard[opp][best];
                st.graveyard[opp].erase(st.graveyard[opp].begin() + best);
                st.banished[opp].push_back(g);
                CardInstance ghost = make_instance(*card_of(st, g.card_id), g.level, "g" + num(st.next_instance_id++));
                emit(st, Ev::Banish, opp, &ghost, "", 0, false, "gy");
                ++count;
            }
            cx.banished += count;
            log(st, who + ": " + num(count) + " card(s) banished from " + opp_k + "'s Graveyard.");
            break;
        }
        case OpKind::DiscardEnemy:
            for (int i = 0; i < n && !st.hand[opp].empty() && !st.game_over; ++i)
                discard_from_hand(st, opp, static_cast<int>(rand_index(st.hand[opp].size())));
            break;
        case OpKind::Shield: add_shield(st, me, n); break;
        case OpKind::Heal: heal(st, me, n); break;
        case OpKind::ShieldPerMonster: add_shield(st, me, n * static_cast<int>(st.field[me].size())); break;
        case OpKind::Draw:
            for (int i = 0; i < n; ++i) draw_card(st, me);
            log(st, who + ": " + side_cap(me) + " draws " + num(n) + " card(s).");
            break;
        case OpKind::DrawIfBehind:
            if (st.field[opp].size() > st.field[me].size()) {
                for (int i = 0; i < n; ++i) draw_card(st, me);
                log(st, who + ": behind on monsters -- " + side_cap(me) + " draws " + num(n) + " card(s).");
            } else log(st, who + ": not behind on monsters -- no card is drawn.");
            break;
        case OpKind::Insight:
            st.insight[me] += n;
            log(st, who + ": " + side_cap(me) + " gains " + num(n) + " Insight this turn.");
            emit(st, Ev::Insight, me, nullptr, "", n);
            break;
        case OpKind::PayLp: {
            const int pay = std::min(n, st.lp[me] - 1);
            if (pay > 0) {
                st.lp[me] -= pay;
                log(st, who + ": " + side_cap(me) + " pays " + num(pay) + " LP (" + num(st.lp[me]) + " LP left).");
                emit(st, Ev::FaceDamage, me, nullptr, "0", pay, false);
            }
            break;
        }
        case OpKind::SearchMonster: for (int i = 0; i < n; ++i) search_deck(st, me, CardType::Monster, m, who); break;
        case OpKind::SearchSpell: for (int i = 0; i < n; ++i) search_deck(st, me, CardType::Spell, 0, who); break;
        case OpKind::SearchTrap: for (int i = 0; i < n; ++i) search_deck(st, me, CardType::Trap, 0, who); break;
        case OpKind::MillSelf: mill(st, me, n); break;
        case OpKind::ReviveStrongest: {
            const int idx = best_grave_monster(st, st.graveyard[me], 0);
            if (idx >= 0 && static_cast<int>(st.field[me].size()) < FIELD_LIMIT) {
                GraveEntry g = st.graveyard[me][idx];
                st.graveyard[me].erase(st.graveyard[me].begin() + idx);
                special_summon(st, me, g, "gy");
            } else fallback(idx < 0 ? "no Monster in the Graveyard to revive" : "the field is full");
            break;
        }
        case OpKind::ReviveCostLe:
            for (int i = 0; i < n; ++i) {
                const int idx = best_grave_monster(st, st.graveyard[me], m);
                if (idx < 0 || static_cast<int>(st.field[me].size()) >= FIELD_LIMIT) { if (i == 0) log(st, who + ": nothing to revive."); break; }
                GraveEntry g = st.graveyard[me][idx];
                st.graveyard[me].erase(st.graveyard[me].begin() + idx);
                special_summon(st, me, g, "gy");
            }
            break;
        case OpKind::StealGy: {
            const int idx = best_grave_monster(st, st.graveyard[opp], 0);
            if (idx >= 0 && static_cast<int>(st.field[me].size()) < FIELD_LIMIT) {
                GraveEntry g = st.graveyard[opp][idx];
                st.graveyard[opp].erase(st.graveyard[opp].begin() + idx);
                special_summon(st, me, g, "enemy-gy");
            } else fallback(idx < 0 ? "the enemy Graveyard holds no Monster to take" : "the field is full");
            break;
        }
        case OpKind::BuffAlliesAtk:
        case OpKind::BuffAlliesHp: {
            const bool atk = op.kind == OpKind::BuffAlliesAtk;
            int count = 0;
            for (auto& c : st.field[me]) {
                if (c.instance_id == cx.src.instance_id) continue;
                if (atk) c.atk += n; else { c.hp += n; c.max_hp += n; }
                emit(st, Ev::Buff, me, &c, "+" + num(n) + (atk ? " ATK" : " HP"));
                ++count;
            }
            if (count) log(st, who + ": " + num(count) + " monster(s) gain +" + num(n) + (atk ? " ATK." : " HP."));
            break;
        }
        case OpKind::BuffSelfAtk:
            if (CardInstance* c = find_in_field(st, me, cx.src.instance_id)) {
                c->atk += n;
                log(st, c->name + " gains +" + num(n) + " ATK permanently (now " + num(c->atk) + ").");
                emit(st, Ev::Buff, me, c, "+" + num(n) + " ATK");
            }
            break;
        case OpKind::Indulge:
            if (CardInstance* c = find_in_field(st, me, cx.src.instance_id)) {
                c->atk += n;
                c->pending_penalty += n;
                log(st, "Indulge: " + c->name + " gains +" + num(n) + " ATK until the start of your next turn.");
                emit(st, Ev::Buff, me, c, "+" + num(n) + " ATK");
            }
            break;
        case OpKind::SacForTeam: {
            const CardInstance* w = weakest_other(st.field[me], cx.src.instance_id);
            if (!w) { log(st, who + ": no other monster to sacrifice."); break; }
            CardInstance victim = *w;
            log(st, who + ": " + victim.name + " is sacrificed for the greater good!");
            destroy_monster(st, me, victim);
            if (st.game_over) break;
            for (auto& c : st.field[me]) {
                if (c.instance_id == cx.src.instance_id) continue;
                c.atk += n;
                emit(st, Ev::Buff, me, &c, "+" + num(n) + " ATK");
            }
            heal(st, me, m);
            break;
        }
        case OpKind::ShareDraw:
            log(st, who + ": " + side_cap(me) + " draws " + num(n) + ", " + side_cap(opp) + " draws " + num(m) + ".");
            for (int i = 0; i < n; ++i) draw_card(st, me);
            for (int i = 0; i < m; ++i) draw_card(st, opp);
            break;
        case OpKind::Coin: {
            const bool heads = rand_index(2) == 0;
            log(st, who + ": coin flip -- " + (heads ? "heads!" : "tails."));
            emit(st, Ev::CoinFlip, me, &cx.src, heads ? "Heads" : "Tails", 0, heads);
            run_ops(st, cx, heads ? op.a : op.b);
            break;
        }
        case OpKind::OneOf:
            if (!op.a.empty()) exec_op(st, cx, op.a[rand_index(op.a.size())]);
            break;
        case OpKind::ReturnSelf: {
            auto& gy = st.graveyard[me];
            for (int i = static_cast<int>(gy.size()) - 1; i >= 0; --i) {
                if (gy[i].card_id != cx.src.card_id) continue;
                GraveEntry g = gy[i];
                gy.erase(gy.begin() + i);
                st.hand[me].push_back(make_instance(*card_of(st, g.card_id), g.level, new_instance_id(st, me)));
                log(st, who + " returns to " + side_key(me) + "'s hand.");
                emit(st, Ev::ToHand, me, &st.hand[me].back(), "", 0, false, "gy");
                break;
            }
            break;
        }
        // ---- Trap-only effects: act on the monster / Spell that set the Trap off
        case OpKind::NegateAttack:
            cx.negated = true;
            log(st, "The attack is negated!");
            emit(st, Ev::Negate, me, &cx.src, "Attack negated");
            break;
        case OpKind::ShieldFromAttacker: add_shield(st, me, cx.atk_value); break;
        case OpKind::LeechAttacker: heal(st, me, cx.atk_value); break;
        case OpKind::TBounce:
            if (CardInstance* t = find_in_field(st, cx.actor, cx.target_id)) { CardInstance copy = *t; bounce_monster(st, cx.actor, copy); }
            break;
        case OpKind::TDestroy:
            if (CardInstance* t = find_in_field(st, cx.actor, cx.target_id)) { CardInstance copy = *t; destroy_monster(st, cx.actor, copy); }
            break;
        case OpKind::TWeaken:
            if (CardInstance* t = find_in_field(st, cx.actor, cx.target_id)) {
                t->atk = std::max(0, t->atk - n);
                log(st, t->name + "'s ATK is permanently reduced by " + num(n) + ".");
                emit(st, Ev::Debuff, cx.actor, t, "-" + num(n) + " ATK");
            }
            break;
        case OpKind::TNegateEffect:
            cx.negated = true;
            log(st, "The summoned monster's effect is negated!");
            emit(st, Ev::Negate, me, &cx.src, "Effect negated");
            break;
        case OpKind::TDmgSummoner:
            log(st, who + ": " + side_key(cx.actor) + " takes " + num(n) + " damage from the Trap.");
            apply_face_damage(st, cx.actor, n);
            break;
        case OpKind::NegateSpell:
            cx.negated = true;
            cx.banish_spell = op.k == 1;
            log(st, "The Spell is negated!");
            emit(st, Ev::Negate, me, &cx.src, "Spell negated");
            break;
    }
}

static void run_ops(DuelState& st, Ctx& cx, const std::vector<AbilityOp>& ops) {
    for (const auto& op : ops) {
        if (st.game_over) return;
        exec_op(st, cx, op);
    }
}

// Runs every ability of `inst` that has the given timing. `inst` must be a copy (effects may erase zones).
static void trigger_abilities(DuelState& st, Side side, const CardInstance& inst, Timing t) {
    if (st.game_over || inst.silenced || st.chain_depth >= 6) return;
    const Card* base = card_of(st, inst.card_id);
    if (!base) return;
    for (const Ability* ab : duel_cards::abilities_of(*base, t)) {
        const std::string text = duel_cards::describe_ability(*base, *ab, inst.level);
        if (text.empty()) continue;                      // every number is 0 at this level
        ++st.chain_depth;
        Ctx cx;
        cx.side = side; cx.src = inst; cx.level = inst.level;
        log(st, inst.name + " activates (" + duel_cards::timing_label(t) + ").");
        emit(st, Ev::Ability, side, &inst, text, 0, false, duel_cards::timing_label(t));
        run_ops(st, cx, ab->ops);
        --st.chain_depth;
        if (st.game_over) return;
    }
}

// ------------------------------------------------------------------ traps
struct TrapResult { bool activated = false, negated = false, banish_spell = false; };

static TrapResult trap_window(DuelState& st, Side defender, Trigger trig, Side actor, const std::string& target_id, int atk_value) {
    TrapResult res;
    if (st.game_over || st.traps_blocked[defender] || !st.trap_resolver) return res;
    std::vector<CardInstance> eligible;
    for (const auto& t : st.traps[defender]) if (t.trigger == trig) eligible.push_back(t);
    if (eligible.empty()) return res;
    const std::string id = st.trap_resolver(st, defender, eligible);
    if (id.empty()) return res;
    CardInstance* tp = find_in_traps(st, defender, id);
    if (!tp) return res;
    CardInstance trap = *tp;
    auto& v = st.traps[defender];
    v.erase(std::remove_if(v.begin(), v.end(), [&](const CardInstance& t) { return t.instance_id == trap.instance_id; }), v.end());
    trap.face_down = false;
    res.activated = true;
    log(st, side_cap(defender) + " activates the Trap " + trap.name + "!");
    const Card* base = card_of(st, trap.card_id);
    std::string text;
    for (const Ability* ab : duel_cards::abilities_of(*base, Timing::Trap)) text += duel_cards::describe_ability(*base, *ab, trap.level);
    emit(st, Ev::TrapFlip, defender, &trap, text);
    Ctx cx;
    cx.side = defender; cx.src = trap; cx.level = trap.level; cx.actor = actor; cx.target_id = target_id; cx.atk_value = atk_value;
    for (const Ability* ab : duel_cards::abilities_of(*base, Timing::Trap)) run_ops(st, cx, ab->ops);
    st.graveyard[defender].push_back(graveyard_entry(trap));
    emit(st, Ev::ToGrave, defender, &trap, "", 0, false, "trap");
    res.negated = cx.negated;
    res.banish_spell = cx.banish_spell;
    check_win(st);
    return res;
}

// --------------------------------------------------------- playing / combat
Action can_play(const DuelState& st, Side side, int idx) {
    if (st.game_over || st.active_side != side) return {false, "It's not your turn."};
    if (idx < 0 || idx >= static_cast<int>(st.hand[side].size())) return {false, "No such card in hand."};
    const CardInstance& card = st.hand[side][idx];
    if (card.card_type == CardType::Monster && static_cast<int>(st.field[side].size()) >= FIELD_LIMIT)
        return {false, "Your field is full (max 5 monsters)."};
    if (card.card_type == CardType::Trap && static_cast<int>(st.traps[side].size()) >= TRAP_ZONE_LIMIT)
        return {false, "Your trap zone is full (max 3 Set traps)."};
    if (card.cost > st.insight[side])
        return {false, "Not enough Insight (needs " + num(card.cost) + ", you have " + num(st.insight[side]) + ")."};
    return {true, ""};
}

Action play_card(DuelState& st, Side side, int hand_index) {
    Action ok = can_play(st, side, hand_index);
    if (!ok.ok) return ok;
    const Side opp = other(side);
    CardInstance instance = st.hand[side][hand_index];
    st.hand[side].erase(st.hand[side].begin() + hand_index);
    st.insight[side] -= instance.cost;
    const Card* base = card_of(st, instance.card_id);

    if (instance.card_type == CardType::Trap) {
        instance.face_down = true;
        st.traps[side].push_back(instance);
        log(st, side_cap(side) + " Sets a Trap face-down.");
        emit(st, Ev::SetTrap, side, &st.traps[side].back(), "", 0, false, "hand");
        return {true, ""};
    }
    if (instance.card_type == CardType::Spell) {
        log(st, side_cap(side) + " activates the Spell " + instance.name + ".");
        std::string text;
        for (const auto& line : duel_cards::describe(*base, instance.level)) text += (text.empty() ? "" : "\n") + line;
        emit(st, Ev::SpellCast, side, &instance, text, 0, false, "hand");
        const TrapResult tr = trap_window(st, opp, Trigger::OnSpell, side, instance.instance_id, 0);
        if (!st.game_over && !tr.negated) {
            Ctx cx;
            cx.side = side; cx.src = instance; cx.level = instance.level;
            for (const Ability* ab : duel_cards::abilities_of(*base, Timing::Spell)) run_ops(st, cx, ab->ops);
        } else if (tr.negated) {
            log(st, instance.name + " is negated and has no effect.");
        }
        if (tr.negated && tr.banish_spell) {
            st.banished[side].push_back(graveyard_entry(instance));
            emit(st, Ev::Banish, side, &instance, "", 0, false, "spell");
        } else {
            st.graveyard[side].push_back(graveyard_entry(instance));
            emit(st, Ev::ToGrave, side, &instance, "", 0, false, "spell");
        }
        check_win(st);
        return {true, ""};
    }
    // Monster
    instance.can_attack_this_turn = base->rush;
    st.field[side].push_back(instance);
    log(st, side_cap(side) + " plays " + instance.name + " (ATK " + num(instance.atk) + "/HP " + num(instance.hp) + ")." + (base->rush ? " Rush!" : ""));
    emit(st, Ev::Summon, side, &st.field[side].back(), "", 0, false, "hand");
    const TrapResult tr = trap_window(st, opp, Trigger::OnSummon, side, instance.instance_id, 0);
    if (CardInstance* still = find_in_field(st, side, instance.instance_id)) {
        if (!tr.negated && !st.game_over) { CardInstance copy = *still; trigger_abilities(st, side, copy, Timing::Summon); }
        else if (tr.negated) log(st, instance.name + "'s On Summon effect is negated.");
    }
    check_win(st);
    return {true, ""};
}

Action can_attack(const DuelState& st, Side side, const std::string& attacker_id) {
    if (st.game_over || st.active_side != side) return {false, "It's not your turn."};
    const CardInstance* a = find_in_field(st, side, attacker_id);
    if (!a) return {false, "No such monster on your field."};
    if (!a->can_attack_this_turn) return {false, "That monster can't attack yet (just played, frozen, or already attacked this turn)."};
    return {true, ""};
}

Action attack(DuelState& st, Side side, const std::string& attacker_id, const std::string& target) {
    Action ok = can_attack(st, side, attacker_id);
    if (!ok.ok) return ok;
    const Side opp = other(side);
    if (target != "face" && !find_in_field(st, opp, target)) return {false, "No such enemy monster to attack."};
    const CardInstance snap = *find_in_field(st, side, attacker_id);

    log(st, snap.name + " declares an attack" + (target == "face" ? " on " + std::string(side_key(opp)) + "'s face." : "."));
    emit(st, Ev::Attack, side, &snap, "", 0, false, "declare", target);
    const TrapResult tr = trap_window(st, opp, Trigger::OnAttack, side, attacker_id, snap.atk);
    if (CardInstance* a = find_in_field(st, side, attacker_id)) a->can_attack_this_turn = false;
    if (st.game_over) return {true, ""};
    CardInstance* attacker = find_in_field(st, side, attacker_id);
    if (!attacker) return {true, ""};                       // a Trap removed the attacker
    if (tr.negated) return {true, ""};

    { CardInstance copy = *attacker; trigger_abilities(st, side, copy, Timing::Attack); }
    attacker = find_in_field(st, side, attacker_id);
    if (!attacker || st.game_over) return {true, ""};

    emit(st, Ev::Attack, side, attacker, "", 0, false, "strike", target);
    if (target == "face") {
        log(st, attacker->name + " hits " + side_key(opp) + "'s face for " + num(attacker->atk) + ".");
        apply_face_damage(st, opp, attacker->atk);
    } else {
        CardInstance* defender = find_in_field(st, opp, target);
        if (!defender) return {true, ""};                   // the target vanished (for example destroyed by a Trap)
        log(st, attacker->name + " (" + num(attacker->atk) + "/" + num(attacker->hp) + ") attacks " + defender->name + " (" +
                    num(defender->atk) + "/" + num(defender->hp) + ").");
        const int dealt = attacker->atk, taken = defender->atk;
        const std::string did = defender->instance_id;
        hurt_monster(st, opp, did, dealt, false);
        hurt_monster(st, side, attacker_id, taken, false);
        CardInstance* d2 = find_in_field(st, opp, did);
        if (d2 && d2->hp <= 0) { CardInstance copy = *d2; destroy_monster(st, opp, copy); }
        CardInstance* a2 = find_in_field(st, side, attacker_id);
        if (a2 && a2->hp <= 0) { CardInstance copy = *a2; destroy_monster(st, side, copy); }
    }
    check_win(st);
    return {true, ""};
}

// ------------------------------------------------------- Graveyard effects
Action can_activate_grave(const DuelState& st, Side side, int idx) {
    if (st.game_over || st.active_side != side) return {false, "It's not your turn."};
    if (idx < 0 || idx >= static_cast<int>(st.graveyard[side].size())) return {false, "No such card in the Graveyard."};
    const Card* base = card_of(st, st.graveyard[side][idx].card_id);
    const Ability* ab = base ? duel_cards::grave_ability(*base) : nullptr;
    if (!ab) return {false, "That card has no Graveyard effect."};
    if (ab->grave_cost > st.insight[side])
        return {false, "Not enough Insight (needs " + num(ab->grave_cost) + ", you have " + num(st.insight[side]) + ")."};
    return {true, ""};
}

Action activate_grave(DuelState& st, Side side, int idx) {
    Action ok = can_activate_grave(st, side, idx);
    if (!ok.ok) return ok;
    GraveEntry g = st.graveyard[side][idx];
    const Card* base = card_of(st, g.card_id);
    const Ability* ab = duel_cards::grave_ability(*base);
    st.insight[side] -= ab->grave_cost;
    st.graveyard[side].erase(st.graveyard[side].begin() + idx);
    st.banished[side].push_back(g);
    CardInstance inst = make_instance(*base, g.level, new_instance_id(st, side));
    log(st, side_cap(side) + " banishes " + g.name + " from the Graveyard to use its effect.");
    emit(st, Ev::Banish, side, &inst, "", 0, false, "gy");
    emit(st, Ev::Ability, side, &inst, duel_cards::describe_ability(*base, *ab, g.level), 0, false, duel_cards::timing_label(Timing::Grave));
    Ctx cx;
    cx.side = side; cx.src = inst; cx.level = g.level;
    run_ops(st, cx, ab->ops);
    check_win(st);
    return {true, ""};
}

int grave_effect_count(const DuelState& st, Side side) {
    int n = 0;
    for (const auto& g : st.graveyard[side]) {
        const Card* c = card_of(st, g.card_id);
        if (c && duel_cards::has_grave_ability(*c)) ++n;
    }
    return n;
}
int usable_grave_count(const DuelState& st, Side side) {
    int n = 0;
    for (int i = 0; i < static_cast<int>(st.graveyard[side].size()); ++i) if (can_activate_grave(st, side, i).ok) ++n;
    return n;
}

// -------------------------------------------------------------- match flow
DuelState start_duel(const std::vector<DeckEntry>& player_deck, const std::vector<DeckEntry>& bot_deck,
                     const CardEngine& engine, Side starting_side) {
    if (player_deck.size() != 40 || bot_deck.size() != 40)
        throw std::invalid_argument("Both decks must contain exactly 40 cards.");
    DuelState st = new_state();
    st.active_side = starting_side;
    st.card_engine = &engine;
    st.deck.player = player_deck;
    st.deck.bot = bot_deck;
    shuffle_vec(st.deck.player);
    shuffle_vec(st.deck.bot);
    for (int i = 0; i < OPENING_HAND; ++i) draw_card(st, Side::Player);
    for (int i = 0; i < OPENING_HAND; ++i) draw_card(st, Side::Bot);
    return st;
}

void begin_match(DuelState& st, Side starting_side) { begin_turn(st, starting_side); }

void mulligan(DuelState& st, Side side, std::vector<int> idx) {
    if (idx.empty()) return;
    std::sort(idx.begin(), idx.end(), std::greater<int>());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    std::vector<DeckEntry> returned;
    for (int i : idx) {
        if (i < 0 || i >= static_cast<int>(st.hand[side].size())) continue;
        returned.push_back({st.hand[side][i].card_id, st.hand[side][i].level});
        st.hand[side].erase(st.hand[side].begin() + i);
    }
    st.deck[side].insert(st.deck[side].end(), returned.begin(), returned.end());
    shuffle_vec(st.deck[side]);
    for (std::size_t i = 0; i < returned.size(); ++i) draw_card(st, side);
    log(st, side_cap(side) + " mulligans " + num(static_cast<int>(returned.size())) + " card(s).");
}

static void run_turn_triggers(DuelState& st, Side side, Timing t) {
    std::vector<std::string> ids;
    for (const auto& c : st.field[side]) ids.push_back(c.instance_id);
    for (const auto& id : ids) {
        if (st.game_over) return;
        if (const CardInstance* c = find_in_field(st, side, id)) { CardInstance copy = *c; trigger_abilities(st, side, copy, t); }
    }
}

void begin_turn(DuelState& st, Side side) {
    st.active_side = side;
    st.max_insight[side] = std::min(MAX_INSIGHT, st.max_insight[side] + 1);
    st.insight[side] = st.max_insight[side];
    for (auto& inst : st.field[side]) {
        inst.can_attack_this_turn = !inst.frozen;
        if (inst.frozen) { log(st, inst.name + " is frozen and can't attack this turn."); inst.frozen = false; }
        if (inst.pending_penalty) {
            inst.atk = std::max(0, inst.atk - inst.pending_penalty);
            log(st, inst.name + "'s Indulge fades: -" + num(inst.pending_penalty) + " ATK.");
            inst.pending_penalty = 0;
        }
    }
    log(st, "--- " + side_cap(side) + "'s turn " + num(st.turn_number) + " (Insight " + num(st.insight[side]) + "/" + num(st.max_insight[side]) + ") ---");
    emit(st, Ev::TurnStart, side, nullptr, "", st.turn_number);
    if (!st.very_first_turn_done) st.very_first_turn_done = true;
    else draw_card(st, side);
    run_turn_triggers(st, side, Timing::TurnStart);
}

void end_turn(DuelState& st) {
    if (st.game_over) return;
    const Side current = st.active_side;
    run_turn_triggers(st, current, Timing::TurnEnd);
    if (st.game_over) return;
    st.traps_blocked.player = st.traps_blocked.bot = false;
    const Side next_side = other(current);
    if (next_side == Side::Player) ++st.turn_number;
    if (st.turn_number > TURN_LIMIT) {
        Winner w = st.lp.player == st.lp.bot ? Winner::Draw : (st.lp.player > st.lp.bot ? Winner::Player : Winner::Bot);
        end_game(st, w, "Turn limit (" + num(TURN_LIMIT) + ") reached -- higher Life Points wins.");
        return;
    }
    begin_turn(st, next_side);
}

}  // namespace fd::battle
