// Profile selection, main menu, scenario quiz, Wisdom Meter, Codex, card collection and Tutor Report.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.hpp"

namespace fd::ui {

static std::string upper(std::string s) { return to_upper(std::move(s)); }

static bool back_pressed(App& a) { return !a.typing() && a.key(KEY_ESCAPE); }

static bool back_button(App& a) { return a.button(R(40, kH - 56, 170, 40), "<  Back  (Esc)", BtnStyle::Normal, true, 18); }

void App::dispatch() {
    static Screen last_screen = Screen::ProfileSelect;
    if (screen_ != last_screen) { focus_ = nullptr; last_screen = screen_; }
    switch (screen_) {
        case Screen::ProfileSelect: screen_profile_select(); break;
        case Screen::NewProfile: screen_new_profile(); break;
        case Screen::MainMenu: screen_main_menu(); break;
        case Screen::Focus: screen_focus(); break;
        case Screen::Quiz: screen_quiz(); break;
        case Screen::Summary: screen_summary(); break;
        case Screen::Wisdom: screen_wisdom(); break;
        case Screen::Codex: screen_codex(); break;
        case Screen::Collection: screen_collection(); break;
        case Screen::Report: screen_report(); break;
        case Screen::DuelMenu: screen_duel_menu(); break;
        case Screen::Difficulty: screen_difficulty(); break;
        case Screen::Presets: screen_presets(); break;
        case Screen::DeckList: screen_deck_list(); break;
        case Screen::DeckEditor: screen_deck_editor(); break;
        case Screen::DuelIntro: screen_duel_intro(); break;
        case Screen::Mulligan: screen_mulligan(); break;
        case Screen::Duel: screen_duel(); break;
    }
}

void App::login(const std::string& id, const std::string& name, const std::string& cls) {
    profile_ = pm_->load_or_create(id, name, cls);
    deck_builder::ensure(profile_);
    logged_in_ = true;
    if (!pm_->last_warning.empty()) { toast(pm_->last_warning); pm_->last_warning.clear(); }
    screen_ = Screen::MainMenu;
}

// ------------------------------------------------------------- profile select
void App::screen_profile_select() {
    text_c("FAUST'S DECK", R(0, 60, kW, 80), 64, col::text, true);
    text_c("Learn philosophy by playing it", R(0, 140, kW, 30), 20, col::muted);
    const auto students = pm_->list_students();
    const Rectangle box = R((kW - 620) / 2.0f, 200, 620, 420);
    panel(box);
    text("Who is playing?", box.x + 24, box.y + 18, 24, col::text, true);
    const Rectangle list = R(box.x + 20, box.y + 58, box.width - 40, 260);
    scroll_area(list, static_cast<float>(students.size()) * 58, profile_scroll_);
    begin_clip(list);
    for (std::size_t i = 0; i < students.size(); ++i) {
        const Rectangle br = R(list.x, list.y + i * 58 - profile_scroll_, list.width - 14, 50);
        if (br.y + br.height < list.y || br.y > list.y + list.height) continue;
        if (button(br, students[i].second + "   (" + students[i].first + ")", BtnStyle::Normal, true, 20)) {
            const std::string id = students[i].first;
            defer([this, id] { login(id); });
        }
    }
    end_clip();
    if (button(R(box.x + 20, box.y + box.height - 78, 280, 54), "+  New player", BtnStyle::Primary, true, 21)) {
        f_id_.text.clear(); f_name_.text.clear(); f_class_.text.clear();
        screen_ = Screen::NewProfile;
    }
    if (button(R(box.x + box.width - 300, box.y + box.height - 78, 280, 54), "Quit", BtnStyle::Normal, true, 21)) quit_ = true;
}

void App::screen_new_profile() {
    text_c("Welcome! Let's get you set up.", R(0, 70, kW, 50), 36, col::text, true);
    const Rectangle box = R((kW - 620) / 2.0f, 150, 620, 430);
    panel(box);
    float y = box.y + 24;
    text("Short student ID", box.x + 28, y, 17, col::muted, true);
    text_r("e.g. initials + class no., like ac2b15", box.x + box.width - 28, y + 1, 15, col::muted);
    y += 26;
    const bool s1 = text_field(R(box.x + 28, y, box.width - 56, 48), f_id_, "ac2b15");
    y += 74;
    text("Your name", box.x + 28, y, 17, col::muted, true);
    y += 26;
    const bool s2 = text_field(R(box.x + 28, y, box.width - 56, 48), f_name_, "Name shown in the game");
    y += 74;
    text("Class (optional)", box.x + 28, y, 17, col::muted, true);
    y += 26;
    const bool s3 = text_field(R(box.x + 28, y, box.width - 56, 48), f_class_, "2B");
    y += 76;
    std::string id;
    for (char ch : f_id_.text) if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') id.push_back(ch);
    const bool can_create = !id.empty();
    const bool go = button(R(box.x + 28, y, 330, 52), "Create profile", BtnStyle::Primary, can_create, 21) ||
                    ((s1 || s2 || s3) && can_create);
    if (go) {
        const std::string name = trim(f_name_.text), cls = trim(f_class_.text);
        defer([this, id, name, cls] { login(id, name, cls); });
    }
    if (!pm_->list_students().empty()) {
        if (button(R(box.x + box.width - 28 - 200, y, 200, 52), "Back", BtnStyle::Normal, true, 20)) screen_ = Screen::ProfileSelect;
    }
    text("Letters, numbers, - and _ only for the ID.", box.x + 28, box.y + box.height - 34, 14, col::muted);
}

// ------------------------------------------------------------------ main menu
void App::screen_main_menu() {
    deck_builder::ensure(profile_);
    title_bar("FAUST'S DECK", "Main menu");
    const int cards = collection::collection_size(profile_);
    const bool duel_ok = collection::duel_unlocked(profile_);

    struct Item { const char* label; Screen target; BtnStyle style; std::string extra; };
    const std::vector<Item> items = {
        {"1   Play a Round", Screen::Quiz, BtnStyle::Primary, ""},
        {"2   Wisdom Meter", Screen::Wisdom, BtnStyle::Normal, ""},
        {"3   Codex / Glossary", Screen::Codex, BtnStyle::Normal, ""},
        {"4   My Cards", Screen::Collection, BtnStyle::Normal, std::to_string(cards) + "/60"},
        {"5   Duel Mode", Screen::DuelMenu, duel_ok ? BtnStyle::Good : BtnStyle::Normal, duel_ok ? "ready" : "locked " + std::to_string(cards) + "/40"},
        {"6   Tutor Report", Screen::Report, BtnStyle::Normal, ""},
    };
    float y = 110;
    int chosen = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Rectangle r = R(40, y, 440, 58);
        if (button(r, items[i].label, items[i].style, true, 22)) chosen = static_cast<int>(i);
        if (!items[i].extra.empty()) text_r(items[i].extra, r.x + r.width - 16, r.y + 20, 15, duel_ok || i != 4 ? col::muted : col::warn, true);
        y += 68;
    }
    if (button(R(40, y + 6, 214, 44), "9   Switch player", BtnStyle::Normal, true, 18)) { logged_in_ = false; screen_ = Screen::ProfileSelect; }
    if (button(R(266, y + 6, 214, 44), "0   Exit", BtnStyle::Danger, true, 18)) { pm_->save_profile(profile_); quit_ = true; }

