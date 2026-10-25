// Duel Mode, part B: the duel board (Master Duel-style zones), overlays, trap prompt and bot turn.
// The board is drawn from d.shown -- a copy of the table that follows the engine's recorded events
// (see anim.cpp) -- so every move, including the opponent's, is played out instead of announced in a pop-up.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.hpp"
#include "rlgl.h"

namespace fd::ui {
namespace be = fd::battle;
namespace L = fd::ui::lay;

static std::string side_label(Side s) { return s == Side::Player ? "Your" : "Opponent's"; }

// ----- actions are queued and run after the frame has been drawn (never while iterating game state)
static void after_player_action(App& a) {
    a.duel_.sel_hand = -1;
    a.duel_.sel_attacker.clear();
}
static void queue_attack(App& a, const std::string& id, const std::string& target) {
    a.defer([&a, id, target] {
        auto r = be::attack(a.duel_.st, Side::Player, id, target);
        if (!r.ok) a.toast(r.reason);
        after_player_action(a);
    });
}
static void queue_play(App& a, int idx) {
    a.defer([&a, idx] {
        auto r = be::play_card(a.duel_.st, Side::Player, idx);
        if (!r.ok) a.toast(r.reason);
        after_player_action(a);
    });
}
static void queue_end_turn(App& a) {
    a.defer([&a] {
        be::end_turn(a.duel_.st);
        after_player_action(a);
    });
}
static void queue_grave(App& a, int idx) {
    a.defer([&a, idx] {
        auto r = be::activate_grave(a.duel_.st, Side::Player, idx);
        if (!r.ok) a.toast(r.reason);
        after_player_action(a);
    });
}

static Color log_color(const App& a, const std::string& s) {
    if (s.rfind("---", 0) == 0) return col::accent;
    if (s.find("Trap") != std::string::npos) return a.type_color(CardType::Trap);
    if (s.find("damage") != std::string::npos || s.find("loses") != std::string::npos) return Color{250, 150, 140, 255};
    if (s.find("destroyed") != std::string::npos || s.find("banish") != std::string::npos || s.find("Banish") != std::string::npos) return col::warn;
    if (s.find("wins") != std::string::npos || s.find("draw!") != std::string::npos) return col::good;
    return col::text;
}

void App::finish_duel_if_over() {
    DuelUI& d = duel_;
    if (!d.st.game_over || d.overlay == DuelOverlay::Result || duel_animating()) return;
    const auto& log = d.st.log;
    const std::size_t from = log.size() > 5 ? log.size() - 5 : 0;
    d.summary_lines.assign(log.begin() + static_cast<std::ptrdiff_t>(from), log.end());
    d.overlay = DuelOverlay::Result;
}

void App::bot_turn() {
    DuelUI& d = duel_;
    be::DuelState& st = d.st;
    if (st.game_over || st.active_side != Side::Bot) return;
    bot_ai::take_turn(st, Side::Bot, d.diff);          // everything it does is recorded and then played back as animations
    d.sel_hand = -1;
    d.sel_attacker.clear();
}

// ------------------------------------------------------------------ the board
void App::draw_duel_scene(bool interactive) {
    DuelUI& d = duel_;
    be::DuelState& st = d.st;
    const be::Board& b = d.shown;
    FxState& fx = d.fx;
    const bool saved_blocked = blocked_;
    if (!interactive) blocked_ = true;
    const bool busy = duel_animating();
    const bool my_turn = st.active_side == Side::Player && !st.game_over;
    const bool shown_my_turn = b.active_side == Side::Player && !st.game_over;
    const bool can_act = interactive && my_turn && !busy && d.overlay == DuelOverlay::None && !blocked_;
    d.hover = CardView{};
    std::string tip;
    auto see = [&](Rectangle r, const CardView& v) { if (hover(r)) d.hover = v; };

    float sx = 0, sy = 0;                                // screen shake on heavy hits
    if (fx.shake_t >= 0 && fx.shake_t < fx.shake_dur) {
        const float k = 1.0f - fx.shake_t / fx.shake_dur;
        sx = std::sin(static_cast<float>(now_) * 95.0f) * fx.shake_mag * k;
        sy = std::cos(static_cast<float>(now_) * 81.0f) * fx.shake_mag * k * 0.6f;
    }
    rlPushMatrix();
    rlTranslatef(sx, sy, 0.0f);

    // ---- top bar
    DrawRectangle(0, 0, kW, 34, Color{22, 26, 40, 255});
    DrawRectangle(0, 34, kW, 1, alpha(col::border, 160));
    text("DUEL", 14, 6, 20, col::accent, true);
    const std::string who = st.game_over && !busy ? "Duel over" : shown_my_turn ? "Your turn" : "Opponent's turn";
    text("Turn " + std::to_string(b.turn_number) + "  -  " + who, 80, 8, 17, shown_my_turn ? col::good : st.game_over && !busy ? col::muted : col::warn, true);
    text_c(std::string(bot_ai::difficulty_name(d.diff)) + " bot   |   Deck: " + d.deck_label, R(330, 0, 510, 34), 15, col::muted);
    const char* speed_label = d.speed == 1 ? "Anim: Normal" : d.speed == 2 ? "Anim: Fast" : "Anim: Off";
    if (button(R(848, 4, 106, 26), speed_label, BtnStyle::Normal, true, 14)) d.speed = d.speed == 1 ? 2 : d.speed == 2 ? 0 : 1;
    if (button(R(960, 4, 96, 26), "Effects", BtnStyle::Normal, true, 14)) { d.overlay = DuelOverlay::Glossary; d.gloss_scroll = 0; }
    if (button(R(1062, 4, 96, 26), "History", BtnStyle::Normal, true, 14)) { d.overlay = DuelOverlay::History; d.hist_scroll = 1e9f; }
    if (button(R(1164, 4, 104, 26), "Concede", BtnStyle::Danger, !st.game_over, 14)) d.overlay = DuelOverlay::Concede;

    // ---- HUD strips
    auto draw_hud = [&](Side side) {
        const bool opp = side == Side::Bot;
        const Rectangle r = L::hud(side);
        const float y = r.y;
        const bool target = opp && can_act && !d.sel_attacker.empty();
        const bool hov = target && hover(r);
        if (hov) { hot_ = true; tip = "Attack the opponent directly"; }
        panel(r, col::panel, target ? (hov ? col::bad : mix(col::bad, col::panel, 0.5f)) : col::border, 8, target ? 2.5f : 1.5f);
        text(opp ? "OPPONENT" : "YOU", r.x + 14, y + 10, 16, opp ? col::warn : col::good, true);
        const float lpf = d.lp_disp[side];
        const int lp = static_cast<int>(std::lround(lpf));
        bar(R(r.x + 106, y + 11, 190, 16), lpf / 20.0f, lp > 10 ? col::good : lp > 5 ? col::warn : col::bad);
        text(std::to_string(lp), r.x + 306, y + 7, 22, col::text, true);
        text("LP", r.x + 340, y + 12, 14, col::muted, true);
        const int sh = static_cast<int>(std::lround(d.sh_disp[side]));
        const Color shell = sh ? Color{38, 90, 108, 255} : Color{26, 30, 44, 255};
        DrawRectangleRounded(R(r.x + 378, y + 8, 92, 22), 0.4f, 6, shell);
        text_c("Shield " + std::to_string(sh), R(r.x + 378, y + 8, 92, 22), 14, sh ? Color{170, 238, 250, 255} : col::muted, true);
        const float x0 = r.x + 492;
        for (int i = 0; i < 10; ++i) {
            const Vector2 c{x0 + i * 13.0f, y + 19.0f};
            if (i < b.insight[side]) DrawCircleV(c, 4.8f, col::accent);
            else if (i < b.max_insight[side]) DrawCircleV(c, 4.8f, Color{40, 60, 100, 255});
            else DrawCircleV(c, 2.2f, Color{54, 62, 86, 255});
        }
        text(std::to_string(b.insight[side]) + "/" + std::to_string(b.max_insight[side]) + " Insight", x0 + 10 * 13.0f + 4, y + 11, 14, col::muted, true);
        if (opp) text_r("Hand " + std::to_string(b.hand[side].size()), r.x + r.width - 40, y + 11, 16, col::muted, true);
        if (hov && in_.pressed) queue_attack(*this, d.sel_attacker, "face");
    };
    draw_hud(Side::Bot);
    draw_hud(Side::Player);

    // ---- zone rows: piles (Deck / Graveyard / Banished) + Set traps
    const int grave_fx = my_turn && !busy ? be::usable_grave_count(st, Side::Player) : 0;       // Graveyard effects ready to use
    const int grave_any = be::grave_effect_count(st, Side::Player);
    auto pile = [&](Rectangle r, const char* label, int count, bool clickable, Side side, bool banished, int glow) {
        const bool hov = clickable && hover(r);
        if (hov) {
            hot_ = true;
            tip = side_label(side) + (banished ? " banished zone" : " Graveyard") + " (" + std::to_string(count) + ")" +
                  (glow == 2 ? " - " + std::to_string(grave_fx) + " effect(s) ready to use!" : glow == 1 ? " - holds a card with a Graveyard effect" : "") + " - click to view";
        }
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_) * 5.0f);
        if (glow == 2) DrawRectangleRounded(R(r.x - 7, r.y - 7, r.width + 6, r.height + 6), 0.25f, 8, alpha(col::warn, 40 + static_cast<int>(80 * pulse)));
        if (count > 1) DrawRectangleRounded(R(r.x + 4, r.y + 4, r.width - 4, r.height - 4), 0.15f, 6, Color{30, 35, 54, 255});
        const Rectangle top = R(r.x, r.y, r.width - 4, r.height - 4);
        const Color edge = glow == 2 ? mix(col::warn, col::white, 0.4f * pulse) : glow == 1 ? mix(col::warn, col::panel2, 0.35f) : hov ? col::accent : col::border;
        panel(top, count ? col::panel2 : Color{22, 25, 36, 255}, edge, 7, glow == 2 ? 3.5f : (glow || hov) ? 2.5f : 1.5f);
        text_c(label, R(top.x, top.y + 5, top.width, 15), 12, hov ? col::accent : glow ? col::warn : col::muted, true);
        text_c(std::to_string(count), R(top.x, top.y + 20, top.width, 30), 24, count ? col::text : Color{104, 112, 136, 255}, true);
        if (glow == 2) {
            DrawCircleV({top.x + top.width - 2, top.y + 2}, 10, col::warn);
            text_c(std::to_string(grave_fx), R(top.x + top.width - 12, top.y - 8, 20, 20), 13, on_color(col::warn), true);
        }
        if (hov && in_.pressed) {
            d.overlay = DuelOverlay::Zone;
            d.zone_side = side;
            d.zone_banished = banished;
            d.zone_sel = -1;
            d.zone_scroll = 0;
        }
    };
    auto zone_row = [&](Side side) {
        const bool mine = side == Side::Player;
        pile(L::deck_pile(side), "DECK", b.deck_count[side], false, side, false, 0);
        pile(L::grave_pile(side), "GRAVE", static_cast<int>(b.graveyard[side].size()), true, side, false, mine ? (grave_fx ? 2 : grave_any ? 1 : 0) : 0);
        pile(L::banish_pile(side), "BANISH", static_cast<int>(b.banished[side].size()), true, side, true, 0);
        for (int i = 0; i < battle::TRAP_ZONE_LIMIT; ++i) {
            const Rectangle tr = L::trap_slot(side, i);
            if (i < static_cast<int>(b.traps[side].size()) && b.traps[side][i].instance_id != d.hide_iid) {
                const CardInstance& t = b.traps[side][i];
                const bool hov = hover(tr);
                const bool face_up = mine || t.revealed;
                panel(tr, face_up ? col::panel2 : Color{38, 44, 72, 255}, hov ? col::accent : mix(type_color(CardType::Trap), col::white, 0.2f), 7, hov ? 2.5f : 1.5f);
                DrawRectangle(static_cast<int>(tr.x + 5), static_cast<int>(tr.y + 8), 4, 40, type_color(CardType::Trap));
                if (face_up) {
                    text(fit(t.name, 13, 76, true), tr.x + 14, tr.y + 6, 13, col::text, true);
                    const Card* c = engine_->get_card(t.card_id);
                    text(fit(c ? c->tag : std::string(), 12, 76), tr.x + 14, tr.y + 23, 12, Color{214, 222, 242, 255});
                    text(mine ? "SET TRAP" : "REVEALED", tr.x + 14, tr.y + 39, 11, mine ? type_color(CardType::Trap) : Color{210, 150, 235, 255}, true);
                    see(tr, view_of(t, !mine));
                } else {
                    text_c("?", R(tr.x, tr.y + 2, tr.width, 34), 26, alpha(col::muted, 230), true);
                    text_c("Set Trap", R(tr.x, tr.y + 34, tr.width, 16), 12, type_color(CardType::Trap), true);
                    see(tr, view_of(t, true));
                }
            } else {
                panel(tr, Color{20, 23, 34, 255}, Color{48, 55, 76, 255}, 7, 1.0f);
                text_c("Trap zone", tr, 12, Color{92, 100, 124, 255});
            }
        }
    };
    zone_row(Side::Bot);
    zone_row(Side::Player);

    // ---- monster fields
    auto monster_row = [&](Side side) {
        for (int i = 0; i < battle::FIELD_LIMIT; ++i) {
            const Rectangle mr = L::monster_slot(side, i);
            if (i >= static_cast<int>(b.field[side].size()) || b.field[side][i].instance_id == d.hide_iid) {
                panel(mr, Color{20, 23, 34, 255}, Color{48, 55, 76, 255}, 8, 1.0f);
                text_c("Monster zone", mr, 13, Color{92, 100, 124, 255});
                continue;
            }
            const CardInstance& c = b.field[side][i];
            CardView v = view_of(c);
            v.on_field = true;
            v.show_cost = false;
            CardDraw cd;
            cd.hovered = hover(mr);
            if (side == Side::Player) {
                cd.selected = c.instance_id == d.sel_attacker;
                cd.ready = my_turn && !busy && c.can_attack_this_turn && !cd.selected;
            } else {
                cd.target = can_act && !d.sel_attacker.empty();
            }
            draw_card_face(mr, v, cd);
            see(mr, v);
            if (can_act && clicked(mr)) {
                d.pinned = v;
                if (side == Side::Player) {
                    if (c.can_attack_this_turn) { d.sel_attacker = d.sel_attacker == c.instance_id ? std::string() : c.instance_id; d.sel_hand = -1; }
                    else toast(c.name + " can't attack yet (just played, frozen, or it already attacked).");
                } else if (!d.sel_attacker.empty()) {
                    queue_attack(*this, d.sel_attacker, c.instance_id);
                }
            } else if (clicked(mr)) {
                d.pinned = v;
            }
        }
    };
    monster_row(Side::Bot);
    monster_row(Side::Player);

    // ---- hand
    const auto& hand = b.hand.player;
    const int n = static_cast<int>(hand.size());
    int hov_i = -1;
    if (!blocked_)
        for (int i = n - 1; i >= 0; --i) {
            const Rectangle s = L::hand_slot(i, n);
            if (CheckCollisionPointRec(in_.mouse, R(s.x, s.y - 16, s.width, s.height + 16))) { hov_i = i; break; }
        }
    auto draw_hand_card = [&](int i, bool is_hover) {
        if (hand[i].instance_id == d.hide_iid) return;                    // that card is flying in right now
        const bool sel = i == d.sel_hand;
        const float lift = (is_hover || sel) ? 14.0f : 0.0f;
        CardDraw cd;
        cd.selected = sel;
        cd.hovered = is_hover;
        cd.dim = my_turn && !busy && !be::can_play(st, Side::Player, i).ok;
        const CardView v = view_of(hand[i]);
        Rectangle s = L::hand_slot(i, n);
        s.y -= lift;
        draw_card_face(s, v, cd);
        if (is_hover) d.hover = v;
    };
    for (int i = 0; i < n; ++i) if (i != hov_i) draw_hand_card(i, false);
    if (hov_i >= 0) draw_hand_card(hov_i, true);
    if (hov_i >= 0) hot_ = hot_ || can_act;
    if (can_act && in_.pressed && hov_i >= 0) {
        d.sel_hand = d.sel_hand == hov_i ? -1 : hov_i;
        d.sel_attacker.clear();
        if (d.sel_hand >= 0) d.pinned = view_of(hand[hov_i]);
    }

    // ---- action buttons (in the player's HUD strip)
    const bool has_sel = d.sel_hand >= 0 && d.sel_hand < n && !busy;
    std::string play_label = "Play card", why;
    bool can_play_sel = false;
    if (has_sel) {
        const CardInstance& c = hand[d.sel_hand];
        play_label = c.card_type == CardType::Monster ? "Summon" : c.card_type == CardType::Spell ? "Activate" : "Set Trap";
        auto res = be::can_play(st, Side::Player, d.sel_hand);
        can_play_sel = res.ok && can_act;
        why = res.reason;
    }
    if (button(R(L::kMainX + 700, L::kPlHudY + 4, 118, 30), play_label, BtnStyle::Primary, can_play_sel, 16)) queue_play(*this, d.sel_hand);
    if (button(R(L::kMainX + 826, L::kPlHudY + 4, 112, 30), "End turn", BtnStyle::Good, can_act, 16)) queue_end_turn(*this);

    // ---- banner between the two fields
    std::string banner;
    Color bc = col::muted;
    if (st.game_over && !busy) banner = "The duel is over.";
    else if (!my_turn || (busy && !shown_my_turn)) {
        const int dots = static_cast<int>(std::fmod(now_ * 2.5, 4.0));
        banner = "Opponent's turn" + std::string(static_cast<std::size_t>(dots), '.') + (busy ? "   (Space = skip animations)" : "");
        bc = col::warn;
    } else if (busy) {
        banner = "Resolving...   (Space = skip animations)";
        bc = col::muted;
    } else if (!d.sel_attacker.empty()) {
        std::string nm;
        if (const CardInstance* a = be::find_in_field(st, Side::Player, d.sel_attacker)) nm = a->name;
        banner = "Attacking with " + nm + ": click an enemy monster, or the opponent's life bar for a direct attack.   (Esc = cancel)";
        bc = col::bad;
    } else if (has_sel) {
        banner = can_play_sel ? "Selected: " + hand[d.sel_hand].name + "  -  press " + play_label + " (Enter)" : "Can't play that yet: " + why;
        bc = can_play_sel ? col::accent : col::warn;
    } else if (grave_fx > 0) {
        banner = "Your Graveyard glows: it holds " + std::to_string(grave_fx) + " effect(s) you can use right now. Click the pile to open it.";
        bc = col::warn;
    } else {
        banner = "Your turn: click a card in your hand to play it, or a ready monster to attack.";
        bc = col::good;
    }
    text_c(fit(banner, 15, L::kMainW - 20, true), R(L::kMainX, L::kBannerY, L::kMainW, 22), 15, bc, true);

    // ---- animation layer (cards in flight, banners, particles)
    duel_anim_draw();

    // ---- sidebar: card info + battle log
    const CardView* shown = d.hover.valid ? &d.hover : d.pinned.valid ? &d.pinned : nullptr;
    draw_detail(R(L::kSideX, 38, L::kSideW, 312), shown, "Hover over any card to read its effect. Click the Graveyard or Banish piles on either side to look inside them.");
    const Rectangle lp = R(L::kSideX, 356, L::kSideW, 360);
    panel(lp);
    text("Battle log", lp.x + 14, lp.y + 8, 16, col::muted, true);
    const Rectangle la = R(lp.x + 10, lp.y + 34, lp.width - 20, lp.height - 44);
    struct Line { std::string s; Color c; };
    std::vector<Line> lines;
    const std::size_t upto = std::min(d.log_shown, st.log.size());
    const std::size_t from = upto > 60 ? upto - 60 : 0;
    for (std::size_t i = from; i < upto; ++i) {
        const Color c = log_color(*this, st.log[i]);
        for (auto& w : wrap(st.log[i], 14, la.width - 14)) lines.push_back({w, c});
    }
    const float lh = 18;
    const float total = static_cast<float>(lines.size()) * lh;
    const float max_up = std::max(0.0f, total - la.height);
    if (hover(la)) d.log_scroll += in_.wheel * 40.0f;
    d.log_scroll = std::max(0.0f, std::min(max_up, d.log_scroll));
    float ly = total > la.height ? la.y + la.height - total + d.log_scroll : la.y;
    begin_clip(la);
    for (const auto& l : lines) {
        if (ly + lh >= la.y && ly <= la.y + la.height) text(l.s, la.x + 2, ly, 14, l.c, l.s.rfind("---", 0) == 0);
        ly += lh;
    }
    end_clip();

    rlPopMatrix();
    if (!tip.empty() && !blocked_) {
        const float w = text_w(tip, 14, true) + 18;
        const Rectangle r = R(std::min(in_.mouse.x + 14, kW - w - 8), in_.mouse.y + 20, w, 26);
        panel(r, Color{14, 17, 26, 245}, col::accent, 7, 1.5f);
        text_c(tip, r, 14, col::text, true);
    }
    blocked_ = saved_blocked;
}

