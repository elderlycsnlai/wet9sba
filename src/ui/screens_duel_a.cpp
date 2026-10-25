// Duel Mode, part A: menus, deck list / deck editor, difficulty, presets, intro and mulligan.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.hpp"

namespace fd::ui {
namespace be = fd::battle;

static bool esc(App& a) { return !a.typing() && a.key(KEY_ESCAPE); }
static bool back_btn(App& a, const char* label = "<  Back  (Esc)") { return a.button(R(40, kH - 56, 170, 40), label, BtnStyle::Normal, true, 18); }

// Title + subtitle of a big menu button. The text colour is chosen from the button's fill so it is always readable.
static void big_button_text(App& a, Rectangle r, const std::string& title, const std::string& sub, bool enabled,
                            Color fill = Color{42, 50, 74, 255}) {
    const Color on = a.on_color(fill);
    a.text(title, r.x + 26, r.y + 16, 25, enabled ? on : col::muted, true);
    a.text(a.fit(sub, 17, r.width - 52, true), r.x + 26, r.y + 54, 17, enabled ? alpha(on, 240) : col::muted, true);
}

// ------------------------------------------------------------------ duel menu
void App::screen_duel_menu() {
    deck_builder::ensure(profile_);
    title_bar("Duel Mode", "Play the cards you have collected against a bot opponent.");
    const int cards = collection::collection_size(profile_);
    if (!collection::duel_unlocked(profile_)) {
        const Rectangle p = R(240, 150, 800, 330);
        panel(p);
        text_c("Duel Mode is locked", R(p.x, p.y + 28, p.width, 46), 38, col::warn, true);
        text_wrapped("Collect at least " + std::to_string(collection::COLLECTION_UNLOCK_THRESHOLD) + " different cards by answering scenarios correctly, then come back and build a deck.",
                     R(p.x + 60, p.y + 100, p.width - 120, 80), 20, col::text);
        bar(R(p.x + 60, p.y + 200, p.width - 120, 22), cards / static_cast<float>(collection::COLLECTION_UNLOCK_THRESHOLD), col::accent);
        text_c(std::to_string(cards) + " / " + std::to_string(collection::COLLECTION_UNLOCK_THRESHOLD) + " cards", R(p.x, p.y + 236, p.width, 30), 20, col::muted, true);
        if (back_btn(*this) || esc(*this)) screen_ = Screen::MainMenu;
        return;
    }
    const std::string active = profile_.active_deck;
    const auto val = deck_builder::validate_full(profile_, *engine_);
    const bool ready = !active.empty() && val.legal;
    const int size = active.empty() ? 0 : deck_builder::deck_size(profile_, active);

    Rectangle r1 = R(40, 116, 580, 104), r2 = R(40, 236, 580, 104), r3 = R(40, 356, 580, 104);
    if (button(r1, "", ready ? BtnStyle::Good : BtnStyle::Normal, ready)) {
        defer([this] { begin_duel_setup(""); });
    }
    big_button_text(*this, r1, "Duel with my active deck",
                    active.empty() ? "You don't have a deck yet - build one first."
                    : ready ? active + "   -   " + std::to_string(size) + "/40 cards, ready!"
                            : active + " needs exactly 40 cards (it has " + std::to_string(size) + ").", ready,
                    ready ? Color{32, 106, 68, 255} : Color{28, 32, 46, 255});
    if (button(r2, "", BtnStyle::Normal)) screen_ = Screen::Presets;
    big_button_text(*this, r2, "Duel with a preset deck", "Ready-made decks - no building needed.", true);
    if (button(r3, "", BtnStyle::Normal)) screen_ = Screen::DeckList;
    big_button_text(*this, r3, "Manage decks", "Build, rename and choose your decks.", true);

    const Rectangle side = R(660, 116, 580, 344);
    panel(side);
    text("Your decks", side.x + 22, side.y + 16, 22, col::text, true);
    float y = side.y + 56;
    if (profile_.decks.empty()) text("No decks yet.", side.x + 22, y, 17, col::muted);
    for (const auto& d : profile_.decks) {
        if (y > side.y + side.height - 40) break;
        const int n = static_cast<int>(d.cards.size());
        text(fit(d.name, 18, 300, true), side.x + 22, y, 18, col::text, true);
        text(std::to_string(n) + "/40", side.x + 340, y + 1, 17, n == 40 ? col::good : col::warn, true);
        if (d.name == active) text("ACTIVE", side.x + 430, y + 2, 14, col::accent, true);
        y += 34;
    }
    if (back_btn(*this) || esc(*this)) screen_ = Screen::MainMenu;
}

void App::begin_duel_setup(const std::string& preset_id) {
    DuelUI& d = duel_;
    d.preset_id = preset_id;
    d.player_deck.clear();
    if (preset_id.empty()) {
        d.deck_label = profile_.active_deck;
        for (const auto& id : deck_builder::get_active_deck_cards(profile_))
            d.player_deck.push_back({id, std::max(1, collection::level_of(profile_, id))});
    } else {
        const auto* p = preset_decks::get_preset(preset_id);
        d.deck_label = (p ? p->name : preset_id) + " (preset)";
        d.player_deck = preset_decks::build_preset_deck(*engine_, preset_id);
    }
    screen_ = Screen::Difficulty;
}

void App::screen_difficulty() {
    title_bar("Choose a difficulty", "The bot always fights with the same cards - harder bots just play them better.");
    struct Opt { bot_ai::Difficulty d; const char* name; const char* desc; Color c; };
    const Opt opts[3] = {
        {bot_ai::Difficulty::Easy, "Easy", "Plays and attacks almost at random, and sometimes skips its own attacks. Great for learning the rules.", col::good},
        {bot_ai::Difficulty::Normal, "Normal", "Always plays its most expensive card and only attacks when the trade is safe for it.", col::warn},
        {bot_ai::Difficulty::Hard, "Hard", "Goes for lethal when it can, picks value trades, and holds back Rally until it has a team.", col::bad},
    };
    for (int i = 0; i < 3; ++i) {
        const Rectangle r = R(40 + i * 408.0f, 130, 384, 360);
        const bool hov = hover(r);
        if (hov) hot_ = true;
        panel(r, hov ? Color{44, 52, 78, 255} : col::panel, hov ? opts[i].c : col::border, 12, hov ? 2.5f : 1.5f);
        text_c(opts[i].name, R(r.x, r.y + 30, r.width, 44), 40, opts[i].c, true);
        text_wrapped(opts[i].desc, R(r.x + 28, r.y + 110, r.width - 56, 160), 19, col::text);
        text_c("Click to choose", R(r.x, r.y + r.height - 56, r.width, 30), 17, col::muted);
        if (hov && in_.pressed) {
            duel_.diff = opts[i].d;
            defer([this] { launch_duel(); });
        }
    }
    text("Deck: " + duel_.deck_label, 40, 520, 20, col::muted, true);
    if (back_btn(*this) || esc(*this)) screen_ = duel_.preset_id.empty() ? Screen::DuelMenu : Screen::Presets;
}

void App::screen_presets() {
    title_bar("Preset decks", "Each preset pairs two schools - all 40 cards are Level 2.");
    const auto& list = preset_decks::presets();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Rectangle r = R(40, 116 + i * 148.0f, 1200, 134);
        const bool hov = hover(r);
        if (hov) hot_ = true;
        panel(r, hov ? Color{44, 52, 78, 255} : col::panel, hov ? col::accent : col::border, 12, hov ? 2.5f : 1.5f);
        text(list[i].name, r.x + 28, r.y + 18, 28, col::text, true);
        float ix = r.x + 28;
        for (const auto& sid : list[i].schools) {
            school_icon(sid, ix + 10, r.y + 66, 9, school_color(sid));
            const School* sc = engine_->get_school(sid);
            text(sc ? sc->name : sid, ix + 26, r.y + 56, 17, school_color(sid), true);
            ix += 40 + text_w(sc ? sc->name : sid, 17, true);
        }
        text_wrapped(list[i].description, R(r.x + 28, r.y + 88, r.width - 260, 40), 16, col::muted);
        text_r("Choose  >", r.x + r.width - 28, r.y + r.height / 2 - 12, 22, hov ? col::accent : col::muted, true);
        if (hov && in_.pressed) { const std::string id = list[i].id; defer([this, id] { begin_duel_setup(id); }); }
    }
    if (back_btn(*this) || esc(*this)) screen_ = Screen::DuelMenu;
}

