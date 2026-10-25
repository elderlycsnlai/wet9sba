// Headless regression suite -- the C++ port of dev_checks.py.
// Exercises every engine module without opening a window.
// Usage: dev_checks [path/to/data]
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "battle_engine.hpp"
#include "bot_ai.hpp"
#include "card_engine.hpp"
#include "collection.hpp"
#include "deck_builder.hpp"
#include "duel_cards.hpp"
#include "evaluator.hpp"
#include "preset_decks.hpp"
#include "profile_manager.hpp"
#include "progress.hpp"
#include "report_generator.hpp"

using namespace fd;
namespace be = fd::battle;
namespace fs = std::filesystem;

struct TestFailure {};
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("    check failed: %s  (line %d)\n", #cond, __LINE__);     \
            throw TestFailure();                                                   \
        }                                                                          \
    } while (0)

extern std::string g_data_dir;
static CardEngine* g_engine = nullptr;
static CardEngine& E() { return *g_engine; }
static int g_failed = 0, g_total = 0;

static void run(const char* name, void (*fn)()) {
    ++g_total;
    try {
        fn();
        std::printf("PASS: %s\n", name);
    } catch (const TestFailure&) {
        ++g_failed;
        std::printf("FAIL: %s\n", name);
    } catch (const std::exception& e) {
        ++g_failed;
        std::printf("FAIL: %s  (exception: %s)\n", name, e.what());
    }
}

// ------------------------------------------------------------------ helpers
static int g_iid = 0;
static CardInstance inst(const std::string& card_id, int level = 2) {
    return duel_cards::make_instance(*E().get_card(card_id), level, "t" + std::to_string(++g_iid));
}
static std::vector<DeckEntry> sample_deck(int level = 2) {
    std::vector<DeckEntry> d;
    for (int i = 0; i < 20; ++i) { d.push_back({E().cards()[i].id, level}); d.push_back({E().cards()[i].id, level}); }
    return d;
}
static be::DuelState duel(Side first = Side::Player) {
    auto st = be::start_duel(sample_deck(), sample_deck(), E(), first);
    be::begin_match(st, first);
    return st;
}
// Unique per run (never uses the seeded game RNG) and removed again when the program exits.
static std::vector<fs::path>& temp_dirs() { static std::vector<fs::path> v; return v; }
static std::string temp_dir(const char* tag) {
    static int counter = 0;
    fs::path p = fs::temp_directory_path() /
                 ("fd_" + std::string(tag) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(++counter));
    fs::create_directories(p);
    temp_dirs().push_back(p);
    return p.string();
}
static bool log_contains(const be::DuelState& st, const std::string& needle) {
    for (const auto& l : st.log) if (l.find(needle) != std::string::npos) return true;
    return false;
}