    if (!typing()) {
        for (int k = 0; k < 6; ++k) if (key(KEY_ONE + k)) chosen = k;
        if (key(KEY_NINE)) { logged_in_ = false; screen_ = Screen::ProfileSelect; }
        if (key(KEY_ZERO)) { pm_->save_profile(profile_); quit_ = true; }
    }
    if (chosen >= 0) {
        if (items[chosen].target == Screen::Quiz) {
            quiz_ = QuizState{};
            screen_ = Screen::Focus;
        } else if (items[chosen].target == Screen::Report) {
            has_report_ = false;
            screen_ = Screen::Report;
        } else if (items[chosen].target == Screen::DuelMenu) {
            screen_ = Screen::DuelMenu;
        } else {
            coll_detail_ = nullptr;
            screen_ = items[chosen].target;
        }
    }

    // progress panel
    const Rectangle p = R(520, 110, 720, 580);
    panel(p);
    text("Your progress", p.x + 24, p.y + 18, 24, col::text, true);
    float ry = p.y + 66;
    for (const auto& s : engine_->all_schools()) {
        const bool unlocked = progress::is_unlocked(profile_, s.id);
        const Color sc = school_color(s.id);
        school_icon(s.id, p.x + 40, ry + 14, 12, unlocked ? sc : Color{70, 78, 100, 255});
        text(unlocked ? s.name : "???", p.x + 66, ry + 3, 20, unlocked ? col::text : col::muted, unlocked);
        wisdom_bar(R(p.x + 270, ry + 5, 330, 18), unlocked ? progress::get_percentage(profile_, s.id) : 0, unlocked ? sc : Color{70, 78, 100, 255});
        ry += 44;
    }
    ry += 14;
    DrawRectangle(static_cast<int>(p.x + 24), static_cast<int>(ry), static_cast<int>(p.width - 48), 1, alpha(col::border, 150));
    ry += 18;
    text("Card collection", p.x + 24, ry, 20, col::text, true);
    text_r(std::to_string(cards) + " / 60 cards", p.x + p.width - 24, ry + 2, 17, col::muted, true);
    ry += 34;
    bar(R(p.x + 24, ry, p.width - 48, 16), std::min(1.0f, cards / static_cast<float>(collection::COLLECTION_UNLOCK_THRESHOLD)), duel_ok ? col::good : col::accent);
    ry += 26;
    text(duel_ok ? "Duel Mode is unlocked. Build a deck and challenge the bot!"
                 : std::to_string(collection::COLLECTION_UNLOCK_THRESHOLD - cards) + " more cards to unlock Duel Mode (answer scenarios correctly to collect them).",
         p.x + 24, ry, 16, duel_ok ? col::good : col::muted);
    ry += 34;
    text_wrapped("Tip: answer a scenario correctly to earn its card. Answer the same scenario correctly again to level the card up (max Level 3).",
                 R(p.x + 24, ry, p.width - 48, 60), 16, col::muted);
}