// ---------------------------------------------------------------------- decks
void App::screen_deck_list() {
    deck_builder::ensure(profile_);
    DeckUI& u = deck_ui_;
    title_bar("Your decks", "A deck needs exactly 40 cards, with up to 3 copies of any one card.");
    const bool modal = u.modal != DeckUI::Modal::None;
    blocked_ = modal;
    const Rectangle area = R(40, 100, 1200, 500);
    scroll_area(area, static_cast<float>(profile_.decks.size()) * 76, u.scroll);
    begin_clip(area);
    if (profile_.decks.empty()) text("You don't have any decks yet. Press \"New deck\" to start one.", area.x, area.y + 20, 20, col::muted);
    for (std::size_t i = 0; i < profile_.decks.size(); ++i) {
        const Deck deck = profile_.decks[i];
        const Rectangle r = R(area.x, area.y + i * 76 - u.scroll, area.width - 14, 66);
        if (r.y + r.height < area.y || r.y > area.y + area.height) continue;
        const bool active = deck.name == profile_.active_deck;
        panel(r, col::panel, active ? col::accent : col::border, 10, active ? 2.5f : 1.5f);
        text(fit(deck.name, 24, 400, true), r.x + 22, r.y + 10, 24, col::text, true);
        const int n = static_cast<int>(deck.cards.size());
        text(std::to_string(n) + " / 40 cards" + (n == 40 ? "   ready" : n > 40 ? "   too many" : "   needs " + std::to_string(40 - n) + " more"),
             r.x + 22, r.y + 40, 15, n == 40 ? col::good : col::warn, true);
        if (active) text("ACTIVE", r.x + 440, r.y + 22, 16, col::accent, true);
        float bx = r.x + r.width - 4;
        const float bw = 104, bh = 40, by = r.y + 13;
        bx -= bw; if (button(R(bx, by, bw, bh), "Delete", BtnStyle::Danger, true, 16)) { u.modal = DeckUI::Modal::ConfirmDelete; u.target = deck.name; }
        bx -= bw + 8; if (button(R(bx, by, bw, bh), "Rename", BtnStyle::Normal, true, 16)) { u.modal = DeckUI::Modal::Rename; u.target = deck.name; u.name_field.text = deck.name; focus_ = &u.name_field; }
        bx -= bw + 8; if (button(R(bx, by, bw + 20, bh), "Set active", BtnStyle::Normal, !active, 16)) { deck_builder::set_active_deck(profile_, deck.name); pm_->save_profile(profile_); }
        bx -= bw + 28; if (button(R(bx, by, bw, bh), "Edit", BtnStyle::Primary, true, 16)) { u.editing = deck.name; u.scroll = 0; defer([this] { deck_ui_.scroll = 0; screen_ = Screen::DeckEditor; }); }
    }
    end_clip();
    if (button(R(kW - 40 - 220, kH - 56, 220, 40), "+  New deck", BtnStyle::Good, true, 18)) { u.modal = DeckUI::Modal::NewDeck; u.name_field.text.clear(); focus_ = &u.name_field; }
    if (!modal && (back_btn(*this) || esc(*this))) screen_ = Screen::DuelMenu;

    if (modal) {
        blocked_ = false;
        dim_background(190);
        const Rectangle box = R(380, 220, 520, 250);
        panel(box, col::panel, col::accent, 12, 2.0f);
        if (u.modal == DeckUI::Modal::ConfirmDelete) {
            text_c("Delete \"" + fit(u.target, 22, 380, true) + "\"?", R(box.x, box.y + 30, box.width, 34), 24, col::text, true);
            text_c("This can't be undone. Your cards stay in your collection.", R(box.x, box.y + 80, box.width, 24), 16, col::muted);
            if (button(R(box.x + 40, box.y + 160, 200, 50), "Delete", BtnStyle::Danger, true, 20)) {
                deck_builder::delete_deck(profile_, u.target);
                pm_->save_profile(profile_);
                u.modal = DeckUI::Modal::None;
            }
            if (button(R(box.x + 280, box.y + 160, 200, 50), "Keep it", BtnStyle::Normal, true, 20) || key(KEY_ESCAPE)) u.modal = DeckUI::Modal::None;
        } else {
            const bool rename = u.modal == DeckUI::Modal::Rename;
            text_c(rename ? "Rename deck" : "Name your new deck", R(box.x, box.y + 24, box.width, 34), 26, col::text, true);
            const bool sub = text_field(R(box.x + 40, box.y + 80, box.width - 80, 50), u.name_field, "Deck name");
            const bool ok = !trim(u.name_field.text).empty();
            if (button(R(box.x + 40, box.y + 164, 200, 50), rename ? "Rename" : "Create", BtnStyle::Primary, ok, 20) || (sub && ok)) {
                deck_builder::Result res = rename ? deck_builder::rename_deck(profile_, u.target, u.name_field.text) : deck_builder::create_deck(profile_, u.name_field.text);
                if (!res.ok) toast(res.reason);
                else {
                    pm_->save_profile(profile_);
                    const std::string nm = trim(u.name_field.text);
                    u.modal = DeckUI::Modal::None;
                    focus_ = nullptr;
                    if (!rename) { u.editing = nm; u.scroll = 0; screen_ = Screen::DeckEditor; }
                }
            }
            if (button(R(box.x + 280, box.y + 164, 200, 50), "Cancel", BtnStyle::Normal, true, 20) || key(KEY_ESCAPE)) { u.modal = DeckUI::Modal::None; focus_ = nullptr; }
        }
    }
}