// -------------------------------------------------------------------- tests
static void t_no_repeat_draw_real() {
    // Draw the whole 10-card pool of one school: no repeats until it is exhausted, then it reshuffles.
    CardEngine ce(g_data_dir);
    std::set<std::string> seen;
    for (int i = 0; i < 10; ++i) {
        auto [school, card] = ce.draw_card("STO");
        CHECK(school->id == "STO");
        CHECK(seen.insert(card->id).second);
    }
    auto [school, card] = ce.draw_card("STO");   // pool exhausted -> reshuffle
    CHECK(card != nullptr && card->school_id == "STO");
}
static void t_wisdom_cap() {
    Profile p;
    progress::unlock_school(p, "STO");
    progress::award_points(p, "STO", 95);
    CHECK(progress::get_percentage(p, "STO") == 95);
    progress::award_points(p, "STO", 20);
    CHECK(progress::get_percentage(p, "STO") == 100);
    CHECK(p.lifetime_points == 115);
}
static void t_evaluator() {
    Card c = *E().get_card("STO-01");
    c.correct = 'B';
    auto ok = evaluator::evaluate(c, 'B');
    CHECK(ok.is_correct && ok.points == 10);
    auto lower = evaluator::evaluate(c, 'b');
    CHECK(lower.is_correct);
    auto bad = evaluator::evaluate(c, 'A');
    CHECK(!bad.is_correct && bad.points == 2 && bad.correct_choice == 'B');
}
static void t_all_cards_valid() {
    CHECK(E().cards().size() == 60);
    for (const auto& c : E().cards()) {
        CHECK(E().get_school(c.school_id) != nullptr);
        for (const auto& ch : c.choices) CHECK(!ch.empty());
        CHECK(c.correct >= 'A' && c.correct <= 'D');
        CHECK(!c.explanation.empty() && !c.collectible.empty() && !c.scenario.empty());
        CHECK(c.difficulty >= 1 && c.difficulty <= 3);
        if (c.card_type == CardType::Trap) CHECK(c.trigger != Trigger::None);
        else CHECK(c.trigger == Trigger::None);
        if (c.card_type == CardType::Monster) CHECK(c.atk > 0 && c.hp > 0);
    }
}
static void t_letter_balance() {
    std::map<char, int> n;
    for (const auto& c : E().cards()) ++n[c.correct];
    CHECK(n['A'] == 15 && n['B'] == 15 && n['C'] == 15 && n['D'] == 15);
}
static void t_type_totals() {
    int m = 0, s = 0, t = 0;
    for (const auto& c : E().cards()) {
        if (c.card_type == CardType::Monster) ++m;
        else if (c.card_type == CardType::Spell) ++s;
        else ++t;
    }
    CHECK(m == 42 && s == 12 && t == 6);
}
static void t_collection_levels() {
    Profile p;
    auto r1 = collection::record_correct_answer(p, "STO-01");
    CHECK(r1.status == "new" && r1.level == 1);
    CHECK(collection::record_correct_answer(p, "STO-01").level == 2);
    CHECK(collection::record_correct_answer(p, "STO-01").level == 3);
    auto r4 = collection::record_correct_answer(p, "STO-01");
    CHECK(r4.status == "maxed" && r4.level == 3);
}
static void t_duel_unlock() {
    Profile p;
    for (int i = 0; i < 39; ++i) collection::record_correct_answer(p, E().cards()[i].id);
    CHECK(!collection::duel_unlocked(p));
    collection::record_correct_answer(p, E().cards()[39].id);
    CHECK(collection::duel_unlocked(p));
}
static void t_multi_deck() {
    Profile p;
    CHECK(deck_builder::create_deck(p, "Aggro").ok);
    CHECK(deck_builder::create_deck(p, "Control").ok);
    CHECK(p.active_deck == "Aggro");
    CHECK(!deck_builder::create_deck(p, "Aggro").ok);
    CHECK(!deck_builder::create_deck(p, "   ").ok);
    CHECK(deck_builder::set_active_deck(p, "Control").ok && p.active_deck == "Control");
    CHECK(deck_builder::rename_deck(p, "Aggro", "Rush").ok);
    CHECK(!deck_builder::rename_deck(p, "Rush", "Control").ok);
    auto names = deck_builder::list_decks(p);
    CHECK(names.size() == 2 && names[0] == "Control" && names[1] == "Rush");
    CHECK(deck_builder::delete_deck(p, "Control").ok);
    CHECK(p.active_deck == "Rush");
    CHECK(deck_builder::delete_deck(p, "Rush").ok);
    CHECK(p.active_deck.empty() && p.decks.empty());
    CHECK(!deck_builder::delete_deck(p, "Nope").ok);
}
static void t_decks_isolated() {
    Profile a, b;
    deck_builder::create_deck(a, "Only Mine");
    CHECK(deck_builder::list_decks(b).empty());
    CHECK(deck_builder::list_decks(a).size() == 1);
}
static void t_deck_builder_rules() {
    Profile p;
    deck_builder::create_deck(p, "D");
    CHECK(!deck_builder::add_card(p, "D", E().cards()[50].id).ok);          // not owned
    collection::record_correct_answer(p, "STO-01");
    for (int i = 0; i < 3; ++i) CHECK(deck_builder::add_card(p, "D", "STO-01").ok);
    CHECK(!deck_builder::add_card(p, "D", "STO-01").ok);                    // 4th copy
    CHECK(!deck_builder::add_card(p, "Missing", "STO-01").ok);
    CHECK(deck_builder::remove_card(p, "D", "STO-01").ok);
    CHECK(deck_builder::deck_size(p, "D") == 2);
    CHECK(!deck_builder::remove_card(p, "D", "STO-02").ok);
}
static void t_full_deck_validate() {
    Profile p;
    for (int i = 0; i < 20; ++i) collection::record_correct_answer(p, E().cards()[i].id);
    deck_builder::create_deck(p, "Full");
    for (int i = 0; i < 20; ++i)
        for (int k = 0; k < 2; ++k) CHECK(deck_builder::add_card(p, "Full", E().cards()[i].id).ok);
    auto v = deck_builder::validate_full(p, E(), "Full");
    CHECK(v.legal && v.reasons.empty());
    CHECK(deck_builder::is_battle_ready(p, "Full"));
    CHECK(!deck_builder::add_card(p, "Full", E().cards()[0].id).ok);        // deck already full
    deck_builder::remove_card(p, "Full", E().cards()[0].id);
    CHECK(!deck_builder::validate_full(p, E(), "Full").legal);
}
static void t_start_duel_basic() {
    auto st = duel(Side::Player);
    CHECK(st.hand.player.size() == 5 && st.hand.bot.size() == 5);
    CHECK(st.deck.player.size() == 35 && st.deck.bot.size() == 35);
    CHECK(st.lp.player == 20 && st.lp.bot == 20);
    CHECK(st.max_insight.player == 1 && st.insight.player == 1 && st.max_insight.bot == 0);
    CHECK(st.active_side == Side::Player && st.turn_number == 1);
}
static void t_second_player_draws() {
    auto st = duel(Side::Player);
    CHECK(st.hand.player.size() == 5);          // first mover skips the turn-1 draw
    be::end_turn(st);
    CHECK(st.active_side == Side::Bot);
    CHECK(st.hand.bot.size() == 6);             // second mover draws normally
}
static void t_no_hand_limit() {
    auto st = duel();
    for (int i = 0; i < 20; ++i) be::draw_card(st, Side::Player);
    CHECK(st.hand.player.size() == 25);
    CHECK(!st.game_over);
}
static void t_play_card_deducts() {
    auto st = duel();
    st.hand.player = {inst("STO-01", 2)};
    st.insight.player = 10;
    const int cost = st.hand.player[0].cost;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.insight.player == 10 - cost);
    CHECK(st.field.player.size() == 1);
    CHECK(st.shield.player == E().get_card("STO-01")->abilities.at(0).ops.at(0).n.at(2));
    st.insight.player = 0;
    st.hand.player = {inst("STO-02", 2)};
    CHECK(!be::play_card(st, Side::Player, 0).ok);   // can't afford
}
static void t_shield_absorbs() {
    auto st = duel();
    st.shield.player = 5;
    be::apply_face_damage(st, Side::Player, 3);
    CHECK(st.shield.player == 2 && st.lp.player == 20);
    be::apply_face_damage(st, Side::Player, 5);
    CHECK(st.shield.player == 0 && st.lp.player == 17);
    be::apply_face_damage(st, Side::Player, 0);
    be::apply_face_damage(st, Side::Player, -4);
    CHECK(st.lp.player == 17);
}
static void t_combat_trade() {
    auto st = duel();
    CardInstance a = inst("STO-01", 2), b = inst("STO-01", 2);
    a.can_attack_this_turn = true;
    st.field.player = {a};
    st.field.bot = {b};
    CHECK(be::attack(st, Side::Player, a.instance_id, b.instance_id).ok);
    CHECK(st.field.player.empty() && st.field.bot.empty());
    CHECK(st.graveyard.player.size() == 1 && st.graveyard.bot.size() == 1);
}
static void t_lp_zero() {
    auto st = duel();
    CardInstance a = inst("STO-01", 2);
    a.can_attack_this_turn = true;
    a.atk = 9;
    st.field.player = {a};
    st.lp.bot = 3;
    CHECK(be::attack(st, Side::Player, a.instance_id, "face").ok);
    CHECK(st.game_over && st.winner == be::Winner::Player && st.lp.bot <= 0);
}
static void t_fatigue() {
    auto st = duel();
    st.deck.player.clear();
    be::draw_card(st, Side::Player);
    CHECK(st.game_over && st.winner == be::Winner::Bot);
}
static void t_indulge() {
    auto st = duel();
    st.hand.player = {inst("HED-01", 2)};
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    const auto& m = st.field.player[0];
    const int mag = E().get_card("HED-01")->abilities.at(0).ops.at(0).n.at(2);
    CHECK(m.can_attack_this_turn);                    // Rush
    CHECK(m.atk == m.base_atk + mag && m.pending_penalty == mag);
    be::end_turn(st);                                  // bot's turn
    be::end_turn(st);                                  // back to the player: boost fades
    CHECK(st.field.player[0].atk == st.field.player[0].base_atk);
    CHECK(st.field.player[0].pending_penalty == 0);
}
static void t_cost_monotonic() {
    for (const auto& c : E().cards()) {
        int prev = 0;
        for (int lv = 1; lv <= 3; ++lv) {
            int cost = duel_cards::stats_at_level(c, lv).cost;
            CHECK(cost >= prev);
            CHECK(cost >= 1 && cost <= 10);
            prev = cost;
        }
    }
}
static void t_spell_resolves() {
    auto st = duel();
    st.hand.player = {inst("NIH-08", 2)};     // Excavate (Spell)
    st.insight.player = 10;
    const std::size_t field_before = st.field.player.size();
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.field.player.size() == field_before);
    bool spell_in_gy = false;
    for (const auto& g : st.graveyard.player) if (g.card_id == "NIH-08") spell_in_gy = true;
    CHECK(spell_in_gy);
}
static void t_trap_on_attack() {
    auto st = duel();
    st.trap_resolver = bot_ai::bot_trap_decision;
    CardInstance trap = inst("STO-10", 2);
    trap.face_down = true;
    st.traps.bot = {trap};
    CardInstance a = inst("STO-01", 2);
    a.can_attack_this_turn = true;
    st.field.player = {a};
    CHECK(be::attack(st, Side::Player, a.instance_id, "face").ok);     // fires on the attacker's own turn
    CHECK(st.lp.bot == 20);
    CHECK(st.shield.bot == a.atk);
    CHECK(st.traps.bot.empty() && st.graveyard.bot.size() == 1);
    CHECK(log_contains(st, "activates the Trap"));
}
static void t_trap_on_summon() {
    auto st = duel();
    st.trap_resolver = bot_ai::bot_trap_decision;
    CardInstance trap = inst("UTI-10", 2);
    trap.face_down = true;
    st.traps.bot = {trap};
    st.hand.player = {inst("STO-01", 2)};
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.traps.bot.empty());
    CHECK(st.field.player[0].atk < st.field.player[0].base_atk);
    CHECK(st.shield.player == 0);                       // the trap also negated its On Summon effect
}
static void t_revive_once() {
    auto st = duel();
    st.graveyard.player = {duel_cards::graveyard_entry(inst("STO-01", 2))};
    st.hand.player = {inst("NIH-07", 2)};
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    // NIH-07 itself is a Monster, plus the revived STO-01.
    const CardInstance* revived = nullptr;
    for (const auto& c : st.field.player) if (c.card_id == "STO-01") revived = &c;
    CHECK(revived != nullptr && revived->revived && !revived->can_attack_this_turn);
    CHECK(st.graveyard.player.empty());
    // Destroy the revived monster in combat -> its Graveyard entry remembers it was revived.
    CardInstance killer = inst("STO-01", 3);
    killer.atk = 20;
    killer.can_attack_this_turn = true;
    st.field.bot = {killer};
    st.active_side = Side::Bot;
    CHECK(be::attack(st, Side::Bot, killer.instance_id, revived->instance_id).ok);
    CHECK(st.graveyard.player.size() == 1 && st.graveyard.player[0].revived);
    // A second Revive finds nothing eligible.
    st.active_side = Side::Player;
    st.hand.player = {inst("NIH-07", 2)};
    st.insight.player = 10;
    const std::size_t field_before = st.field.player.size();
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.field.player.size() == field_before + 1);                  // only NIH-07 itself arrived
    CHECK(st.graveyard.player.size() == 1 && st.graveyard.player[0].revived);
}
static void t_banish() {
    auto st = duel();
    st.field.bot = {inst("STO-01", 2)};
    st.hand.player = {inst("NIH-09", 2)};
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.field.bot.empty());
    CHECK(st.banished.bot.size() == 1);
    CHECK(st.graveyard.bot.empty());
}
static void t_mulligan() {
    auto st = duel();
    const auto hand_before = st.hand.player.size();
    const auto deck_before = st.deck.player.size();
    be::mulligan(st, Side::Player, {0, 2, 2});
    CHECK(st.hand.player.size() == hand_before);
    CHECK(st.deck.player.size() == deck_before);
}
static void t_report_alignment() {
    Profile p;
    p.student_id = "aligntest";
    p.name = "Align Test";
    LogData log;
    for (int i = 0; i < 7; ++i) {
        ResponseRec r;
        r.school_id = E().all_schools()[i % E().all_schools().size()].id;
        r.is_correct = (i % 2 == 0);
        log.responses.push_back(r);
    }
    auto rep = report_generator::generate_report(p, log, E().all_schools(), temp_dir("report"));
    CHECK(rep.text_lines.size() > 12);
    const std::size_t width = rep.text_lines[6].size();                  // header row of the table
    for (std::size_t i = 6; i < 6 + 1 + E().all_schools().size(); ++i) CHECK(rep.text_lines[i].size() == width);
    CHECK(fs::exists(rep.txt_path) && fs::exists(rep.csv_path));
}
static void t_presets_legal() {
    CHECK(preset_decks::presets().size() == 3);
    for (const auto& pr : preset_decks::presets()) {
        auto deck = preset_decks::build_preset_deck(E(), pr.id);
        CHECK(deck.size() == 40);
        std::map<std::string, int> n;
        for (const auto& e : deck) { CHECK(E().get_card(e.card_id) != nullptr); CHECK(++n[e.card_id] <= 3); }
    }
}