// ----------------------------------------------------------------- overlays
void App::draw_duel_overlays() {
    DuelUI& d = duel_;
    be::DuelState& st = d.st;
    if (d.overlay == DuelOverlay::None) return;
    dim_background(190);

    switch (d.overlay) {
        case DuelOverlay::Zone: {
            const Rectangle box = R(110, 40, 1060, 640);
            panel(box, col::panel, col::accent, 12, 2.0f);
            const auto& zone = d.zone_banished ? st.banished[d.zone_side] : st.graveyard[d.zone_side];
            const int zn = static_cast<int>(zone.size());
            const bool own_grave = d.zone_side == Side::Player && !d.zone_banished;
            text(side_label(d.zone_side) + (d.zone_banished ? " Banished Zone" : " Graveyard") + "  (" + std::to_string(zn) + ")", box.x + 24, box.y + 16, 28, col::text, true);
            if (button(R(box.x + box.width - 190, box.y + 14, 166, 38), "Close  (Esc)", BtnStyle::Normal, true, 17) || key(KEY_ESCAPE)) d.overlay = DuelOverlay::None;
            const float ty = box.y + 62;
            if (button(R(box.x + 24, ty, 160, 38), "Your side", d.zone_side == Side::Player ? BtnStyle::Primary : BtnStyle::Normal, true, 17)) { d.zone_side = Side::Player; d.zone_sel = -1; d.zone_scroll = 0; }
            if (button(R(box.x + 192, ty, 160, 38), "Opponent's side", d.zone_side == Side::Bot ? BtnStyle::Primary : BtnStyle::Normal, true, 17)) { d.zone_side = Side::Bot; d.zone_sel = -1; d.zone_scroll = 0; }
            const int gn = static_cast<int>(st.graveyard[d.zone_side].size()), bn = static_cast<int>(st.banished[d.zone_side].size());
            if (button(R(box.x + 440, ty, 200, 38), "Graveyard (" + std::to_string(gn) + ")", !d.zone_banished ? BtnStyle::Primary : BtnStyle::Normal, true, 17)) { d.zone_banished = false; d.zone_sel = -1; d.zone_scroll = 0; }
            if (button(R(box.x + 648, ty, 200, 38), "Banished (" + std::to_string(bn) + ")", d.zone_banished ? BtnStyle::Primary : BtnStyle::Normal, true, 17)) { d.zone_banished = true; d.zone_sel = -1; d.zone_scroll = 0; }

            const Rectangle area = R(box.x + 20, box.y + 116, 704, 500);
            const float cw = 124, chh = 156, gap = 14;
            const int cols = 5;
            const int rows = (zn + cols - 1) / cols;
            CardView hv;
            const std::string where = std::string("In ") + (d.zone_side == Side::Player ? "your " : "the opponent's ") + (d.zone_banished ? "banished zone" : "Graveyard");
            if (zn == 0) {
                text_c(d.zone_banished ? "Nothing has been banished yet." : "The Graveyard is empty.", area, 20, col::muted);
            } else {
                scroll_area(area, rows * (chh + gap), d.zone_scroll);
                begin_clip(area);
                const bool inside = CheckCollisionPointRec(in_.mouse, area);
                for (int k = 0; k < zn; ++k) {
                    const int idx = zn - 1 - k;                      // most recent first
                    const Rectangle r = R(area.x + (k % cols) * (cw + gap), area.y + (k / cols) * (chh + gap) - d.zone_scroll, cw, chh);
                    if (r.y + r.height < area.y || r.y > area.y + area.height) continue;
                    const bool hov = inside && CheckCollisionPointRec(in_.mouse, r);
                    CardDraw cd;
                    cd.hovered = hov;
                    cd.selected = idx == d.zone_sel;
                    CardView v = view_of(zone[idx], where);
                    if (own_grave && be::can_activate_grave(st, Side::Player, idx).ok) { v.grave_ready = true; cd.ready = !cd.selected; }
                    draw_card_face(r, v, cd);
                    if (hov) { hv = v; hot_ = true; if (in_.pressed) d.zone_sel = idx; }
                }
                end_clip();
            }
            CardView sel;
            if (d.zone_sel >= 0 && d.zone_sel < zn) {
                sel = view_of(zone[d.zone_sel], where);
                if (own_grave && be::can_activate_grave(st, Side::Player, d.zone_sel).ok) sel.grave_ready = true;
            }
            const CardView* shown = hv.valid ? &hv : sel.valid ? &sel : nullptr;
            draw_detail(R(box.x + 744, box.y + 116, 296, 440), shown, d.zone_banished
                        ? "Banished cards are removed from the game. They can't be revived."
                        : own_grave ? "Click a card to read its effect. Cards marked READY have a Graveyard effect you can use now."
                                    : "Click a card to read its effect.");
            if (own_grave && sel.valid) {                            // use a Graveyard effect
                const Card* c = engine_->get_card(sel.card_id);
                const Ability* ab = c ? duel_cards::grave_ability(*c) : nullptr;
                const Rectangle ar = R(box.x + 744, box.y + 566, 296, 50);
                if (ab) {
                    const auto can = be::can_activate_grave(st, Side::Player, d.zone_sel);
                    const std::string label = "Use effect  (" + std::to_string(ab->grave_cost) + " Insight)";
                    if (button(ar, label, BtnStyle::Primary, can.ok && !duel_animating(), 18)) {
                        const int idx = d.zone_sel;
                        d.overlay = DuelOverlay::None;
                        queue_grave(*this, idx);
                    }
                    if (!can.ok) text_c(fit(can.reason, 13, 290), R(ar.x, ar.y - 20, ar.width, 18), 13, col::warn);
                } else {
                    text_c("This card has no Graveyard effect.", ar, 15, col::muted);
                }
            }
            break;
        }
        case DuelOverlay::History: {
            const Rectangle box = R(240, 50, 800, 620);
            panel(box, col::panel, col::accent, 12, 2.0f);
            text("Battle history", box.x + 24, box.y + 16, 28, col::text, true);
            if (button(R(box.x + box.width - 190, box.y + 14, 166, 38), "Close  (Esc)", BtnStyle::Normal, true, 17) || key(KEY_ESCAPE) || key(KEY_H)) d.overlay = DuelOverlay::None;
            const Rectangle area = R(box.x + 20, box.y + 68, box.width - 40, box.height - 88);
            struct Line { std::string s; Color c; bool head; };
            std::vector<Line> lines;
            const std::size_t upto = std::min(d.log_shown, st.log.size());
            for (std::size_t i = 0; i < upto; ++i)
                for (auto& w : wrap(st.log[i], 16, area.width - 24)) lines.push_back({w, log_color(*this, st.log[i]), st.log[i].rfind("---", 0) == 0});
            const float lh = 22;
            scroll_area(area, lines.size() * lh + 6, d.hist_scroll);
            begin_clip(area);
            float y = area.y + 3 - d.hist_scroll;
            for (const auto& l : lines) {
                if (y + lh >= area.y && y <= area.y + area.height) text(l.s, area.x + 4, y, 16, l.c, l.head);
                y += lh;
            }
            end_clip();
            break;
        }
        case DuelOverlay::Glossary: {
            const Rectangle box = R(180, 50, 920, 620);
            panel(box, col::panel, col::accent, 12, 2.0f);
            text("Effects & keywords", box.x + 24, box.y + 16, 28, col::text, true);
            if (button(R(box.x + box.width - 190, box.y + 14, 166, 38), "Close  (Esc)", BtnStyle::Normal, true, 17) || key(KEY_ESCAPE)) d.overlay = DuelOverlay::None;
            draw_effect_glossary(R(box.x + 20, box.y + 68, box.width - 40, box.height - 88), d.gloss_scroll);
            break;
        }
        case DuelOverlay::Result: {
            const Rectangle box = R(320, 130, 640, 460);
            const be::Winner w = st.winner;
            const Color c = w == be::Winner::Player ? col::good : w == be::Winner::Bot ? col::bad : col::warn;
            panel(box, col::panel, c, 14, 3.0f);
            const std::string head = w == be::Winner::Player ? "VICTORY" : w == be::Winner::Bot ? "DEFEAT" : "DRAW";
            text_c(head, R(box.x, box.y + 24, box.width, 70), 60, c, true);
            text_c(w == be::Winner::Player ? "You win the duel!" : w == be::Winner::Bot ? "The opponent wins this time." : "Neither side comes out ahead.", R(box.x, box.y + 100, box.width, 30), 22, col::text, true);
            text_c("Final Life Points   You: " + std::to_string(std::max(0, st.lp.player)) + "     Opponent: " + std::to_string(std::max(0, st.lp.bot)),
                   R(box.x, box.y + 142, box.width, 26), 19, col::muted, true);
            float y = box.y + 188;
            for (const auto& l : d.summary_lines) {
                if (l.rfind("---", 0) == 0) continue;
                text_c(fit(l, 15, box.width - 60), R(box.x, y, box.width, 20), 15, log_color(*this, l));
                y += 21;
            }
            if (button(R(box.x + 40, box.y + box.height - 76, 270, 52), "Rematch", BtnStyle::Primary, true, 21)) defer([this] { launch_duel(); });
            if (button(R(box.x + box.width - 40 - 270, box.y + box.height - 76, 270, 52), "Back to Duel menu", BtnStyle::Normal, true, 21)) {
                d.overlay = DuelOverlay::None;
                screen_ = Screen::DuelMenu;
            }
            break;
        }
        case DuelOverlay::Concede: {
            const Rectangle box = R(400, 240, 480, 230);
            panel(box, col::panel, col::bad, 12, 2.0f);
            text_c("Concede this duel?", R(box.x, box.y + 28, box.width, 36), 28, col::text, true);
            text_c("You'll lose the match.", R(box.x, box.y + 78, box.width, 24), 17, col::muted);
            if (button(R(box.x + 30, box.y + 150, 200, 50), "Concede", BtnStyle::Danger, true, 20)) {
                st.game_over = true;
                st.winner = be::Winner::Bot;
                st.log.push_back("You conceded the duel. Bot wins!");
                d.log_shown = st.log.size();
                d.overlay = DuelOverlay::None;
            }
            if (button(R(box.x + 250, box.y + 150, 200, 50), "Keep playing", BtnStyle::Normal, true, 20) || key(KEY_ESCAPE)) d.overlay = DuelOverlay::None;
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- screen_duel
void App::screen_duel() {
    DuelUI& d = duel_;
    be::DuelState& st = d.st;
    duel_pull_events();
    duel_anim_update(frame_dt());
    const bool busy = duel_animating();
    const bool modal = d.overlay != DuelOverlay::None;

    if (busy && !modal && (key(KEY_SPACE) || key(KEY_ENTER) || in_.pressed)) d.skip = true;    // fast-forward the animations

    // The opponent acts once the table has caught up with everything that happened so far.
    if (!st.game_over && st.active_side == Side::Bot && !busy && !modal) {
        if (!d.bot_waiting) {
            d.bot_waiting = true;
            d.bot_ready_at = now_ + 0.35;
        } else if (now_ >= d.bot_ready_at) {
            d.bot_waiting = false;
            defer([this] { bot_turn(); });
        }
    } else if (st.active_side != Side::Bot) {
        d.bot_waiting = false;
    }

    blocked_ = modal;
    draw_duel_scene(true);
    blocked_ = false;
    draw_duel_overlays();
    finish_duel_if_over();

    if (!modal && !busy && !st.game_over && st.active_side == Side::Player) {
        if (key(KEY_E)) queue_end_turn(*this);
        if (key(KEY_ESCAPE)) { d.sel_hand = -1; d.sel_attacker.clear(); }
        if ((key(KEY_ENTER) || key(KEY_P)) && d.sel_hand >= 0 && be::can_play(st, Side::Player, d.sel_hand).ok) queue_play(*this, d.sel_hand);
        if (key(KEY_H)) { d.overlay = DuelOverlay::History; d.hist_scroll = 1e9f; }
    }
}

// -------------------------------------------------- Trap prompt (blocking modal)
std::string App::ask_trap_modal(be::DuelState& s, const std::vector<CardInstance>& eligible) {
    drain_duel_animations();                         // show everything that led up to this moment first
    const std::string trigger = s.log.empty() ? std::string() : s.log.back();
    std::string chosen;
    bool decided = false;
    std::vector<std::string> texts;
    std::vector<float> heights;
    for (const auto& t : eligible) {
        std::string txt;
        if (const Card* c = engine_->get_card(t.card_id))
            for (const auto& line : duel_cards::describe(*c, t.level)) txt += (txt.empty() ? "" : " ") + line;
        texts.push_back(txt);
        heights.push_back(std::max(88.0f, 58.0f + wrapped_height(txt, 560, 14, false, 1)));
    }
    while (!decided) {
        if (!begin_frame()) return "";
        draw_duel_scene(false);
        dim_background(175);
        float rows_h = 0;
        for (float h : heights) rows_h += h + 8;
        const Rectangle box = R(230, 90, 820, 190 + rows_h);
        panel(box, col::panel, type_color(CardType::Trap), 12, 3.0f);
        text_c("Activate a Trap?", R(box.x, box.y + 18, box.width, 40), 34, type_color(CardType::Trap), true);
        text_wrapped("Just happened:  " + trigger, R(box.x + 36, box.y + 68, box.width - 72, 44), 17, col::muted);
        float y = box.y + 118;
        for (std::size_t i = 0; i < eligible.size(); ++i) {
            const CardInstance& t = eligible[i];
            const Rectangle r = R(box.x + 30, y, box.width - 60, heights[i]);
            panel(r, col::panel2, mix(type_color(CardType::Trap), col::white, 0.2f), 9, 1.5f);
            DrawRectangle(static_cast<int>(r.x + 8), static_cast<int>(r.y + 10), 5, static_cast<int>(r.height - 20), type_color(CardType::Trap));
            const Card* c = engine_->get_card(t.card_id);
            text(t.name, r.x + 26, r.y + 8, 20, col::text, true);
            text("Lv " + std::to_string(t.level) + "  -  " + (c ? c->tag : std::string()), r.x + 26, r.y + 32, 14, col::accent, true);
            text_wrapped(texts[i], R(r.x + 26, r.y + 54, r.width - 210, r.height - 56), 14, col::text, false, 1);
            if (button(R(r.x + r.width - 150, r.y + r.height / 2 - 22, 130, 44), "Activate", BtnStyle::Primary, true, 18)) { chosen = t.instance_id; decided = true; }
            y += heights[i] + 8;
        }
        if (button(R(box.x + box.width / 2 - 130, box.y + box.height - 62, 260, 46), "Don't activate  (Esc)", BtnStyle::Normal, true, 18) || key(KEY_ESCAPE)) { chosen.clear(); decided = true; }
        end_frame();
    }
    return chosen;
}

}  // namespace fd::ui
