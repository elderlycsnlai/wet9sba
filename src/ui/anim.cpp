// Duel animations: replays the engine's recorded events as Master Duel-style effects -- cards flying
// between zones, attack lunges, effect banners, destruction shards, banish swirls, coin flips and turn
// banners. The engine has already finished the move when an event is replayed; this layer only decides
// what the player sees while the table catches up (d.shown), and never changes the game itself.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app.hpp"
#include "rlgl.h"

namespace fd::ui {
namespace be = fd::battle;
namespace L = fd::ui::lay;

static const float kPi = 3.14159265f;
static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
static float ease_out(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }
static float ease_in(float t) { t = clamp01(t); return t * t; }
static float ease_in_out(float t) { t = clamp01(t); return t < 0.5f ? 2 * t * t : 1.0f - std::pow(-2 * t + 2, 2.0f) / 2.0f; }
static Vector2 center_of(Rectangle r) { return {r.x + r.width / 2, r.y + r.height / 2}; }
static Rectangle lerp_rect(Rectangle a, Rectangle b, float t) {
    return R(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.width + (b.width - a.width) * t, a.height + (b.height - a.height) * t);
}
static float frand() {
    static std::uint32_t s = 2463534242u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFF) / 16777216.0f;
}
static float frand(float a, float b) { return a + (b - a) * frand(); }
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = s.find('\n', start);
        out.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// Where is this card on the table `b`? (hand, field or Trap zone). found = false when it is nowhere.
static Rectangle locate(const be::Board& b, const std::string& iid, bool& found) {
    found = true;
    for (Side s : {Side::Player, Side::Bot}) {
        for (std::size_t i = 0; i < b.field[s].size(); ++i) if (b.field[s][i].instance_id == iid) return L::monster_slot(s, static_cast<int>(i));
        for (std::size_t i = 0; i < b.traps[s].size(); ++i) if (b.traps[s][i].instance_id == iid) return L::trap_slot(s, static_cast<int>(i));
    }
    const int n = static_cast<int>(b.hand.player.size());
    for (int i = 0; i < n; ++i) if (b.hand.player[i].instance_id == iid) return L::hand_slot(i, n);
    for (const auto& c : b.hand.bot) if (c.instance_id == iid) return L::opp_hand_anchor();
    found = false;
    return {};
}

CardView App::view_of_event_card(const be::Event& ev) const { return view_of(ev.card); }

void App::duel_pull_events() {
    DuelUI& d = duel_;
    if (d.st.events.empty()) return;
    for (auto& e : d.st.events) d.queue.push_back(std::move(e));
    d.st.events.clear();
}

bool App::duel_animating() const {
    const DuelUI& d = duel_;
    return d.have_cur || !d.queue.empty() || !d.st.events.empty();
}

void App::duel_anim_reset() {
    DuelUI& d = duel_;
    d.queue.clear();
    d.have_cur = false;
    d.cur_t = d.cur_dur = 0;
    d.hide_iid.clear();
    d.skip = false;
    d.fx = FxState{};
    d.shown = be::snapshot(d.st);
    d.log_shown = d.st.log.size();
    for (Side s : {Side::Player, Side::Bot}) {
        d.lp_disp[s] = static_cast<float>(std::max(0, d.shown.lp[s]));
        d.sh_disp[s] = static_cast<float>(d.shown.shield[s]);
    }
}

// Blocks until everything queued so far has been played (used before a prompt that needs the true table).
void App::drain_duel_animations() {
    for (int guard = 0; guard < 6000; ++guard) {
        duel_pull_events();
        if (!duel_animating()) break;
        if (!begin_frame()) return;
        duel_anim_update(frame_dt());
        draw_duel_scene(false);
        end_frame();
    }
}