static be::DuelState run_match(int seed, bot_ai::Difficulty dp, bot_ai::Difficulty db) {
    seed_rng(static_cast<std::uint64_t>(seed));
    Side first = bot_ai::choose_starting_side();
    auto st = be::start_duel(bot_ai::build_bot_deck(E()), bot_ai::build_bot_deck(E()), E(), first);
    st.trap_resolver = bot_ai::bot_trap_decision;
    be::mulligan(st, Side::Player, bot_ai::bot_mulligan_indices(st.hand.player));
    be::mulligan(st, Side::Bot, bot_ai::bot_mulligan_indices(st.hand.bot));
    be::begin_match(st, first);
    for (int i = 0; i < 300 && !st.game_over; ++i)
        bot_ai::take_turn(st, st.active_side, st.active_side == Side::Player ? dp : db);
    return st;
}
static void t_matches_terminate() {
    int p = 0, b = 0, d = 0;
    const bot_ai::Difficulty all[3] = {bot_ai::Difficulty::Easy, bot_ai::Difficulty::Normal, bot_ai::Difficulty::Hard};
    for (int i = 0; i < 20; ++i) {
        auto st = run_match(9000 + i, all[i % 3], all[(i / 3) % 3]);
        CHECK(st.game_over);
        if (st.winner == be::Winner::Player) ++p; else if (st.winner == be::Winner::Bot) ++b; else ++d;
    }
    std::printf("  20 matches with Spells/Traps/Graveyard active: {player: %d, bot: %d, draw: %d}\n", p, b, d);
}
static void t_bot_decks() {
    for (int i = 0; i < 10; ++i) {
        auto deck = bot_ai::build_bot_deck(E());
        CHECK(deck.size() == 40);
        std::map<std::string, int> n;
        for (const auto& e : deck) CHECK(++n[e.card_id] <= 3);
    }
}
static void t_harder_ai_wins() {
    int p = 0, b = 0, d = 0;
    for (int i = 0; i < 40; ++i) {
        auto st = run_match(3000 + i, bot_ai::Difficulty::Hard, bot_ai::Difficulty::Easy);
        if (st.winner == be::Winner::Player) ++p; else if (st.winner == be::Winner::Bot) ++b; else ++d;
    }
    std::printf("  Hard-AI vs Easy-AI: {player: %d, bot: %d, draw: %d}\n", p, b, d);
    CHECK(p >= 24);
}
static void t_profile_roundtrip() {
    const std::string dir = temp_dir("saves");
    ProfileManager pm(dir);
    Profile p = pm.load_or_create("rt1", "Round Trip", "3B");
    progress::unlock_school(p, "STO");
    progress::award_points(p, "STO", 10);
    collection::record_correct_answer(p, "STO-01");
    deck_builder::create_deck(p, "Alpha");
    deck_builder::create_deck(p, "Beta");
    deck_builder::add_card(p, "Beta", "STO-01");
    deck_builder::set_active_deck(p, "Beta");
    pm.save_profile(p);

    Profile q = pm.load_or_create("rt1");
    CHECK(q.name == "Round Trip" && q.class_name == "3B");
    CHECK(q.wisdom["STO"] == 10 && q.lifetime_points == 10);
    CHECK(q.collection.at("STO-01") == 1);
    CHECK(q.decks.size() == 2 && q.decks[0].name == "Alpha" && q.decks[1].name == "Beta");
    CHECK(q.decks[1].cards.size() == 1 && q.active_deck == "Beta");
    CHECK(pm.list_students().size() == 1);

    SessionRec s = pm.start_session("rt1");
    const Card& c1 = *E().get_card("STO-01");
    const Card& c2 = *E().get_card("EXI-01");
    pm.record_round("rt1", s, c1, 'B', evaluator::evaluate(c1, 'B'), 1.5);
    pm.record_round("rt1", s, c2, 'A', evaluator::evaluate(c2, 'A'), 2.5);
    LogData log = pm.get_log("rt1");
    CHECK(log.sessions.size() == 1 && log.responses.size() == 2);
    CHECK(log.sessions[0].rounds_played == 2 && log.sessions[0].total_score == s.total_score);
    CHECK(log.responses[1].card_id == "EXI-01" && log.responses[1].time_taken_sec == 2.5);
}
static void t_corrupted_save() {
    const std::string dir = temp_dir("corrupt");
    ProfileManager pm(dir);
    {
        std::ofstream f(dir + "/bad_profile.json");
        f << "{\"student_id\": \"bad\", \"wisdom\": {  BROKEN";
    }
    Profile p = pm.load_or_create("bad", "Fresh Start");
    CHECK(p.name == "Fresh Start");
    CHECK(!pm.last_warning.empty());
    int backups = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().find(".corrupt-") != std::string::npos) ++backups;
    CHECK(backups == 1);
}

