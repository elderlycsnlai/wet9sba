#pragma once
// raylib front-end for Faust's Deck. The game rules live in src/engine (no graphics);
// this layer only draws and turns clicks into engine calls.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "raylib.h"

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

namespace fd::ui {

constexpr int kW = 1280;   // logical layout size. Everything is drawn at the window's real pixel size (scaled, never stretched)
constexpr int kH = 720;

namespace col {
inline constexpr Color bg{15, 17, 26, 255};
inline constexpr Color panel{26, 30, 44, 255};
inline constexpr Color panel2{36, 42, 62, 255};
inline constexpr Color border{78, 90, 124, 255};
inline constexpr Color text{232, 236, 247, 255};
inline constexpr Color muted{176, 186, 212, 255};
inline constexpr Color accent{92, 156, 255, 255};
inline constexpr Color good{98, 208, 142, 255};
inline constexpr Color warn{246, 198, 92, 255};
inline constexpr Color bad{240, 108, 108, 255};
inline constexpr Color barback{18, 21, 32, 255};
inline constexpr Color white{255, 255, 255, 255};
inline constexpr Color black{0, 0, 0, 255};
}  // namespace col

inline Rectangle R(float x, float y, float w, float h) { return Rectangle{x, y, w, h}; }
inline Color alpha(Color c, int a) { c.a = static_cast<unsigned char>(a); return c; }
inline Color mix(Color a, Color b, float t) {
    auto m = [&](unsigned char x, unsigned char y) { return static_cast<unsigned char>(x + (y - x) * t); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

enum class Screen {
    ProfileSelect, NewProfile, MainMenu, Focus, Quiz, Summary, Wisdom, Codex, Collection, Report,
    DuelMenu, Difficulty, Presets, DeckList, DeckEditor, DuelIntro, Mulligan, Duel
};
enum class BtnStyle { Normal, Primary, Good, Danger, Ghost };

struct TextField { std::string text; std::size_t max_len = 24; };

struct Options {
    std::string script_path;               // --script file: drive the UI without a human (tests / screenshots)
    std::optional<std::uint64_t> seed;     // --seed N
    std::string root_override;             // --root DIR
    int win_w = 0, win_h = 0;              // --size WxH (0 = pick a size that fits the monitor)
};

// Everything the card renderer / detail panel needs, whatever zone the card is in.
struct CardView {
    bool valid = false, hidden = false;
    std::string name, school_id, card_id, tag;
    CardType type = CardType::Monster;
    int level = 1, cost = 0, atk = 0, hp = 0, max_hp = 0, pending_penalty = 0;
    bool can_attack = false, revived = false, show_cost = true, on_field = false;
    bool frozen = false, silenced = false, revealed = false;
    bool grave_ready = false;              // a Graveyard card whose effect can be used right now
    std::string note;                      // e.g. "In your Graveyard"
};
struct CardDraw {
    bool selected = false, hovered = false, dim = false, target = false, ready = false, face_down = false;
};

enum class DuelOverlay { None, Zone, History, Glossary, Result, Concede };

// Table layout shared by the board drawing and the animations (logical 1280x720 units).
namespace lay {
constexpr float kMainX = 12, kMainW = 940, kSideX = 964, kSideW = 304;
constexpr float kOppHudY = 38, kOppZoneY = 80, kOppFieldY = 144, kBannerY = 266;
constexpr float kPlFieldY = 292, kPlZoneY = 414, kPlHudY = 478, kHandY = 522;
constexpr float kHandCW = 120, kHandCH = 178;
inline Rectangle hud(Side s) { return R(kMainX, s == Side::Bot ? kOppHudY : kPlHudY, kMainW, 38); }
inline float zone_y(Side s) { return s == Side::Bot ? kOppZoneY : kPlZoneY; }
inline Rectangle deck_pile(Side s) { return R(12, zone_y(s), 62, 56); }
inline Rectangle grave_pile(Side s) { return R(80, zone_y(s), 62, 56); }
inline Rectangle banish_pile(Side s) { return R(148, zone_y(s), 62, 56); }
inline Rectangle trap_slot(Side s, int i) { return R(228 + i * 104.0f, zone_y(s), 96, 56); }
inline Rectangle monster_slot(Side s, int i) { return R(208 + i * 148.0f, s == Side::Bot ? kOppFieldY : kPlFieldY, 140, 118); }
inline float hand_step(int n) { return n > 1 ? std::min(kHandCW + 10.0f, (kMainW - kHandCW - 8) / (n - 1)) : 0.0f; }
inline Rectangle hand_slot(int i, int n) {
    const float step = hand_step(n);
    const float x0 = kMainX + (kMainW - (n > 0 ? kHandCW + step * (n - 1) : 0)) / 2;
    return R(x0 + i * step, kHandY + 14, kHandCW, kHandCH);
}
inline Rectangle opp_hand_anchor() { return R(kMainX + kMainW - 84, kOppHudY + 1, 26, 36); }   // where the opponent's hand "lives"
inline Rectangle center_stage() { return R(kMainX + kMainW / 2 - 60, kBannerY - 70, 120, 178); }
}  // namespace lay

// ---- short-lived visual effects used by the duel animations (see anim.cpp)
struct Fly { CardView view; bool face_down = false; Rectangle a{}, b{}; float t = 0, dur = 0.4f, spin = 0, end_alpha = 1.0f; Color glow{0, 0, 0, 0}; };
struct Spark { Vector2 p{}, v{}; float t = 0, life = 0.6f, size = 4, rot = 0, vrot = 0, gravity = 0; Color c{255, 255, 255, 255}; bool square = true; };
struct Floater { std::string s; Vector2 p{}; float t = 0, dur = 1.0f, size = 22; Color c{255, 255, 255, 255}; };
struct RingFx { Vector2 p{}; float t = 0, dur = 0.5f, r0 = 8, r1 = 60, thick = 3; Color c{255, 255, 255, 255}; };
struct FlashFx { Rectangle r{}; float t = 0, dur = 0.35f; Color c{255, 255, 255, 255}; };
struct BannerFx { CardView view; std::vector<std::string> lines; std::string head; Rectangle from{}; float t = 0, dur = 0.9f; Color c{92, 156, 255, 255}; };
struct CoinFx { bool heads = true; float t = 0, dur = 1.3f; std::string label; };
struct TurnFx { std::string main, sub; Color c{92, 156, 255, 255}; float t = 0, dur = 1.0f; };
struct LungeFx { CardView view; Rectangle from{}, to{}; float t = 0, dur = 0.6f; };
struct AimFx { Vector2 a{}, b{}; float t = 0, dur = 0.45f; };
struct FxState {
    std::vector<Fly> flies;
    std::vector<Spark> sparks;
    std::vector<Floater> floats;
    std::vector<RingFx> rings;
    std::vector<FlashFx> flashes;
    std::optional<BannerFx> banner;
    std::optional<CoinFx> coin;
    std::optional<TurnFx> turn;
    std::optional<LungeFx> lunge;
    std::optional<AimFx> aim;
    float shake_t = 0, shake_dur = 0, shake_mag = 0;
    bool busy() const { return !flies.empty() || banner || coin || turn || lunge || aim; }
};

struct QuizState {
    SessionRec session;
    std::string focus;                     // school id, "" = random
    int round_no = 0;
    const School* school = nullptr;
    const Card* card = nullptr;
    bool newly_unlocked = false, answered = false;
    char picked = 0;
    evaluator::Result res;
    bool has_coll = false;
    collection::Result coll{"", 0};
    double t_start = 0, time_taken = 0;
    bool glossary_open = false;
    float glossary_scroll = 0;
};

struct DeckUI {
    std::string editing;
    float scroll = 0;
    enum class Modal { None, NewDeck, Rename, ConfirmDelete } modal = Modal::None;
    std::string target;
    TextField name_field;
    CardView hover;
};

struct DuelUI {
    battle::DuelState st;
    bot_ai::Difficulty diff = bot_ai::Difficulty::Normal;
    std::string deck_label;
    std::string preset_id;                 // empty = active deck
    Side first = Side::Player;
    std::vector<DeckEntry> player_deck;
    int sel_hand = -1;
    std::string sel_attacker;
    CardView pinned, hover;
    DuelOverlay overlay = DuelOverlay::None;
    double bot_ready_at = 0;
    bool bot_waiting = false;
    std::vector<std::string> summary_lines;
    float log_scroll = 0, hist_scroll = 0, gloss_scroll = 0;
    Side zone_side = Side::Player;
    bool zone_banished = false;
    int zone_sel = -1;
    float zone_scroll = 0;
    std::vector<bool> mull_marks;
    bool conceded = false;
    // ---- animation state
    battle::Board shown;                   // the table as drawn right now (lags the engine while events play)
    std::deque<battle::Event> queue;       // recorded engine events waiting to be played
    bool have_cur = false;
    float cur_t = 0, cur_dur = 0;
    std::string hide_iid;                  // a card that is currently flying; the zone leaves it out
    bool skip = false;                     // Space / click: play the rest of the queue instantly
    int speed = 1;                         // 0 = off, 1 = normal, 2 = fast
    std::size_t log_shown = 0;             // log lines revealed so far (follows the animation)
    float intro_t = 0;                     // coin-toss animation clock on the Get ready screen
    PerSide<float> lp_disp{20.0f, 20.0f}, sh_disp{0.0f, 0.0f};
    FxState fx;
};

class App {
public:
    explicit App(Options opt);
    ~App();
    bool ok() const { return ok_; }
    int run();

    // ------------------------------------------------------------ core (gfx.cpp)
    bool begin_frame();
    void end_frame();
    void defer(std::function<void()> fn) { pending_.push_back(std::move(fn)); }
    void run_pending();
    void toast(const std::string& msg);
    bool key(int k) const { return in_.keys.count(k) > 0; }
    bool typing() const { return focus_ != nullptr; }

    // text
    float frame_dt() const;                // seconds: a fixed 1/60 under the script driver, otherwise the real frame time
    Color on_color(Color bg) const;        // readable text colour for text drawn on `bg`
    void text(const std::string& s, float x, float y, float size, Color c, bool bold = false);
    float text_w(const std::string& s, float size, bool bold = false);
    void text_c(const std::string& s, Rectangle r, float size, Color c, bool bold = false);
    void text_r(const std::string& s, float right_x, float y, float size, Color c, bool bold = false);
    std::string fit(const std::string& s, float size, float max_w, bool bold = false);
    std::vector<std::string> wrap(const std::string& s, float size, float max_w, bool bold = false);
    float text_wrapped(const std::string& s, Rectangle r, float size, Color c, bool bold = false, float gap = 3);
    float wrapped_height(const std::string& s, float width, float size, bool bold = false, float gap = 3);

    // shapes / widgets
    void panel(Rectangle r, Color fill = col::panel, Color border = col::border, float radius = 8.0f, float bw = 1.5f);
    void bar(Rectangle r, float frac, Color fill);
    void school_icon(const std::string& school_id, float cx, float cy, float r, Color c);
    Color school_color(const std::string& school_id) const;
    Color type_color(CardType t) const;
    bool hover(Rectangle r) const;
    bool clicked(Rectangle r) const;
    bool button(Rectangle r, const std::string& label, BtnStyle style = BtnStyle::Normal, bool enabled = true, float size = 20);
    bool text_field(Rectangle r, TextField& f, const char* placeholder);
    void wisdom_bar(Rectangle r, int pct, Color c);
    void begin_clip(Rectangle r);
    void end_clip();
    float scroll_area(Rectangle r, float content_h, float& scroll);   // handles wheel + draws scrollbar
    void dim_background(int alpha_value = 170);
    void title_bar(const std::string& title, const std::string& subtitle = "");

    // cards
    CardView view_of(const CardInstance& i, bool hidden = false) const;
    CardView view_of(const GraveEntry& g, const std::string& note = "") const;
    CardView view_of(const Card& c, int level) const;
    void draw_card_face(Rectangle r, const CardView& v, const CardDraw& d);
    void draw_detail(Rectangle r, const CardView* v, const char* hint);
    void draw_effect_glossary(Rectangle r, float& scroll);

    // ------------------------------------------------------------ screens
    void dispatch();
    // screens_meta.cpp
    void screen_profile_select();
    void screen_new_profile();
    void screen_main_menu();
    void screen_focus();
    void screen_quiz();
    void screen_summary();
    void screen_wisdom();
    void screen_codex();
    void screen_collection();
    void screen_report();
    void login(const std::string& id, const std::string& name = "", const std::string& cls = "");
    void start_quiz_session(const std::string& focus);
    void next_round();
    void submit_answer(char choice);
    void draw_codex_body(Rectangle r, float& scroll);
    // screens_duel.cpp
    void screen_duel_menu();
    void screen_difficulty();
    void screen_presets();
    void screen_deck_list();
    void screen_deck_editor();
    void screen_duel_intro();
    void screen_mulligan();
    void screen_duel();
    void begin_duel_setup(const std::string& preset_id);
    void launch_duel();
    void draw_duel_scene(bool interactive);
    void draw_duel_overlays();
    void bot_turn();
    // anim.cpp
    void duel_pull_events();
    bool duel_animating() const;
    void duel_anim_begin(const battle::Event& ev);
    void duel_anim_update(float dt);
    void duel_anim_draw();
    void duel_anim_reset();
    void drain_duel_animations();
    void draw_coin(Vector2 c, float radius, float u, bool heads);
    CardView view_of_event_card(const battle::Event& ev) const;
    std::string ask_trap_modal(battle::DuelState& s, const std::vector<CardInstance>& eligible);
    void finish_duel_if_over();

    // ------------------------------------------------------------ state
    Options opt_;
    bool ok_ = false;
    bool quit_ = false;
    std::string root_;
    std::unique_ptr<CardEngine> engine_;
    std::unique_ptr<ProfileManager> pm_;
    Profile profile_;
    bool logged_in_ = false;

    Screen screen_ = Screen::ProfileSelect;
    TextField f_id_, f_name_, f_class_;
    float profile_scroll_ = 0;
    QuizState quiz_;
    DeckUI deck_ui_;
    DuelUI duel_;
    const Card* coll_detail_ = nullptr;
    report_generator::Report report_;
    bool has_report_ = false;
    float codex_scroll_ = 0;
    std::string menu_note_;

    // frame state
    struct Input {
        Vector2 mouse{0, 0};
        bool pressed = false, released = false, down = false, right_pressed = false;
        float wheel = 0;
        std::vector<int> chars;
        std::set<int> keys;
    } in_;
    bool blocked_ = false;
    mutable bool hot_ = false;
    const void* focus_ = nullptr;
    double now_ = 0;
    std::string toast_msg_;
    double toast_until_ = 0;
    std::vector<std::function<void()>> pending_;

    // rendering: everything is drawn straight to the window at its real pixel size
    int fb_w_ = kW, fb_h_ = kH;            // framebuffer size in pixels
    Vector2 view_off_{0, 0};               // where the 1280x720 layout starts, in pixels
    float view_scale_ = 1.0f;              // pixels per logical unit
    float font_scale_ = 0.0f;              // scale the font cache was rasterised for (0 = not yet set)
    int font_settle_ = 0;
    std::map<int, Font> font_cache_[2];    // pixel size -> font (regular, bold)
    std::string pending_shot_;
    void clear_fonts();
    const Font& font_for(float size, bool bold);
    void poll_input();

    // script driver
    struct ScriptCmd { std::string op, a, b, rest; };
    std::vector<ScriptCmd> script_;
    std::size_t script_pos_ = 0;
    int script_wait_ = 0;
    bool script_release_ = false;
    bool script_active_ = false;
    Vector2 script_mouse_{0, 0};
    void load_script(const std::string& path);
};

}  // namespace fd::ui