// ------------------------------------------------------------------- focus
void App::screen_focus() {
    title_bar("Choose a focus school", "Pick one school of thought to practise, or let the game surprise you.");
    std::vector<const School*> unlocked;
    for (const auto& s : engine_->all_schools()) if (progress::is_unlocked(profile_, s.id)) unlocked.push_back(&s);
    if (unlocked.empty()) {            // brand-new profile: nothing to choose from yet
        defer([this] { start_quiz_session(""); });
        return;
    }
    float x0 = 40, y0 = 130;
    const float w = 380, h = 90;
    int i = 0;
    auto place = [&](int idx) { return R(x0 + (idx % 3) * (w + 30), y0 + (idx / 3) * (h + 24), w, h); };
    if (button(place(i++), "Random school (any)", BtnStyle::Primary, true, 22)) defer([this] { start_quiz_session(""); });
    for (const School* s : unlocked) {
        const Rectangle r = place(i++);
        if (button(r, "", BtnStyle::Normal, true)) {
            const std::string id = s->id;
            defer([this, id] { start_quiz_session(id); });
        }
        school_icon(s->id, r.x + 38, r.y + r.height / 2, 17, school_color(s->id));
        text(s->name, r.x + 72, r.y + 18, 22, col::text, true);
        wisdom_bar(R(r.x + 72, r.y + 54, r.width - 150, 12), progress::get_percentage(profile_, s->id), school_color(s->id));
    }
    if (back_button(*this) || back_pressed(*this)) screen_ = quiz_.round_no > 0 && quiz_.card ? Screen::Quiz : Screen::MainMenu;
}

// -------------------------------------------------------------------- quiz
void App::start_quiz_session(const std::string& focus) {
    if (quiz_.session.session_id.empty() || quiz_.round_no == 0) {
        engine_->reset_session();
        quiz_ = QuizState{};
        quiz_.session = pm_->start_session(profile_.student_id);
    }
    quiz_.focus = focus;
    next_round();
    screen_ = Screen::Quiz;
}

void App::next_round() {
    auto drawn = engine_->draw_card(quiz_.focus);
    quiz_.school = drawn.first;
    quiz_.card = drawn.second;
    quiz_.newly_unlocked = progress::unlock_school(profile_, quiz_.school->id);
    quiz_.answered = false;
    quiz_.picked = 0;
    quiz_.has_coll = false;
    quiz_.glossary_open = false;
    ++quiz_.round_no;
    quiz_.t_start = GetTime();
}