static void t_python_save_compat() {
    // extras/maxtest_profile.json was written by the original Python game.
    fs::path src;
    for (const fs::path& cand : {fs::path(g_data_dir) / ".." / "extras" / "maxtest_profile.json", fs::path("extras") / "maxtest_profile.json",
                                fs::path("..") / "extras" / "maxtest_profile.json", fs::path("..") / ".." / "extras" / "maxtest_profile.json"})
        if (fs::exists(cand)) { src = cand; break; }
    if (src.empty()) { std::printf("  (extras/maxtest_profile.json not found - skipped)\n"); return; }
    const std::string dir = temp_dir("pycompat");
    fs::copy_file(src, fs::path(dir) / "maxtest_profile.json");
    ProfileManager pm(dir);
    Profile p = pm.load_or_create("maxtest");
    CHECK(p.name == "Max Test" && p.collection.size() == 60);
    for (const auto& [id, lvl] : p.collection) { CHECK(E().get_card(id) != nullptr); CHECK(lvl == 3); }
    CHECK(collection::duel_unlocked(p) && p.active_deck == "Everything Deck");
    CHECK(deck_builder::validate_full(p, E()).legal);
    pm.save_profile(p);                                   // and it round-trips again
    CHECK(pm.load_or_create("maxtest").decks.at(0).cards.size() == 40);
}