void App::screen_deck_editor() {
    DeckUI& u = deck_ui_;
    const Deck* deck = deck_builder::find_deck(profile_, u.editing);
    if (!deck) { screen_ = Screen::DeckList; return; }
    const int size = deck_builder::deck_size(profile_, u.editing);
    title_bar("Deck: " + u.editing, "Add cards with +  /  remove with -.  Max 3 copies of each card, exactly 40 to duel.");
    const auto counts = deck_builder::deck_counts(profile_, u.editing);
    u.hover = CardView{};

    // owned cards, grouped by school
    std::vector<const Card*> owned;
    for (const auto& c : engine_->cards()) if (profile_.collection.count(c.id)) owned.push_back(&c);
    std::stable_sort(owned.begin(), owned.end(), [&](const Card* a, const Card* b) {
        if (a->school_id != b->school_id) return a->school_id < b->school_id;
        return a->id < b->id;
    });
    const Rectangle area = R(40, 138, 790, 508);
    panel(R(40, 100, 790, 34), col::panel2, col::border, 8);
    text("Card", 84, 108, 15, col::muted, true);
    text("Lv", 330, 108, 15, col::muted, true);
    text("Type", 372, 108, 15, col::muted, true);
    text("Cost", 452, 108, 15, col::muted, true);
    text("ATK/HP", 510, 108, 15, col::muted, true);
    text("Effect", 590, 108, 15, col::muted, true);
    text("In deck", 712, 108, 15, col::muted, true);
    scroll_area(area, static_cast<float>(owned.size()) * 36, u.scroll);
    const bool inside = CheckCollisionPointRec(in_.mouse, area);
    begin_clip(area);
    const bool saved_block = blocked_;
    blocked_ = blocked_ || !inside;
    float y = area.y - u.scroll;
    for (const Card* c : owned) {
        const Rectangle r = R(area.x, y, area.width - 10, 33);
        y += 36;
        if (r.y + r.height < area.y || r.y > area.y + area.height) continue;
        const int level = collection::level_of(profile_, c->id);
        const auto st = duel_cards::stats_at_level(*c, level);
        const int n = counts.count(c->id) ? counts.at(c->id) : 0;
        const bool hov = hover(r);
        panel(r, n > 0 ? Color{34, 48, 72, 255} : col::panel, hov ? col::accent : n > 0 ? Color{70, 100, 150, 255} : Color{40, 46, 64, 255}, 7, hov ? 2.0f : 1.0f);
        school_icon(c->school_id, r.x + 20, r.y + 16, 8, school_color(c->school_id));
        text(fit(c->collectible, 16, 230, true), r.x + 44, r.y + 7, 16, col::text, true);
        text(std::to_string(level), r.x + 292, r.y + 8, 16, col::warn, true);
        text(duel_cards::type_label(c->card_type), r.x + 332, r.y + 8, 15, type_color(c->card_type), true);
        text(std::to_string(st.cost), r.x + 414, r.y + 8, 16, col::accent, true);
        text(c->card_type == CardType::Monster ? std::to_string(st.atk) + "/" + std::to_string(st.hp) : "-", r.x + 470, r.y + 8, 15, col::text);
        text(fit(c->tag, 14, 120, true), r.x + 550, r.y + 9, 14, Color{200, 208, 230, 255}, true);
        text(std::to_string(n) + "/3", r.x + 672, r.y + 8, 16, n > 0 ? col::good : col::muted, true);
        if (button(R(r.x + 706, r.y + 3, 32, 27), "-", BtnStyle::Normal, n > 0, 18)) {
            deck_builder::remove_card(profile_, u.editing, c->id);
            pm_->save_profile(profile_);
        }
        if (button(R(r.x + 742, r.y + 3, 32, 27), "+", BtnStyle::Primary, n < 3 && size < 40, 18)) {
            auto res = deck_builder::add_card(profile_, u.editing, c->id);
            if (!res.ok) toast(res.reason); else pm_->save_profile(profile_);
        }
        if (hov) u.hover = view_of(*c, level);
    }
    blocked_ = saved_block;
    end_clip();

    // right panel: summary
    const Rectangle sp = R(850, 100, 390, 262);
    panel(sp);
    text("Deck size", sp.x + 20, sp.y + 14, 18, col::muted, true);
    text(std::to_string(size) + " / 40", sp.x + 20, sp.y + 38, 34, size == 40 ? col::good : col::text, true);
    bar(R(sp.x + 20, sp.y + 84, sp.width - 40, 14), size / 40.0f, size == 40 ? col::good : col::accent);
    int mon = 0, spl = 0, trp = 0;
    std::array<int, 11> curve{};
    for (const auto& [id, n] : counts) {
        const Card* c = engine_->get_card(id);
        if (!c) continue;
        (c->card_type == CardType::Monster ? mon : c->card_type == CardType::Spell ? spl : trp) += n;
        const int cost = duel_cards::stats_at_level(*c, std::max(1, collection::level_of(profile_, id))).cost;
        curve[std::min(10, std::max(1, cost))] += n;
    }
    text("Monsters " + std::to_string(mon), sp.x + 20, sp.y + 112, 16, type_color(CardType::Monster), true);
    text("Spells " + std::to_string(spl), sp.x + 150, sp.y + 112, 16, type_color(CardType::Spell), true);
    text("Traps " + std::to_string(trp), sp.x + 266, sp.y + 112, 16, type_color(CardType::Trap), true);
    const int peak = std::max(1, *std::max_element(curve.begin(), curve.end()));
    for (int c = 1; c <= 10; ++c) {
        const float bx = sp.x + 22 + (c - 1) * 35.5f;
        const float bh = 58.0f * curve[c] / peak;
        DrawRectangleRounded(R(bx, sp.y + 208 - bh, 26, std::max(2.0f, bh)), 0.3f, 4, curve[c] ? col::accent : Color{44, 50, 70, 255});
        text_c(std::to_string(c), R(bx - 4, sp.y + 214, 34, 18), 13, col::muted);
        if (curve[c]) text_c(std::to_string(curve[c]), R(bx - 4, sp.y + 208 - bh - 18, 34, 16), 13, col::text, true);
    }
    text("Insight cost curve", sp.x + 20, sp.y + 136, 13, col::muted);

    const bool legal = deck_builder::validate_full(profile_, *engine_, u.editing).legal;
    text(legal ? "Battle-ready!" : "Needs exactly 40 cards.", 850, 372, 18, legal ? col::good : col::warn, true);
    const bool is_active = profile_.active_deck == u.editing;
    if (button(R(850, 402, 190, 40), is_active ? "Active deck" : "Set as active", BtnStyle::Good, !is_active, 17)) { deck_builder::set_active_deck(profile_, u.editing); pm_->save_profile(profile_); }
    if (button(R(1050, 402, 190, 40), "Clear deck", BtnStyle::Danger, size > 0, 17)) { deck_builder::clear_deck(profile_, u.editing); pm_->save_profile(profile_); }
    draw_detail(R(850, 456, 390, 230), u.hover.valid ? &u.hover : nullptr, "Hover over a card to read its effect.");
    if (back_btn(*this, "<  Decks  (Esc)") || esc(*this)) screen_ = Screen::DeckList;
}