// ------------------------------------------------------------------ starting an event
void App::duel_anim_begin(const be::Event& ev) {
    DuelUI& d = duel_;
    FxState& fx = d.fx;
    const be::Board prev = d.shown;                    // the table before this event
    const float S = d.skip ? 0.0f : d.speed >= 2 ? 0.5f : 1.0f;
    d.hide_iid.clear();
    d.shown = ev.after;
    d.log_shown = ev.log_size;
    d.cur_dur = 0;
    if (S == 0.0f || d.speed == 0) return;             // animations off / skipped: just move on

    const Side side = ev.side, foe = other(side);
    const bool mine = side == Side::Player;
    const std::string& iid = ev.card.instance_id;
    auto D = [&](float x) { return x * S; };
    bool found = false;

    auto burst = [&](Vector2 p, Color c, int n, float speed, float life, float delay = 0.0f, bool square = true, float gravity = 260.0f) {
        for (int i = 0; i < n; ++i) {
            Spark s;
            const float a = frand(0, 2 * kPi), v = frand(0.35f, 1.0f) * speed;
            s.p = p; s.v = {std::cos(a) * v, std::sin(a) * v}; s.t = -delay; s.life = life * frand(0.7f, 1.25f);
            s.size = frand(3, 7); s.rot = frand(0, 360); s.vrot = frand(-420, 420); s.gravity = gravity; s.c = c; s.square = square;
            fx.sparks.push_back(s);
        }
    };
    auto ring = [&](Vector2 p, Color c, float r1, float dur, float delay = 0.0f, float thick = 4.0f) {
        RingFx r; r.p = p; r.c = c; r.r0 = 6; r.r1 = r1; r.dur = dur; r.t = -delay; r.thick = thick; fx.rings.push_back(r);
    };
    auto floater = [&](Vector2 p, const std::string& s, Color c, float size, float dur = 1.05f, float delay = 0.0f) {
        Floater f; f.s = s; f.p = p; f.c = c; f.size = size; f.dur = dur; f.t = -delay; fx.floats.push_back(f);
    };
    auto flash = [&](Rectangle r, Color c, float dur = 0.35f, float delay = 0.0f) {
        FlashFx f; f.r = r; f.c = c; f.dur = dur; f.t = -delay; fx.flashes.push_back(f);
    };
    auto fly = [&](const CardView& v, bool down, Rectangle a, Rectangle b, float dur, float end_alpha = 1.0f, Color glow = Color{0, 0, 0, 0}, float spin = 0.0f) {
        Fly f; f.view = v; f.face_down = down; f.a = a; f.b = b; f.dur = std::max(0.01f, dur); f.end_alpha = end_alpha; f.glow = glow; f.spin = spin;
        fx.flies.push_back(f);
    };
    auto shake = [&](float mag, float dur) { fx.shake_mag = mag; fx.shake_dur = dur; fx.shake_t = 0; };
    const Color kShield{120, 214, 236, 255}, kHeal = col::good, kHurt{255, 112, 104, 255}, kGold = col::warn, kViolet{176, 118, 236, 255};
    const Color kTrap = type_color(CardType::Trap);

    // The board position of this card before (src) and after (dst) the event.
    auto hand_src = [&]() -> Rectangle {
        if (!mine) return L::opp_hand_anchor();
        const Rectangle r = locate(prev, iid, found);
        return found ? r : L::center_stage();
    };
    auto hand_dst = [&]() -> Rectangle {
        if (!mine) return L::opp_hand_anchor();
        const Rectangle r = locate(ev.after, iid, found);
        return found ? r : L::center_stage();
    };

    float ed = 0.2f;                                    // how long this event holds up the queue
    switch (ev.type) {
        case be::Ev::Draw: {
            fly(view_of(ev.card), !mine, L::deck_pile(side), hand_dst(), D(0.30f));
            if (mine) d.hide_iid = iid;
            ed = 0.30f;
            break;
        }
        case be::Ev::ToHand: {
            Rectangle src = L::deck_pile(side);
            if (ev.from == "field") { const Rectangle r = locate(prev, ev.target_id, found); if (found) src = r; }
            else if (ev.from == "gy") src = L::grave_pile(side);
            fly(view_of(ev.card), !mine && ev.from == "deck", src, hand_dst(), D(0.40f), 1.0f, alpha(col::accent, 150));
            if (mine) d.hide_iid = iid;
            ed = 0.40f;
            break;
        }
        case be::Ev::Summon: {
            Rectangle src = hand_src();
            if (ev.from == "gy") src = L::grave_pile(side);
            else if (ev.from == "enemy-gy") src = L::grave_pile(foe);
            const Rectangle dst = locate(ev.after, iid, found);
            fly(view_of(ev.card), false, src, found ? dst : L::center_stage(), D(0.42f), 1.0f, ev.flag ? alpha(kGold, 170) : alpha(col::accent, 120));
            d.hide_iid = iid;
            if (found) {
                const Vector2 c = center_of(dst);
                ring(c, ev.flag ? kGold : col::accent, 80, D(0.5f), D(0.36f));
                burst(c, ev.flag ? kGold : Color{150, 190, 255, 255}, ev.flag ? 22 : 12, 170, D(0.7f), D(0.38f), true, 120);
                flash(dst, alpha(col::white, 150), D(0.3f), D(0.4f));
            }
            ed = 0.46f;
            break;
        }
        case be::Ev::SpellCast: {
            BannerFx b;
            b.view = view_of(ev.card); b.lines = split_lines(ev.text); b.head = "SPELL CARD"; b.from = hand_src();
            b.dur = D(1.05f); b.c = type_color(CardType::Spell);
            fx.banner = b;
            ed = 1.05f;
            break;
        }
        case be::Ev::SetTrap: {
            const Rectangle dst = locate(ev.after, iid, found);
            fly(view_of(ev.card), true, hand_src(), found ? dst : L::center_stage(), D(0.40f));
            d.hide_iid = iid;
            if (found) { ring(center_of(dst), kTrap, 50, D(0.4f), D(0.34f)); floater({dst.x + dst.width / 2, dst.y - 6}, "SET", kTrap, 18, D(0.8f), D(0.36f)); }
            ed = 0.42f;
            break;
        }
        case be::Ev::TrapFlip: {
            const Rectangle at = locate(prev, iid, found);
            if (found) { flash(at, alpha(kTrap, 230), D(0.45f)); ring(center_of(at), kTrap, 90, D(0.55f)); }
            BannerFx b;
            b.view = view_of(ev.card); b.view.hidden = false; b.lines = split_lines(ev.text); b.head = "TRAP CARD ACTIVATED";
            b.from = found ? at : L::center_stage(); b.dur = D(1.1f); b.c = kTrap;
            fx.banner = b;
            shake(5, D(0.3f));
            ed = 1.1f;
            break;
        }
        case be::Ev::Ability: {
            Rectangle at = locate(prev, iid, found);
            if (!found) at = locate(ev.after, iid, found);
            const Rectangle from = found ? at : L::center_stage();
            if (found) { ring(center_of(at), col::accent, 76, D(0.55f)); flash(at, alpha(col::accent, 150), D(0.4f)); }
            BannerFx b;
            b.view = view_of(ev.card); b.lines = split_lines(ev.text); b.head = "EFFECT ACTIVATED  -  " + ev.from; b.from = from;
            b.dur = D(0.95f); b.c = col::accent;
            fx.banner = b;
            ed = 0.95f;
            break;
        }
        case be::Ev::Attack: {
            Rectangle arect = locate(prev, iid, found);
            if (!found) arect = L::center_stage();
            Rectangle trect = L::hud(foe);
            if (ev.target_id != "face") { const Rectangle r = locate(prev, ev.target_id, found); if (found) trect = r; }
            if (ev.from == "declare") {
                AimFx a; a.a = center_of(arect); a.b = center_of(trect); a.dur = D(0.42f);
                fx.aim = a;
                flash(arect, alpha(col::bad, 160), D(0.4f));
                ed = 0.42f;
            } else {
                LungeFx l; l.view = view_of(ev.card); l.from = arect; l.to = trect; l.dur = D(0.64f);
                l.view.on_field = true; l.view.show_cost = false;
                fx.lunge = l;
                d.hide_iid = iid;
                const Vector2 hit = center_of(trect);
                burst(hit, Color{255, 220, 150, 255}, 18, 260, D(0.45f), D(0.31f), true, 200);
                ring(hit, alpha(col::white, 230), 70, D(0.35f), D(0.30f), 5);
                flash(trect, alpha(col::white, 170), D(0.25f), D(0.31f));
                shake(6, D(0.25f));
                fx.shake_t = -D(0.30f);
                ed = 0.64f;
            }
            break;
        }
        case be::Ev::Damage: {
            Rectangle at = locate(ev.after, iid, found);
            if (!found) at = locate(prev, iid, found);
            if (found) {
                floater({at.x + at.width / 2, at.y + 14}, "-" + std::to_string(ev.amount), kHurt, 30);
                flash(at, alpha(kHurt, 170), D(0.3f));
                burst(center_of(at), kHurt, 8, 150, D(0.4f), 0.0f, false, 120);
            }
            ed = 0.2f;
            break;
        }
        case be::Ev::FaceDamage: {
            const Rectangle h = L::hud(side);
            const int absorbed = std::atoi(ev.text.c_str());
            if (absorbed > 0) {
                floater({h.x + 424, h.y - 4}, "Shield -" + std::to_string(absorbed), kShield, 22);
                ring({h.x + 424, h.y + 19}, kShield, 40, D(0.4f));
            }
            if (ev.amount > 0) {
                floater({h.x + 312, h.y - 8}, "-" + std::to_string(ev.amount), kHurt, 34);
                flash(h, alpha(kHurt, 150), D(0.4f));
                burst({h.x + 300, h.y + 19}, kHurt, 14, 200, D(0.5f), 0.0f, true, 160);
                shake(std::min(9.0f, 3.0f + ev.amount), D(0.3f));
            }
            ed = 0.26f;
            break;
        }
        case be::Ev::ShieldGain: {
            const Rectangle h = L::hud(side);
            ring({h.x + 424, h.y + 19}, kShield, 56, D(0.5f));
            floater({h.x + 424, h.y - 4}, "+" + std::to_string(ev.amount) + " Shield", kShield, 22);
            burst({h.x + 424, h.y + 19}, kShield, 10, 120, D(0.5f), 0.0f, false, -60);
            ed = 0.22f;
            break;
        }
        case be::Ev::Heal: {
            const Rectangle h = L::hud(side);
            floater({h.x + 312, h.y - 8}, "+" + std::to_string(ev.amount), kHeal, 32);
            burst({h.x + 300, h.y + 19}, kHeal, 12, 110, D(0.7f), 0.0f, false, -120);
            flash(h, alpha(kHeal, 90), D(0.4f));
            ed = 0.22f;
            break;
        }
        case be::Ev::Insight: {
            const Rectangle h = L::hud(side);
            floater({h.x + 560, h.y - 4}, "+" + std::to_string(ev.amount) + " Insight", col::accent, 22);
            ring({h.x + 540, h.y + 19}, col::accent, 50, D(0.45f));
            ed = 0.2f;
            break;
        }
        case be::Ev::Buff:
        case be::Ev::Debuff: {
            const bool up = ev.type == be::Ev::Buff;
            const Rectangle at = locate(ev.after, iid, found);
            if (found) {
                floater({at.x + at.width / 2, at.y + 10}, ev.text, up ? kGold : kViolet, 24);
                burst(center_of(at), up ? kGold : kViolet, 10, 120, D(0.55f), 0.0f, false, up ? -80.0f : 80.0f);
                flash(at, alpha(up ? kGold : kViolet, 90), D(0.35f));
            }
            ed = 0.2f;
            break;
        }
        case be::Ev::Freeze:
        case be::Ev::Silence: {
            const bool frz = ev.type == be::Ev::Freeze;
            const Rectangle at = locate(ev.after, iid, found);
            if (found) {
                floater({at.x + at.width / 2, at.y + 8}, frz ? "FROZEN" : "SILENCED", frz ? Color{120, 206, 244, 255} : Color{226, 120, 120, 255}, 24);
                flash(at, alpha(frz ? Color{120, 206, 244, 255} : Color{226, 120, 120, 255}, 170), D(0.45f));
                ring(center_of(at), frz ? Color{120, 206, 244, 255} : Color{226, 120, 120, 255}, 70, D(0.5f));
            }
            ed = 0.32f;
            break;
        }
        case be::Ev::Reveal: {
            for (int i = 0; i < static_cast<int>(ev.after.traps[side].size()); ++i) flash(L::trap_slot(side, i), alpha(kViolet, 180), D(0.5f));
            floater({L::trap_slot(side, 1).x + 48, L::trap_slot(side, 1).y - 4}, "TRAPS REVEALED", kViolet, 20);
            ed = 0.34f;
            break;
        }
        case be::Ev::Destroy: {
            Rectangle at = locate(prev, iid, found);
            if (!found) at = L::center_stage();
            fly(view_of(ev.card), false, at, L::grave_pile(side), D(0.55f), 0.0f);
            const Vector2 c = center_of(at);
            burst(c, Color{255, 150, 90, 255}, 26, 280, D(0.7f), 0.0f, true, 420);
            burst(c, Color{255, 230, 170, 255}, 10, 160, D(0.5f), 0.0f, false, 100);
            flash(at, alpha(col::white, 200), D(0.28f));
            ring(c, Color{255, 170, 110, 255}, 80, D(0.45f));
            shake(4, D(0.25f));
            ed = 0.46f;
            break;
        }
        case be::Ev::Banish: {
            Rectangle at = ev.from == "gy" ? L::grave_pile(side) : ev.from == "spell" ? L::center_stage() : locate(prev, iid, found);
            if (ev.from == "field" && !found) at = L::center_stage();
            fly(view_of(ev.card), false, at, L::banish_pile(side), D(0.70f), 0.0f, alpha(kViolet, 200), 540.0f);
            const Vector2 c = center_of(at);
            for (int i = 0; i < 16; ++i) {                         // a violet swirl that is pulled into the banish pile
                Spark s; const float a = frand(0, 2 * kPi), rr = frand(20, 60);
                s.p = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr}; s.v = {-std::sin(a) * 160, std::cos(a) * 160}; s.life = D(0.7f); s.size = frand(3, 6);
                s.c = kViolet; s.square = false; s.gravity = 0; fx.sparks.push_back(s);
            }
            ring(c, kViolet, 70, D(0.5f));
            ring(center_of(L::banish_pile(side)), kViolet, 46, D(0.45f), D(0.45f));
            ed = 0.62f;
            break;
        }
        case be::Ev::ToGrave: {
            Rectangle src;
            if (ev.from == "spell") src = L::center_stage();
            else if (ev.from == "trap") { src = locate(prev, iid, found); if (!found) src = L::center_stage(); }
            else if (ev.from == "deck") src = L::deck_pile(side);
            else src = hand_src();
            fly(view_of(ev.card), false, src, L::grave_pile(side), D(0.38f), 0.35f, alpha(col::bad, 110));
            ring(center_of(L::grave_pile(side)), alpha(col::muted, 220), 40, D(0.4f), D(0.3f), 3);
            ed = 0.38f;
            break;
        }
        case be::Ev::CoinFlip: {
            CoinFx c; c.heads = ev.flag; c.dur = D(1.35f); c.label = ev.text;
            fx.coin = c;
            ed = 1.35f;
            break;
        }
        case be::Ev::Negate: {
            floater({L::kMainX + L::kMainW / 2, L::kBannerY - 30}, "NEGATED", kHurt, 56, D(1.0f));
            floater({L::kMainX + L::kMainW / 2, L::kBannerY + 28}, ev.text, col::text, 22, D(1.0f));
            flash(R(L::kMainX, 150, L::kMainW, 230), alpha(kHurt, 70), D(0.5f));
            shake(7, D(0.3f));
            ed = 0.7f;
            break;
        }
        case be::Ev::TurnStart: {
            TurnFx t; t.main = mine ? "YOUR TURN" : "OPPONENT'S TURN"; t.sub = "Turn " + std::to_string(ev.amount); t.c = mine ? col::good : col::warn; t.dur = D(1.0f);
            fx.turn = t;
            const Rectangle h = L::hud(side);
            ring({h.x + 540, h.y + 19}, col::accent, 54, D(0.6f), D(0.3f));
            ed = 1.0f;
            break;
        }
        case be::Ev::GameOver: {
            flash(R(0, 0, kW, kH), alpha(col::white, 140), D(0.5f));
            shake(10, D(0.45f));
            ed = 0.5f;
            break;
        }
    }
    d.cur_dur = D(ed);
}