void App::submit_answer(char choice) {
    QuizState& q = quiz_;
    if (q.answered || !q.card) return;
    q.answered = true;
    q.picked = choice;
    q.time_taken = std::nearbyint((GetTime() - q.t_start) * 10.0) / 10.0;
    q.res = evaluator::evaluate(*q.card, choice);
    pm_->record_round(profile_.student_id, q.session, *q.card, choice, q.res, q.time_taken);
    progress::award_points(profile_, q.school->id, q.res.points);
    if (q.res.is_correct) {
        q.coll = collection::record_correct_answer(profile_, q.card->id);
        q.has_coll = true;
    }
    pm_->save_profile(profile_);
}

void App::draw_codex_body(Rectangle r, float& scroll) {
    const float w = r.width - 24;
    std::vector<float> heights;
    float content = 4;
    for (const auto& s : engine_->all_schools()) {
        const bool unlocked = progress::is_unlocked(profile_, s.id);
        float h = 58;
        if (unlocked) h += wrapped_height("Core idea: " + s.core_belief, w - 40, 17, false, 2) + wrapped_height(s.description, w - 40, 16, false, 2) + 18;
        else h += 30;
        heights.push_back(h);
        content += h + 12;
    }
    scroll_area(r, content, scroll);
    begin_clip(r);
    float y = r.y + 4 - scroll;
    std::size_t i = 0;
    for (const auto& s : engine_->all_schools()) {
        const float h = heights[i++];
        const bool unlocked = progress::is_unlocked(profile_, s.id);
        const Color sc = school_color(s.id);
        panel(R(r.x, y, w, h), col::panel2, unlocked ? mix(sc, col::black, 0.4f) : col::border, 10, 1.5f);
        school_icon(s.id, r.x + 34, y + 30, 14, unlocked ? sc : Color{70, 78, 100, 255});
        text(unlocked ? s.name : "???", r.x + 64, y + 14, 24, unlocked ? col::text : col::muted, true);
        if (unlocked) {
            float ty = y + 54;
            ty += text_wrapped("Core idea: " + s.core_belief, R(r.x + 20, ty, w - 40, 200), 17, col::warn, false, 2) + 6;
            text_wrapped(s.description, R(r.x + 20, ty, w - 40, 300), 16, col::text, false, 2);
        } else {
            text("Not met yet - play a round to find this school.", r.x + 20, y + 54, 16, col::muted);
        }
        y += h + 12;
    }
    end_clip();
}