// ---------------------------------------------------------------- duel setup
void App::launch_duel() {
    DuelUI& d = duel_;
    try {
        d.first = bot_ai::choose_starting_side();
        d.st = be::start_duel(d.player_deck, bot_ai::build_bot_deck(*engine_), *engine_, d.first);
    } catch (const std::exception& e) {
        toast(e.what());
        screen_ = Screen::DuelMenu;
        return;
    }
    d.st.trap_resolver = [this](be::DuelState& s, Side defender, const std::vector<CardInstance>& eligible) -> std::string {
        if (defender == Side::Bot) return bot_ai::bot_trap_decision(s, defender, eligible);
        return ask_trap_modal(s, eligible);
    };
    d.sel_hand = -1;
    d.sel_attacker.clear();
    d.pinned = CardView{};
    d.hover = CardView{};
    d.overlay = DuelOverlay::None;
    d.bot_waiting = false;
    d.intro_t = 0;
    d.log_scroll = d.hist_scroll = d.gloss_scroll = 0;
    d.summary_lines.clear();
    d.conceded = false;
    d.mull_marks.assign(d.st.hand.player.size(), false);
    screen_ = Screen::DuelIntro;
}

void App::screen_duel_intro() {
    DuelUI& d = duel_;
    title_bar("Get ready", "");
    const Rectangle p = R(240, 140, 800, 380);
    panel(p);
    text_c("Duel vs the bot", R(p.x, p.y + 28, p.width, 44), 38, col::text, true);
    text_c(std::string("Difficulty: ") + bot_ai::difficulty_name(d.diff), R(p.x, p.y + 90, p.width, 30), 22, d.diff == bot_ai::Difficulty::Easy ? col::good : d.diff == bot_ai::Difficulty::Normal ? col::warn : col::bad, true);
    text_c("Your deck: " + d.deck_label, R(p.x, p.y + 130, p.width, 28), 20, col::muted, true);
    d.intro_t += frame_dt();                                  // the coin toss is played out, not just announced
    const float u = std::min(1.0f, d.intro_t / 1.5f);
    draw_coin({p.x + p.width / 2 - 250, p.y + 214}, 26, u, d.first == Side::Player);
    text_c(u < 1.0f ? "Flipping the coin..." : d.first == Side::Player ? "Coin toss: you go first!" : "Coin toss: the opponent goes first.",
           R(p.x + 120, p.y + 190, p.width - 120, 34), 28, u < 1.0f ? col::muted : col::accent, true);
    text_wrapped("Win by reducing the opponent's Life Points to 0. Each turn you gain 1 more Insight to spend on cards. Monsters you play can't attack until your next turn (except Rush monsters).",
                 R(p.x + 60, p.y + 250, p.width - 120, 70), 17, col::muted);
    if (button(R(p.x + 240, p.y + p.height - 70, 320, 52), "Draw opening hand", BtnStyle::Primary, true, 22) || (!typing() && key(KEY_ENTER))) {
        d.mull_marks.assign(d.st.hand.player.size(), false);
        screen_ = Screen::Mulligan;
    }
}