// ------------------------------------------------- card-design and effect tests
static int op_n(const char* id, std::size_t ability, std::size_t op, int level) {
    return E().get_card(id)->abilities.at(ability).ops.at(op).n.at(level);
}
static CardInstance ready(const std::string& id, int level = 2) { CardInstance c = inst(id, level); c.can_attack_this_turn = true; return c; }
static void set_trap(be::DuelState& st, Side s, const std::string& id, int level = 2) {
    CardInstance t = inst(id, level);
    t.face_down = true;
    st.traps[s].push_back(t);
}
static void sig_ops(std::string& s, const std::vector<AbilityOp>& ops) {
    for (const auto& o : ops) {
        s += std::to_string(static_cast<int>(o.kind)) + ":";
        for (int i = 0; i < 3; ++i) s += std::to_string(o.n.v[i]) + "," + std::to_string(o.m.v[i]) + "," + std::to_string(o.fb.v[i]) + ";";
        s += std::to_string(o.k) + "{";
        sig_ops(s, o.a);
        s += "|";
        sig_ops(s, o.b);
        s += "}";
    }
}
static std::string card_sig(const Card& c) {
    std::string s = std::to_string(c.cost) + "/" + std::to_string(c.atk) + "/" + std::to_string(c.hp) + "/" + std::to_string(c.rush) + "/" + std::to_string(static_cast<int>(c.trigger));
    for (const auto& ab : c.abilities) { s += "@" + std::to_string(static_cast<int>(ab.on)) + "c" + std::to_string(ab.grave_cost) + "["; sig_ops(s, ab.ops); s += "]"; }
    return s;
}
static void t_cards_unique() {
    std::set<std::string> sigs, tags, names;
    for (const auto& c : E().cards()) { sigs.insert(card_sig(c)); tags.insert(c.tag); names.insert(c.collectible); CHECK(!c.tag.empty()); }
    CHECK(sigs.size() == 60);          // no two cards share cost, stats and effects
    CHECK(tags.size() == 60 && names.size() == 60);
}
static void t_cost_curve() {
    int low = 0, one_cost_monsters = 0;
    std::map<int, std::pair<int, int>> sum;        // cost -> (total ATK+HP, count) over Monsters
    for (const auto& c : E().cards()) {
        CHECK(c.cost >= 1 && c.cost <= 6);
        if (c.cost <= 2) ++low;
        if (c.card_type == CardType::Monster) {
            sum[c.cost].first += c.atk + c.hp;
            ++sum[c.cost].second;
            if (c.cost == 1) ++one_cost_monsters;
        }
    }
    CHECK(low >= 20 && one_cost_monsters >= 5);   // the board can start building on turn 1-2
    double prev = 0;
    for (int cost = 1; cost <= 6; ++cost) {       // a more expensive Monster has better base stats on average
        CHECK(sum[cost].second > 0);
        const double mean = static_cast<double>(sum[cost].first) / sum[cost].second;
        CHECK(mean > prev);
        prev = mean;
    }
}
static void t_text_exact() {
    for (const auto& c : E().cards())
        for (int lv = 1; lv <= 3; ++lv) {
            const auto lines = duel_cards::describe(c, lv);
            CHECK(!lines.empty());
            std::string all;
            for (const auto& l : lines) all += l + "\n";
            CHECK(all.find("card's power") == std::string::npos && all.find("according to") == std::string::npos);
            for (const auto& ab : c.abilities)
                for (const auto& op : ab.ops) {
                    if (op.kind == OpKind::Coin || op.kind == OpKind::OneOf) continue;
                    const int n = op.n.at(lv);
                    if (n > 0) CHECK(all.find(std::to_string(n)) != std::string::npos);     // the exact number is printed
                }
        }
    const auto l1 = duel_cards::describe(*E().get_card("STO-01"), 1), l3 = duel_cards::describe(*E().get_card("STO-01"), 3);
    CHECK(l1.at(0).find("Gain 2 Shield") != std::string::npos && l3.at(0).find("Gain 4 Shield") != std::string::npos);
}
static void t_level_stats() {
    const Card& c = *E().get_card("NIH-07");
    const auto a = duel_cards::stats_at_level(c, 1), b = duel_cards::stats_at_level(c, 2), d = duel_cards::stats_at_level(c, 3);
    CHECK(b.atk == a.atk + 1 && b.hp == a.hp && d.atk == b.atk && d.hp == b.hp + 1);
    CHECK(a.cost == b.cost && b.cost == d.cost);
}
static void t_death_trigger() {
    auto st = duel();
    st.field.player = {inst("NIH-03", 2)};                  // Last Gasp: when destroyed, 3 damage to the enemy face
    st.field.bot = {ready("STO-01", 2)};
    st.active_side = Side::Bot;
    CHECK(be::attack(st, Side::Bot, st.field.bot[0].instance_id, st.field.player[0].instance_id).ok);
    CHECK(st.field.player.empty());
    CHECK(st.lp.bot == 20 - op_n("NIH-03", 0, 0, 2));
}
static void t_mill_trigger() {
    auto st = duel();
    st.deck.player = {{"ABS-06", 2}, {"ABS-06", 2}, {"NIH-06", 2}};      // NIH-06 is on top
    st.hand.player = {inst("NIH-08", 2)};                                 // Burn Down: mill 3
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    // NIH-06 is milled first and its trigger draws the next card from the deck, so only two cards reach the Graveyard
    // from the deck (NIH-06, ABS-06) plus the Spell itself.
    CHECK(st.deck.player.empty() && st.graveyard.player.size() == 3);
    CHECK(st.hand.player.size() == 1);                                    // NIH-06 drew a card when it was milled
}
static void t_grave_effect() {
    auto st = duel();
    st.graveyard.player = {duel_cards::graveyard_entry(inst("NIH-04", 2)), duel_cards::graveyard_entry(inst("STO-01", 2))};
    st.insight.player = 0;
    CHECK(be::grave_effect_count(st, Side::Player) == 1 && be::usable_grave_count(st, Side::Player) == 0);
    CHECK(!be::activate_grave(st, Side::Player, 0).ok);                   // can't afford it
    st.insight.player = 3;
    CHECK(be::usable_grave_count(st, Side::Player) == 1);
    CHECK(!be::can_activate_grave(st, Side::Player, 1).ok);               // STO-01 has no Graveyard effect
    const auto hand_before = st.hand.player.size();
    CHECK(be::activate_grave(st, Side::Player, 0).ok);
    CHECK(st.insight.player == 2 && st.hand.player.size() == hand_before + 1);
    CHECK(st.banished.player.size() == 1 && st.graveyard.player.size() == 1);
    CHECK(be::usable_grave_count(st, Side::Player) == 0);
    st.active_side = Side::Bot;                                           // only on your own turn
    st.graveyard.bot = {duel_cards::graveyard_entry(inst("NIH-04", 2))};
    st.insight.bot = 5;
    CHECK(!be::can_activate_grave(st, Side::Player, 0).ok && be::can_activate_grave(st, Side::Bot, 0).ok);
}
static void t_destroy_traps() {
    auto st = duel();
    set_trap(st, Side::Bot, "STO-10");
    set_trap(st, Side::Bot, "HED-10");
    st.hand.player = {inst("ABS-07", 2)};                                  // Backstage Pass: reveal, then destroy 1 Set Trap
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.traps.bot.size() == 1 && st.graveyard.bot.size() == 1);
    CHECK(st.traps.bot[0].revealed);
}
static void t_block_traps() {
    auto st = duel();
    st.trap_resolver = bot_ai::bot_trap_decision;
    set_trap(st, Side::Bot, "STO-10");
    st.hand.player = {inst("STO-09", 2)};                                  // Clear Path: Traps can't activate this turn
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.traps_blocked.bot && st.traps.bot[0].revealed);
    st.field.player = {ready("STO-01", 2)};
    CHECK(be::attack(st, Side::Player, st.field.player[0].instance_id, "face").ok);
    CHECK(st.lp.bot == 18 && st.traps.bot.size() == 1);                    // the Trap stayed asleep
    be::end_turn(st);
    CHECK(!st.traps_blocked.bot);
}
static void t_freeze() {
    auto st = duel();
    st.field.bot = {inst("STO-01", 2)};
    st.hand.player = {inst("STO-04", 2)};                                  // Calm Freeze
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.field.bot[0].frozen);
    be::end_turn(st);                                                      // the bot's turn begins
    CHECK(!st.field.bot[0].can_attack_this_turn && !st.field.bot[0].frozen);
    be::end_turn(st);
    be::end_turn(st);
    CHECK(st.field.bot[0].can_attack_this_turn);                           // frozen for exactly one turn
}
static void t_silence() {
    for (int silenced = 0; silenced < 2; ++silenced) {
        auto st = duel();
        st.field.bot = {inst("NIH-02", 2)};                                // Crumble: when destroyed, 3 damage to every enemy monster
        CardInstance striker = ready("STO-01", 2);
        striker.atk = 20;
        st.field.player = {striker, inst("NIH-07", 3)};
        const int ally_hp = st.field.player[1].hp;
        if (silenced) {
            st.hand.player = {inst("STO-06", 2)};                          // Unshaken: silence the strongest enemy monster
            st.insight.player = 10;
            CHECK(be::play_card(st, Side::Player, 0).ok);
            CHECK(st.field.bot[0].silenced);
        }
        CHECK(be::attack(st, Side::Player, striker.instance_id, st.field.bot[0].instance_id).ok);
        CHECK(st.field.bot.empty());
        const CardInstance* ally = nullptr;
        for (const auto& c : st.field.player) if (c.card_id == "NIH-07") ally = &c;
        CHECK(ally != nullptr);
        if (ally) CHECK(silenced ? ally->hp == ally_hp : ally->hp == ally_hp - op_n("NIH-02", 0, 0, 2));
    }
}
static void t_negate_trap_effects() {
    {   // UTI-10 negates the summoned monster's On Summon effect and weakens it
        auto st = duel();
        st.trap_resolver = bot_ai::bot_trap_decision;
        set_trap(st, Side::Bot, "UTI-10");
        st.hand.player = {inst("STO-01", 2)};
        st.insight.player = 10;
        CHECK(be::play_card(st, Side::Player, 0).ok);
        CHECK(st.shield.player == 0 && log_contains(st, "effect is negated"));
    }
    {   // NIH-10 negates a Spell and banishes it
        auto st = duel();
        st.trap_resolver = bot_ai::bot_trap_decision;
        set_trap(st, Side::Bot, "NIH-10");
        st.lp.player = 10;
        st.hand.player = {inst("STO-08", 2)};
        st.insight.player = 10;
        CHECK(be::play_card(st, Side::Player, 0).ok);
        CHECK(st.lp.player == 10);
        CHECK(st.banished.player.size() == 1 && st.graveyard.player.empty() && st.graveyard.bot.size() == 1);
    }
}
static void t_steal_and_revive_limits() {
    {   // Reflection: take the strongest Monster from the enemy Graveyard
        auto st = duel();
        st.graveyard.bot = {duel_cards::graveyard_entry(inst("STO-01", 2)), duel_cards::graveyard_entry(inst("NIH-07", 2))};
        st.hand.player = {inst("EXI-05", 2)};
        st.insight.player = 10;
        CHECK(be::play_card(st, Side::Player, 0).ok);
        const CardInstance* got = nullptr;
        for (const auto& c : st.field.player) if (c.card_id == "NIH-07") got = &c;
        CHECK(got != nullptr && got->revived && st.graveyard.bot.size() == 1);
    }
    {   // Helping Hand revives only a cheap Monster (cost 3 or less at level 2)
        auto st = duel();
        st.graveyard.player = {duel_cards::graveyard_entry(inst("NIH-01", 2)), duel_cards::graveyard_entry(inst("STO-01", 2))};
        st.hand.player = {inst("UTI-07", 2)};
        st.insight.player = 10;
        CHECK(be::play_card(st, Side::Player, 0).ok);
        bool sto = false, nih = false;
        for (const auto& c : st.field.player) { if (c.card_id == "STO-01") sto = true; if (c.card_id == "NIH-01") nih = true; }
        CHECK(sto && !nih && st.graveyard.player.size() == 1);
    }
}
static void t_sacrifice() {
    auto st = duel();
    st.lp.player = 10;
    st.field.player = {inst("STO-01", 2), inst("NIH-07", 2)};
    const int strong_atk = st.field.player[1].atk;
    st.hand.player = {inst("UTI-05", 2)};                                  // Greater Good
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    CHECK(st.graveyard.player.size() == 1 && st.graveyard.player[0].card_id == "STO-01");
    const int buff = E().get_card("UTI-05")->abilities.at(0).ops.at(0).n.at(2), healed = E().get_card("UTI-05")->abilities.at(0).ops.at(0).m.at(2);
    CHECK(st.field.player.size() == 2 && st.field.player[0].atk == strong_atk + buff);
    CHECK(st.field.player[1].atk == st.field.player[1].base_atk);          // the sacrificing card is not buffed
    CHECK(st.lp.player == 10 + healed);
}
static void t_turn_triggers() {
    auto st = duel();
    st.field.player = {inst("HED-06", 2), inst("HED-04", 2)};              // Blossoming (end of turn) / Slow Burn (start of turn)
    const int atk0 = st.field.player[0].atk;
    be::end_turn(st);
    CHECK(st.field.player[0].atk == atk0 + op_n("HED-06", 0, 0, 2));
    be::end_turn(st);                                                       // back to the player: Slow Burn fires
    CHECK(st.lp.bot == 20 - op_n("HED-04", 0, 0, 2));
}
static void t_coin_effects() {
    int heads = 0, tails = 0;
    for (int seed = 0; seed < 40; ++seed) {
        auto run_once = [&](int s) {
            seed_rng(static_cast<std::uint64_t>(500 + s));
            auto st = duel();
            st.hand.player = {inst("ABS-08", 2)};                          // Windfall Gambit
            st.insight.player = 10;
            be::play_card(st, Side::Player, 0);
            return std::make_pair(st.lp.bot, st.lp.player);
        };
        const auto a = run_once(seed), b = run_once(seed);
        CHECK(a == b);                                                      // same seed, same coin
        if (a.first < 20) ++heads; else ++tails;
    }
    CHECK(heads > 5 && tails > 5);
}
static void t_return_self() {
    int returned = 0;
    for (int i = 0; i < 40; ++i) {
        seed_rng(static_cast<std::uint64_t>(900 + i));
        auto st = duel();
        st.field.player = {inst("ABS-03", 2)};                              // Paper Lifeboat: 50% to return to hand when destroyed
        st.field.bot = {ready("NIH-07", 3)};
        st.active_side = Side::Bot;
        const auto hand_before = st.hand.player.size();
        CHECK(be::attack(st, Side::Bot, st.field.bot[0].instance_id, st.field.player[0].instance_id).ok);
        if (st.hand.player.size() == hand_before + 1) { ++returned; CHECK(st.graveyard.player.empty()); }
    }
    CHECK(returned > 5 && returned < 35);
}
static void t_events_recorded() {
    auto quiet = duel();
    quiet.hand.player = {inst("STO-01", 2)};
    quiet.insight.player = 10;
    be::play_card(quiet, Side::Player, 0);
    CHECK(quiet.events.empty());                                            // recording is off by default
    auto st = duel();
    st.record = true;
    st.hand.player = {inst("STO-01", 2)};
    st.insight.player = 10;
    CHECK(be::play_card(st, Side::Player, 0).ok);
    bool summon = false, ability = false, shield = false;
    for (const auto& e : st.events) {
        if (e.type == be::Ev::Summon) summon = true;
        if (e.type == be::Ev::Ability) ability = true;
        if (e.type == be::Ev::ShieldGain) shield = e.amount == st.shield.player;
    }
    CHECK(summon && ability && shield);
    const auto final_board = be::snapshot(st);
    const auto& last = st.events.back().after;
    CHECK(last.shield.player == final_board.shield.player && last.field.player.size() == final_board.field.player.size());
    CHECK(last.insight.player == final_board.insight.player && last.hand.player.size() == final_board.hand.player.size());
}
static void t_bot_uses_grave() {
    auto st = duel();
    st.active_side = Side::Bot;
    st.hand.bot.clear();
    st.graveyard.bot = {duel_cards::graveyard_entry(inst("NIH-04", 2))};
    st.insight.bot = 4;
    bot_ai::take_turn(st, Side::Bot, bot_ai::Difficulty::Hard);
    CHECK(st.banished.bot.size() == 1 && st.graveyard.bot.empty());
}