void App::screen_quiz() {
    QuizState& q = quiz_;
    if (!q.card) { screen_ = Screen::MainMenu; return; }
    const Card& card = *q.card;
    const School& sch = *q.school;
    const Color sc = school_color(sch.id);
    const bool glossary = q.glossary_open;
    blocked_ = glossary;

    // header
    text("Round " + std::to_string(q.round_no), 40, 14, 30, col::text, true);
    school_icon(sch.id, 54, 66, 11, sc);
    text("THE " + upper(sch.name) + " SCHOOL", 76, 56, 19, sc, true);
    text("Wisdom Meter", 880, 14, 16, col::muted, true);
    wisdom_bar(R(880, 40, 300, 16), progress::get_percentage(profile_, sch.id), sc);
    if (q.newly_unlocked) text(fit("New school unlocked: " + sch.name + "!  (see the Codex)", 14, 360, true), 880, 62, 14, col::warn, true);

    // scenario
    panel(R(40, 100, 800, 160));
    text("SCENARIO", 60, 112, 13, col::muted, true);
    text_wrapped(card.scenario, R(60, 134, 760, 120), 23, col::text);

    // choices
    int chosen = -1;
    for (int i = 0; i < 4; ++i) {
        const char letter = static_cast<char>('A' + i);
        const Rectangle r = R(40, 276.0f + i * 70, 800, 62);
        const bool hov = !q.answered && hover(r);
        if (hov) hot_ = true;
        Color fill = hov ? Color{50, 62, 96, 255} : col::panel2, edge = hov ? col::accent : col::border;
        if (q.answered) {
            if (letter == q.res.correct_choice) { fill = Color{34, 76, 56, 255}; edge = col::good; }
            else if (letter == q.picked) { fill = Color{84, 38, 44, 255}; edge = col::bad; }
            else { fill = Color{26, 30, 44, 255}; edge = Color{44, 50, 70, 255}; }
        }
        panel(r, fill, edge, 10, 2.0f);
        DrawCircleV(Vector2{r.x + 32, r.y + r.height / 2}, 17, q.answered && letter == q.res.correct_choice ? col::good : q.answered && letter == q.picked ? col::bad : col::accent);
        text_c(std::string(1, letter), R(r.x + 15, r.y + r.height / 2 - 17, 34, 34), 19, col::white, true);
        const auto lines = wrap(card.choices[i], 19, r.width - 90);
        const float total = lines.size() * 24.0f;
        float ty = r.y + (r.height - total) / 2;
        for (const auto& ln : lines) { text(ln, r.x + 62, ty, 19, q.answered && letter != q.res.correct_choice && letter != q.picked ? col::muted : col::text); ty += 24; }
        if (hov && in_.pressed) chosen = i;
    }
    if (!q.answered && !typing() && !glossary) {
        for (int i = 0; i < 4; ++i) if (key(KEY_A + i)) chosen = i;
    }
    if (chosen >= 0 && !q.answered) submit_answer(static_cast<char>('A' + chosen));

    // feedback
    if (!q.answered) {
        text("Choose the response that best fits this school's reasoning.   A-D to answer  -  G = Codex  -  Esc = exit", 40, 566, 15, col::muted);
    } else {
        const Rectangle fb = R(40, 560, 800, 140);
        panel(fb, Color{24, 30, 44, 255}, q.res.is_correct ? col::good : col::warn, 10, 2.0f);
        if (q.res.is_correct) text("Correct!", fb.x + 18, fb.y + 10, 24, col::good, true);
        else text(std::string("Not quite. The best fit was option ") + q.res.correct_choice + ".", fb.x + 18, fb.y + 10, 22, col::warn, true);
        text_wrapped(q.res.explanation, R(fb.x + 18, fb.y + 44, fb.width - 36, 64), 17, col::text);
        text(q.res.is_correct ? "+" + std::to_string(q.res.points) + " points" : "+" + std::to_string(q.res.points) + " points for participation",
             fb.x + 18, fb.y + fb.height - 28, 16, q.res.is_correct ? col::good : col::muted, true);
        text_r("Time: " + std::to_string(static_cast<int>(q.time_taken)) + "s", fb.x + fb.width - 18, fb.y + fb.height - 28, 15, col::muted);
    }

    // right column
    const Rectangle info = R(864, 100, 376, 200);
    panel(info);
    school_icon(sch.id, info.x + 30, info.y + 30, 12, sc);
    text(sch.name, info.x + 54, info.y + 16, 22, col::text, true);
    text_wrapped("Core idea: " + sch.core_belief, R(info.x + 18, info.y + 54, info.width - 36, 140), 16, col::muted);

    if (q.answered && q.has_coll) {
        const bool is_new = q.coll.status == "new", up = q.coll.status == "levelup";
        const std::string cap = is_new ? "NEW CARD OBTAINED!" : up ? "LEVEL UP!  Level " + std::to_string(q.coll.level) : "Card already at max level";
        text_c(cap, R(864, 312, 376, 26), 20, is_new || up ? col::warn : col::muted, true);
        CardView v = view_of(card, q.coll.level);
        draw_card_face(R(960, 346, 184, 252), v, CardDraw{});
    } else if (q.answered && !q.res.is_correct) {
        text_wrapped("Answer this scenario correctly to earn its card and add it to your Duel collection.", R(884, 330, 336, 80), 16, col::muted);
    }

    const bool next_hit = q.answered && button(R(864, 626, 376, 40), "Next round  (Enter)", BtnStyle::Primary, true, 19);
    if (!q.answered) {
        if (button(R(864, 626, 376, 40), "Open Codex  (G)", BtnStyle::Normal, true, 19)) q.glossary_open = true;
    }
    const bool change = q.answered && button(R(864, 672, 182, 36), "Change school (F)", BtnStyle::Normal, true, 16);
    const bool leave = button(q.answered ? R(1058, 672, 182, 36) : R(864, 672, 376, 36), "Exit to menu  (0)", BtnStyle::Danger, true, 16);

    if (!typing() && !glossary) {
        if (key(KEY_G)) q.glossary_open = true;
        if (q.answered && (key(KEY_ENTER) || key(KEY_SPACE) || key(KEY_N))) { defer([this] { next_round(); }); }
        if (q.answered && key(KEY_F)) { screen_ = Screen::Focus; }
    }
    if (next_hit) defer([this] { next_round(); });
    if (change) screen_ = Screen::Focus;
    if (leave || (!typing() && !glossary && (key(KEY_ZERO) || key(KEY_ESCAPE)))) { screen_ = Screen::Summary; }

    if (glossary) {
        blocked_ = false;
        dim_background(180);
        const Rectangle box = R(130, 50, 1020, 620);
        panel(box, col::panel, col::accent, 12, 2.0f);
        text("Codex / Glossary", box.x + 24, box.y + 16, 28, col::text, true);
        if (button(R(box.x + box.width - 190, box.y + 14, 166, 38), "Close  (G)", BtnStyle::Normal, true, 17) || key(KEY_G) || key(KEY_ESCAPE)) q.glossary_open = false;
        draw_codex_body(R(box.x + 20, box.y + 66, box.width - 40, box.height - 84), q.glossary_scroll);
    }
}