// ------------------------------------------------------------------ per-frame update
void App::duel_anim_update(float dt) {
    DuelUI& d = duel_;
    FxState& fx = d.fx;
    if (d.have_cur) {
        d.cur_t += dt;
        if (d.cur_t >= d.cur_dur) { d.have_cur = false; d.hide_iid.clear(); }
    }
    for (int guard = 0; !d.have_cur && !d.queue.empty() && guard < 256; ++guard) {
        be::Event ev = std::move(d.queue.front());
        d.queue.pop_front();
        duel_anim_begin(ev);
        d.cur_t = 0;
        d.have_cur = d.cur_dur > 0.0f;
        if (!d.have_cur) d.hide_iid.clear();
    }
    if (!d.have_cur && d.queue.empty()) {
        d.skip = false;
        d.shown = be::snapshot(d.st);                  // idle: the picture is always the real table
        d.log_shown = d.st.log.size();
    }
    auto age = [&](auto& v, auto done) { for (auto& x : v) x.t += dt; v.erase(std::remove_if(v.begin(), v.end(), done), v.end()); };
    age(fx.flies, [](const Fly& f) { return f.t >= f.dur; });
    age(fx.floats, [](const Floater& f) { return f.t >= f.dur; });
    age(fx.rings, [](const RingFx& f) { return f.t >= f.dur; });
    age(fx.flashes, [](const FlashFx& f) { return f.t >= f.dur; });
    for (auto& s : fx.sparks) {
        s.t += dt;
        if (s.t >= 0) { s.p.x += s.v.x * dt; s.p.y += s.v.y * dt; s.v.y += s.gravity * dt; s.rot += s.vrot * dt; }
    }
    fx.sparks.erase(std::remove_if(fx.sparks.begin(), fx.sparks.end(), [](const Spark& s) { return s.t >= s.life; }), fx.sparks.end());
    if (fx.banner) { fx.banner->t += dt; if (fx.banner->t >= fx.banner->dur) fx.banner.reset(); }
    if (fx.coin) { fx.coin->t += dt; if (fx.coin->t >= fx.coin->dur) fx.coin.reset(); }
    if (fx.turn) { fx.turn->t += dt; if (fx.turn->t >= fx.turn->dur) fx.turn.reset(); }
    if (fx.lunge) { fx.lunge->t += dt; if (fx.lunge->t >= fx.lunge->dur) fx.lunge.reset(); }
    if (fx.aim) { fx.aim->t += dt; if (fx.aim->t >= fx.aim->dur) fx.aim.reset(); }
    fx.shake_t += dt;
    // LP / Shield numbers count towards their new value instead of jumping
    for (Side s : {Side::Player, Side::Bot}) {
        auto step = [&](float& cur, float target) {
            const float diff = target - cur;
            if (d.skip || d.speed == 0 || std::fabs(diff) < 0.05f) { cur = target; return; }
            const float move = std::min(std::fabs(diff), std::max(12.0f * dt, std::fabs(diff) * 6.0f * dt));
            cur += diff > 0 ? move : -move;
        };
        step(d.lp_disp[s], static_cast<float>(std::max(0, d.shown.lp[s])));
        step(d.sh_disp[s], static_cast<float>(d.shown.shield[s]));
    }
}