void App::screen_mulligan() {
    DuelUI& d = duel_;
    auto& hand = d.st.hand.player;
    title_bar("Opening hand", "Click any cards you want to return to the deck and redraw (optional).");
    if (d.mull_marks.size() != hand.size()) d.mull_marks.assign(hand.size(), false);
    const float cw = 178, ch = 262, gap = 22;
    const float total = hand.size() * cw + (hand.size() - 1) * gap;
    float x = (kW - total) / 2;
    CardView hv;
    for (std::size_t i = 0; i < hand.size(); ++i) {
        const Rectangle r = R(x, 126, cw, ch);
        const bool hov = hover(r);
        if (hov) hot_ = true;
        CardDraw cd;
        cd.hovered = hov;
        cd.selected = d.mull_marks[i];
        const CardView v = view_of(hand[i]);
        draw_card_face(R(r.x, r.y - (d.mull_marks[i] ? 0 : 0), r.width, r.height), v, cd);
        if (d.mull_marks[i]) {
            DrawRectangleRounded(R(r.x, r.y + r.height / 2 - 20, r.width, 40), 0.2f, 4, alpha(col::warn, 230));
            text_c("RETURN", R(r.x, r.y + r.height / 2 - 20, r.width, 40), 22, col::black, true);
        }
        if (hov) hv = v;
        if (hov && in_.pressed) d.mull_marks[i] = !d.mull_marks[i];
        x += cw + gap;
    }
    draw_detail(R(300, 410, 680, 190), hv.valid ? &hv : nullptr, "Hover over a card to read its effect.");
    const int marked = static_cast<int>(std::count(d.mull_marks.begin(), d.mull_marks.end(), true));
    const std::string label = marked == 0 ? "Keep this hand" : "Return " + std::to_string(marked) + " card" + (marked == 1 ? "" : "s") + " and redraw";
    if (button(R(kW / 2.0f - 190, 626, 380, 56), label, BtnStyle::Primary, true, 22) || (!typing() && key(KEY_ENTER))) {
        std::vector<int> idx;
        for (std::size_t i = 0; i < d.mull_marks.size(); ++i) if (d.mull_marks[i]) idx.push_back(static_cast<int>(i));
        defer([this, idx] {
            DuelUI& dd = duel_;
            be::mulligan(dd.st, Side::Player, idx);
            be::mulligan(dd.st, Side::Bot, bot_ai::bot_mulligan_indices(dd.st.hand.bot));
            dd.st.record = true;                 // from here on every change is recorded so it can be animated
            duel_anim_reset();                   // the table as it looks before turn 1 begins
            be::begin_match(dd.st, dd.first);
            dd.overlay = DuelOverlay::None;
            screen_ = Screen::Duel;
        });
    }
}

}  // namespace fd::ui