void App::screen_summary() {
    title_bar("Session summary", "Nice work. Here's how this session went.");
    const QuizState& q = quiz_;
    const Rectangle p = R(40, 110, 560, 330);
    panel(p);
    text("Rounds played", p.x + 28, p.y + 28, 20, col::muted);
    text(std::to_string(q.session.rounds_played), p.x + 28, p.y + 54, 56, col::text, true);
    text("Points earned this session", p.x + 300, p.y + 28, 20, col::muted);
    text(std::to_string(q.session.total_score), p.x + 300, p.y + 54, 56, col::good, true);
    text("Lifetime points", p.x + 28, p.y + 150, 20, col::muted);
    text(std::to_string(profile_.lifetime_points), p.x + 28, p.y + 176, 36, col::text, true);
    text("Cards collected", p.x + 300, p.y + 150, 20, col::muted);
    text(std::to_string(collection::collection_size(profile_)) + " / 60", p.x + 300, p.y + 176, 36, col::text, true);
    text(collection::duel_unlocked(profile_) ? "Duel Mode is unlocked!" : std::to_string(collection::COLLECTION_UNLOCK_THRESHOLD - collection::collection_size(profile_)) + " more cards to unlock Duel Mode",
         p.x + 28, p.y + 262, 18, collection::duel_unlocked(profile_) ? col::good : col::warn, true);

    const Rectangle w = R(640, 110, 600, 330);
    panel(w);
    text("Wisdom Meter", w.x + 24, w.y + 18, 22, col::text, true);
    float y = w.y + 62;
    for (const auto& s : engine_->all_schools()) {
        const bool u = progress::is_unlocked(profile_, s.id);
        school_icon(s.id, w.x + 36, y + 12, 10, u ? school_color(s.id) : Color{70, 78, 100, 255});
        text(u ? s.name : "???", w.x + 60, y + 2, 18, u ? col::text : col::muted, u);
        wisdom_bar(R(w.x + 250, y + 4, 250, 14), u ? progress::get_percentage(profile_, s.id) : 0, u ? school_color(s.id) : Color{70, 78, 100, 255});
        y += 42;
    }
    if (button(R(40, 470, 280, 56), "Back to menu", BtnStyle::Primary, true, 21) || (!typing() && (key(KEY_ENTER) || key(KEY_ESCAPE)))) {
        quiz_ = QuizState{};
        screen_ = Screen::MainMenu;
    }
}

void App::screen_wisdom() {
    title_bar("Wisdom Meter", "How well you know each school of thought.");
    float y = 116;
    for (const auto& s : engine_->all_schools()) {
        const bool u = progress::is_unlocked(profile_, s.id);
        const Color sc = u ? school_color(s.id) : Color{70, 78, 100, 255};
        panel(R(40, y, 1200, 72), col::panel, u ? mix(sc, col::black, 0.5f) : col::border);
        school_icon(s.id, 84, y + 36, 18, sc);
        text(u ? s.name : "???", 124, y + 14, 26, u ? col::text : col::muted, true);
        text(u ? "Core idea: " + s.core_belief : "Not met yet", 124, y + 44, 15, col::muted);
        wisdom_bar(R(640, y + 26, 460, 20), u ? progress::get_percentage(profile_, s.id) : 0, sc);
        y += 82;
    }
    text("Lifetime points: " + std::to_string(profile_.lifetime_points), 40, y + 8, 22, col::text, true);
    if (back_button(*this) || back_pressed(*this)) screen_ = Screen::MainMenu;
}