// ------------------------------------------------------------------ drawing
void App::draw_coin(Vector2 c, float r, float u, bool heads) {
    u = clamp01(u);
    const float spins = heads ? 4.0f : 4.5f;                         // an extra half turn flips the final face
    const float ang = spins * 2.0f * kPi * ease_out(u);
    const float k = std::fabs(std::cos(ang));                         // 1 = face-on, 0 = edge-on
    const bool face_a = std::cos(ang) >= 0;
    const float lift = std::sin(kPi * u) * r * 2.6f;
    const Vector2 p{c.x, c.y - lift};
    DrawEllipse(static_cast<int>(c.x), static_cast<int>(c.y + r * 1.15f), r * (0.95f - 0.35f * std::sin(kPi * u)), r * 0.22f, Color{0, 0, 0, 100});
    const float w = std::max(2.5f, r * k);
    DrawEllipse(static_cast<int>(p.x), static_cast<int>(p.y), w + 3, r + 3, Color{150, 108, 28, 255});
    DrawEllipse(static_cast<int>(p.x), static_cast<int>(p.y), w, r, face_a ? Color{246, 206, 90, 255} : Color{228, 180, 66, 255});
    if (k > 0.4f) text_c(face_a ? "H" : "T", R(p.x - r, p.y - r, 2 * r, 2 * r), r * 1.05f, Color{112, 76, 16, 255}, true);
}