std::string g_data_dir;

int main(int argc, char** argv) {
    g_data_dir = argc > 1 ? argv[1] : "data";
    for (const char* cand : {"data", "../data", "../../data"})
        if (argc <= 1 && fs::exists(std::string(cand) + "/cards.json")) { g_data_dir = cand; break; }
    CardEngine engine(g_data_dir);
    g_engine = &engine;
    seed_rng(12345);

    run("no-repeat draw + reshuffle-on-exhaustion.", t_no_repeat_draw_real);
    run("Wisdom Meter caps at 100% while lifetime_points stays uncapped.", t_wisdom_cap);
    run("evaluator correct/incorrect + case-insensitive.", t_evaluator);
    run("all 60 cards have valid structure incl. type/trigger.", t_all_cards_valid);
    run("correct answers are exactly 15/15/15/15 across A/B/C/D.", t_letter_balance);
    run("card-type split is 42 Monster / 12 Spell / 6 Trap.", t_type_totals);
    run("collection obtain + level-up capped at 3.", t_collection_levels);
    run("Duel unlocks at exactly 40 unique obtained cards.", t_duel_unlock);
    run("multi-deck create/rename/delete/set-active all work correctly.", t_multi_deck);
    run("decks are stored per-profile and never leak across profiles.", t_decks_isolated);
    run("deck builder enforces max-3-copies and owned-only rules.", t_deck_builder_rules);
    run("a legally built 40-card deck validates cleanly.", t_full_deck_validate);
    run("start_duel + begin_match produce correct opening state (5/5 hands).", t_start_duel_basic);
    run("first mover skips turn-1 draw; second mover draws normally on turn 1.", t_second_player_draws);
    run("hand size is uncapped (no discard-on-full-hand).", t_no_hand_limit);
    run("play_card deducts Insight and a Shield effect grants Shield.", t_play_card_deducts);
    run("Shield absorbs damage before Life Points are reduced.", t_shield_absorbs);
    run("combat trade damages both sides, destroys the loser, and sends it to the Graveyard.", t_combat_trade);
    run("reducing LP to 0 correctly ends the Duel.", t_lp_zero);
    run("drawing from an empty deck causes a fatigue loss.", t_fatigue);
    run("Indulge grants Rush and its ATK boost decays next turn.", t_indulge);
    run("a Monster's Insight cost never decreases as it levels up.", t_cost_monotonic);
    run("Spells resolve immediately, never occupy a field slot, then go to the Graveyard.", t_spell_resolves);
    run("a Set Trap activates on the opponent's attack and negates it.", t_trap_on_attack);
    run("a Set Trap activates when the opponent summons a monster.", t_trap_on_summon);
    run("Revive brings back a Graveyard monster once, and never the same one twice.", t_revive_once);
    run("Banish removes a monster to a separate zone, not the Graveyard (unrevivable).", t_banish);
    run("Mulligan returns chosen cards and redraws the same count, hand size unchanged.", t_mulligan);
    run("every one of the 60 cards has its own cost/stats/effect combination, tag and name.", t_cards_unique);
    run("low-cost cards are plentiful and base stats rise with cost.", t_cost_curve);
    run("card text prints the exact numbers of the card's level (no vague wording).", t_text_exact);
    run("level 2 adds 1 ATK, level 3 adds 1 HP, and the Insight cost stays fixed.", t_level_stats);
    run("a 'when destroyed' effect triggers when the monster is destroyed in battle.", t_death_trigger);
    run("a 'when milled' effect triggers when the card is sent from the deck to the Graveyard.", t_mill_trigger);
    run("Graveyard effects: cost, banish, own-turn-only, and the usable count that lights the pile.", t_grave_effect);
    run("a monster can reveal and destroy the opponent's Set Traps.", t_destroy_traps);
    run("a Spell can stop the opponent's Traps for the rest of the turn.", t_block_traps);
    run("a frozen monster misses exactly one attack turn.", t_freeze);
    run("a silenced monster's 'when destroyed' ability is negated.", t_silence);
    run("Traps can negate a summoned monster's effect and negate (and banish) a Spell.", t_negate_trap_effects);
    run("stealing from the enemy Graveyard and cost-limited revives work.", t_steal_and_revive_limits);
    run("sacrificing a weaker monster buffs the team and heals.", t_sacrifice);
    run("start-of-turn and end-of-turn abilities fire for the right side.", t_turn_triggers);
    run("coin-flip effects are reproducible per seed and land both ways.", t_coin_effects);
    run("a card that may return to hand when destroyed does so about half the time.", t_return_self);
    run("recording is off by default; when on, events carry the table state after each change.", t_events_recorded);
    run("the bot spends leftover Insight on Graveyard effects.", t_bot_uses_grave);
    run("Tutor Report table rows align in the same columns.", t_report_alignment);
    run("all 3 preset decks are exactly 40 legal cards.", t_presets_legal);
    run("matches with the full mechanic set always terminate cleanly.", t_matches_terminate);
    run("bot decks are always exactly 40 cards, <=3 copies.", t_bot_decks);
    run("difficulty differentiation still comes from decision quality alone.", t_harder_ai_wins);
    run("ProfileManager save/load (incl. multi-deck fields) and log round-trip correctly.", t_profile_roundtrip);
    run("a corrupted save file is set aside and replaced, never a crash.", t_corrupted_save);
    run("save files written by the Python version load unchanged.", t_python_save_compat);

    for (const auto& d : temp_dirs()) { std::error_code ec; fs::remove_all(d, ec); }
    std::printf("\n%s (%d/%d checks passed)\n", g_failed ? "SOME CHECKS FAILED." : "ALL CHECKS PASSED.", g_total - g_failed, g_total);
    return g_failed ? 1 : 0;
}