void App::screen_codex() {
    title_bar("Codex / Glossary", "The schools of thought you have met so far.");
    draw_codex_body(R(40, 100, 1200, 540), codex_scroll_);
    if (back_button(*this) || back_pressed(*this)) screen_ = Screen::MainMenu;
}

// -------------------------------------------------------------- collection
void App::screen_collection() {
    const int total = collection::collection_size(profile_);
    title_bar("My Cards", "Every scenario you answer correctly becomes a Duel card. Click a card to inspect it.");
    float x = 40;
    const float colw = 190, gap = 12, tile_h = 43, tile_gap = 5;
    bool any_open = coll_detail_ != nullptr;
    blocked_ = any_open;
    for (const auto& s : engine_->all_schools()) {
        int owned = 0;
        std::vector<const Card*> cards;
        for (const auto& c : engine_->cards()) if (c.school_id == s.id) { cards.push_back(&c); if (profile_.collection.count(c.id)) ++owned; }
        const Color sc = school_color(s.id);
        school_icon(s.id, x + 12, 106, 9, sc);
        text(s.name, x + 30, 96, 17, col::text, true);
        text_r(std::to_string(owned) + "/10", x + colw, 98, 14, col::muted);
        float y = 124;
        for (const Card* c : cards) {
            const Rectangle r = R(x, y, colw, tile_h);
            const int lvl = collection::level_of(profile_, c->id);
            const bool has = lvl > 0;
            const bool hov = hover(r);
            if (hov) hot_ = true;
            panel(r, has ? col::panel2 : Color{22, 25, 36, 255}, hov ? col::accent : has ? mix(sc, col::black, 0.45f) : Color{40, 46, 64, 255}, 8, hov ? 2.5f : 1.5f);
            if (has) {
                DrawRectangle(static_cast<int>(r.x + 4), static_cast<int>(r.y + 8), 4, static_cast<int>(r.height - 16), type_color(c->card_type));
                text(fit(c->collectible, 15, colw - 22, true), r.x + 14, r.y + 5, 15, col::text, true);
                const char* tl = duel_cards::type_label(c->card_type);
                text(tl, r.x + 14, r.y + 24, 12, type_color(c->card_type));
                for (int k = 0; k < 3; ++k) DrawCircleV(Vector2{r.x + colw - 20 + k * 0.0f - (2 - k) * 12.0f, r.y + 31}, 4.0f, k < lvl ? col::warn : Color{54, 60, 84, 255});
            } else {
                text("???", r.x + 14, r.y + 12, 16, Color{84, 92, 116, 255}, true);
            }
            if (clicked(r)) coll_detail_ = c;
            y += tile_h + tile_gap;
        }
        x += colw + gap;
    }
    text("Cards collected: " + std::to_string(total) + " / 60", 640, kH - 50, 18, col::muted, true);
    if (!any_open && (back_button(*this) || back_pressed(*this))) screen_ = Screen::MainMenu;

    if (any_open) {
        blocked_ = false;
        dim_background(190);
        const Card& c = *coll_detail_;
        const int owned_lvl = collection::level_of(profile_, c.id);
        const Rectangle box = R(190, 40, 900, 640);
        panel(box, col::panel, col::accent, 12, 2.0f);
        const int show_lvl = owned_lvl > 0 ? owned_lvl : 1;
        if (owned_lvl == 0) {
            text("Not collected yet", box.x + 28, box.y + 24, 26, col::text, true);
            text_wrapped("Answer this scenario correctly to add it to your collection. You'll find it in the " + engine_->get_school(c.school_id)->name + " school.",
                         R(box.x + 28, box.y + 66, box.width - 56, 100), 18, col::muted);
        } else {
            draw_card_face(R(box.x + 28, box.y + 28, 210, 290), view_of(c, show_lvl), CardDraw{});
            CardView v = view_of(c, show_lvl);
            draw_detail(R(box.x + 262, box.y + 28, box.width - 290, 290), &v, "");
            text("Scenario", box.x + 28, box.y + 340, 17, col::accent, true);
            float y = box.y + 366;
            y += text_wrapped(c.scenario, R(box.x + 28, y, box.width - 56, 100), 16, col::text, false, 2) + 8;
            text(std::string("Best response: ") + c.correct + ")  " + c.choices[c.correct - 'A'], box.x + 28, y, 16, col::good, true);
            y += 26;
            text_wrapped(c.explanation, R(box.x + 28, y, box.width - 56, 120), 15, col::muted, false, 2);
        }
        if (button(R(box.x + box.width - 190, box.y + box.height - 58, 166, 42), "Close  (Esc)", BtnStyle::Normal, true, 18) || key(KEY_ESCAPE) || (in_.pressed && !CheckCollisionPointRec(in_.mouse, box)))
            coll_detail_ = nullptr;
    }
}