void App::duel_anim_draw() {
    DuelUI& d = duel_;
    FxState& fx = d.fx;
    for (const auto& f : fx.flashes) {
        if (f.t < 0) continue;
        const float a = 1.0f - clamp01(f.t / f.dur);
        DrawRectangleRounded(f.r, 0.12f, 6, alpha(f.c, static_cast<int>(f.c.a * a * a)));
    }
    for (const auto& r : fx.rings) {
        if (r.t < 0) continue;
        const float u = clamp01(r.t / r.dur);
        DrawRing(r.p, r.r0 + (r.r1 - r.r0) * ease_out(u) - r.thick, r.r0 + (r.r1 - r.r0) * ease_out(u), 0, 360, 36, alpha(r.c, static_cast<int>(r.c.a * (1.0f - u))));
    }
    for (const auto& f : fx.flies) {
        const float u = clamp01(f.t / f.dur), e = ease_in_out(u);
        const Rectangle r = lerp_rect(f.a, f.b, e);
        const float a = 1.0f + (f.end_alpha - 1.0f) * u;
        if (f.glow.a) DrawRectangleRounded(R(r.x - 6, r.y - 6, r.width + 12, r.height + 12), 0.2f, 6, alpha(f.glow, static_cast<int>(f.glow.a * (1.0f - 0.4f * u))));
        const bool rotate = f.spin != 0.0f;
        if (rotate) { rlPushMatrix(); rlTranslatef(r.x + r.width / 2, r.y + r.height / 2, 0); rlRotatef(f.spin * e, 0, 0, 1); rlTranslatef(-(r.x + r.width / 2), -(r.y + r.height / 2), 0); }
        CardDraw cd;
        cd.face_down = f.face_down;
        draw_card_face(r, f.view, cd);
        if (a < 0.99f) DrawRectangleRounded(r, 0.08f, 6, alpha(col::bg, static_cast<int>(255 * (1.0f - clamp01(a)))));
        if (rotate) rlPopMatrix();
    }
    if (fx.lunge) {
        const LungeFx& l = *fx.lunge;
        const float u = clamp01(l.t / l.dur);
        const Vector2 c0 = center_of(l.from), c1 = center_of(l.to);
        float off;
        if (u < 0.25f) off = -0.07f * ease_out(u / 0.25f);                              // wind-up
        else if (u < 0.48f) off = -0.07f + 0.87f * ease_in((u - 0.25f) / 0.23f);        // strike
        else if (u < 0.6f) off = 0.80f;                                                  // impact
        else off = 0.80f * (1.0f - ease_in_out((u - 0.6f) / 0.4f));                      // return
        const Vector2 pos{c0.x + (c1.x - c0.x) * off, c0.y + (c1.y - c0.y) * off};
        const Rectangle r = R(pos.x - l.from.width / 2, pos.y - l.from.height / 2, l.from.width, l.from.height);
        DrawRectangleRounded(R(r.x - 5, r.y - 5, r.width + 10, r.height + 10), 0.2f, 6, alpha(col::bad, u > 0.2f && u < 0.7f ? 110 : 0));
        draw_card_face(r, l.view, CardDraw{});
        if (u > 0.47f && u < 0.72f) {                                                    // slash marks on the target
            const float a = 1.0f - (u - 0.47f) / 0.25f;
            const Color sc = alpha(col::white, static_cast<int>(255 * a));
            DrawLineEx({c1.x - 34, c1.y - 34}, {c1.x + 34, c1.y + 34}, 5, sc);
            DrawLineEx({c1.x + 34, c1.y - 34}, {c1.x - 34, c1.y + 34}, 3, sc);
        }
    }
    if (fx.aim) {
        const AimFx& a = *fx.aim;
        const float u = clamp01(a.t / a.dur);
        const float k = ease_out(u * 1.7f);
        const Vector2 tip{a.a.x + (a.b.x - a.a.x) * k, a.a.y + (a.b.y - a.a.y) * k};
        const float fade = u > 0.8f ? 1.0f - (u - 0.8f) / 0.2f : 1.0f;
        const Color c = alpha(col::bad, static_cast<int>(235 * fade));
        DrawLineEx(a.a, tip, 6, c);
        const float len = std::max(1.0f, std::sqrt((tip.x - a.a.x) * (tip.x - a.a.x) + (tip.y - a.a.y) * (tip.y - a.a.y)));
        const Vector2 dir{(tip.x - a.a.x) / len, (tip.y - a.a.y) / len}, nrm{-dir.y, dir.x};
        DrawTriangle({tip.x + dir.x * 22, tip.y + dir.y * 22}, {tip.x + nrm.x * 14, tip.y + nrm.y * 14}, {tip.x - nrm.x * 14, tip.y - nrm.y * 14}, c);
        DrawRing(a.b, 20 + 8 * std::sin(a.t * 20), 26 + 8 * std::sin(a.t * 20), 0, 360, 24, alpha(col::bad, static_cast<int>(200 * fade)));
    }
    for (const auto& s : fx.sparks) {
        if (s.t < 0) continue;
        const float a = 1.0f - clamp01(s.t / s.life);
        const Color c = alpha(s.c, static_cast<int>(s.c.a * a));
        if (s.square) DrawRectanglePro(R(s.p.x, s.p.y, s.size, s.size), {s.size / 2, s.size / 2}, s.rot, c);
        else DrawCircleV(s.p, s.size * (0.5f + 0.5f * a), c);
    }
    if (fx.banner) {
        const BannerFx& b = *fx.banner;
        const float a = std::min(clamp01(b.t / 0.18f), clamp01((b.dur - b.t) / 0.22f));
        const Rectangle band = R(L::kMainX, 150, L::kMainW, 232);
        DrawRectangleRec(band, alpha(Color{8, 10, 18, 255}, static_cast<int>(222 * a)));
        DrawRectangle(static_cast<int>(band.x), static_cast<int>(band.y), static_cast<int>(band.width), 3, alpha(b.c, static_cast<int>(255 * a)));
        DrawRectangle(static_cast<int>(band.x), static_cast<int>(band.y + band.height - 3), static_cast<int>(band.width), 3, alpha(b.c, static_cast<int>(255 * a)));
        const Rectangle spot = R(band.x + 52, band.y + 5, 150, 222);
        const Rectangle cr = lerp_rect(b.from, spot, ease_out(b.t / 0.28f));
        DrawRectangleRounded(R(cr.x - 7, cr.y - 7, cr.width + 14, cr.height + 14), 0.15f, 6, alpha(b.c, static_cast<int>(110 * a)));
        draw_card_face(cr, b.view, CardDraw{});
        DrawRectangleRounded(cr, 0.08f, 6, alpha(Color{8, 10, 18, 255}, static_cast<int>(255 * (1.0f - a))));
        const float tx = spot.x + spot.width + 44, tw = band.x + band.width - tx - 34;
        const int ta = static_cast<int>(255 * a);
        text(b.head, tx, band.y + 20, 20, alpha(b.c, ta), true);
        text(fit(b.view.name, 32, tw, true), tx, band.y + 50, 32, alpha(col::text, ta), true);
        float y = band.y + 100;
        for (const auto& line : b.lines) {
            const float h = text_wrapped(line, R(tx, y, tw, 120), 20, alpha(col::text, ta), false, 3);
            y += h + 8;
        }
    }
    if (fx.coin) {
        const CoinFx& c = *fx.coin;
        const float u = clamp01(c.t / c.dur), a = std::min(clamp01(c.t / 0.12f), clamp01((c.dur - c.t) / 0.2f));
        const Vector2 mid{L::kMainX + L::kMainW / 2, 262};
        DrawRectangleRounded(R(mid.x - 150, mid.y - 120, 300, 200), 0.1f, 8, alpha(Color{8, 10, 18, 255}, static_cast<int>(215 * a)));
        draw_coin({mid.x, mid.y + 20}, 36, clamp01(u / 0.8f), c.heads);
        if (u > 0.78f) text_c(c.heads ? "HEADS" : "TAILS", R(mid.x - 150, mid.y + 62, 300, 24), 24, alpha(col::warn, static_cast<int>(255 * a)), true);
    }
    for (const auto& f : fx.floats) {
        if (f.t < 0) continue;
        const float u = clamp01(f.t / f.dur);
        const float a = u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) / 0.3f;
        const float pop = u < 0.15f ? 0.6f + 0.4f * (u / 0.15f) : 1.0f;
        const float sz = f.size * pop;
        const float w = text_w(f.s, sz, true);
        const float x = f.p.x - w / 2, y = f.p.y - 44 * ease_out(u);
        text(f.s, x + 1.5f, y + 1.5f, sz, alpha(Color{0, 0, 0, 255}, static_cast<int>(180 * a)), true);   // drop shadow for contrast on any card
        text(f.s, x, y, sz, alpha(f.c, static_cast<int>(f.c.a * a)), true);
    }
    if (fx.turn) {
        const TurnFx& t = *fx.turn;
        const float a = std::min(clamp01(t.t / 0.14f), clamp01((t.dur - t.t) / 0.22f));
        const float slide = (1.0f - ease_out(t.t / 0.3f)) * -260.0f + ease_in(clamp01((t.t - (t.dur - 0.3f)) / 0.3f)) * 260.0f;
        const Rectangle band = R(0, 268, kW, 84);
        DrawRectangleRec(band, alpha(Color{8, 10, 18, 255}, static_cast<int>(228 * a)));
        DrawRectangle(0, 268, kW, 3, alpha(t.c, static_cast<int>(255 * a)));
        DrawRectangle(0, 349, kW, 3, alpha(t.c, static_cast<int>(255 * a)));
        text_c(t.main, R(slide, 274, kW, 48), 42, alpha(t.c, static_cast<int>(255 * a)), true);
        text_c(t.sub, R(-slide * 0.4f, 322, kW, 24), 18, alpha(col::muted, static_cast<int>(255 * a)), true);
    }
}

}  // namespace fd::ui