// ------------------------------------------------------------------ report
void App::screen_report() {
    title_bar("Tutor Report", "A progress summary a tutor can read at a glance.");
    if (!has_report_) {
        const LogData log = pm_->get_log(profile_.student_id);
        report_ = report_generator::generate_report(profile_, log, engine_->all_schools(), root_ + "reports");
        has_report_ = true;
    }
    const Rectangle p = R(40, 106, 1200, 460);
    panel(p);
    const float cx[5] = {p.x + 30, p.x + 400, p.x + 560, p.x + 720, p.x + 900};
    text("School", cx[0], p.y + 18, 17, col::muted, true);
    text("Attempted", cx[1], p.y + 18, 17, col::muted, true);
    text("Correct", cx[2], p.y + 18, 17, col::muted, true);
    text("Accuracy", cx[3], p.y + 18, 17, col::muted, true);
    text("Wisdom", cx[4], p.y + 18, 17, col::muted, true);
    float y = p.y + 54;
    for (const auto& r : report_.rows) {
        const Color sc = school_color(r.school_id);
        school_icon(r.school_id, cx[0] + 10, y + 14, 9, sc);
        text(r.school_name, cx[0] + 32, y + 3, 21, col::text, true);
        text(std::to_string(r.attempted), cx[1], y + 3, 21, col::text);
        text(std::to_string(r.correct), cx[2], y + 3, 21, col::text);
        char buf[32];
        if (r.accuracy_pct) std::snprintf(buf, sizeof buf, "%.1f%%", *r.accuracy_pct); else std::snprintf(buf, sizeof buf, "n/a");
        const bool weak = r.accuracy_pct && *r.accuracy_pct < 80;
        text(buf, cx[3], y + 3, 21, !r.accuracy_pct ? col::muted : weak ? col::warn : col::good, true);
        wisdom_bar(R(cx[4], y + 8, 180, 12), r.wisdom_pct, sc);
        y += 46;
    }
    DrawRectangle(static_cast<int>(p.x + 24), static_cast<int>(y + 4), static_cast<int>(p.width - 48), 1, alpha(col::border, 160));
    y += 18;
    int rounds = 0, correct = 0;
    for (const auto& r : report_.rows) { rounds += r.attempted; correct += r.correct; }
    text("Total rounds: " + std::to_string(rounds) + "     Lifetime points: " + std::to_string(profile_.lifetime_points) +
         "     Cards collected: " + std::to_string(collection::collection_size(profile_)) + "/60", p.x + 30, y, 18, col::text);
    y += 34;
    std::string weak;
    for (std::size_t i = 0; i < report_.weakest.size(); ++i) weak += (i ? ", " : "") + report_.weakest[i];
    text(report_.weakest.empty() ? "Suggested focus for next lesson: no clear weak area yet (or not enough data)."
                                 : "Suggested focus for next lesson: " + weak, p.x + 30, y, 20, report_.weakest.empty() ? col::muted : col::warn, true);
    text("Saved: " + report_.txt_path, 40, 580, 14, col::muted);
    text("Saved: " + report_.csv_path, 40, 600, 14, col::muted);
    if (button(R(kW - 40 - 220, kH - 56, 220, 40), "Regenerate", BtnStyle::Normal, true, 18)) has_report_ = false;
    if (back_button(*this) || back_pressed(*this)) screen_ = Screen::MainMenu;
}

}  // namespace fd::ui
