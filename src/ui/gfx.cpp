// Window, input, text, widgets and card rendering shared by every screen.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app.hpp"
#include "rlgl.h"

namespace fd::ui {
namespace fs = std::filesystem;

static std::string find_root(const std::string& override_root) {
    std::vector<std::string> cands;
    if (!override_root.empty()) cands.push_back(override_root);
    const std::string app = GetApplicationDirectory();
    for (const char* rel : {"", "../", "../../", "../../../"}) cands.push_back(app + rel);
    for (const char* rel : {"./", "../", "../../"}) cands.push_back(rel);
    for (const auto& c : cands) {
        std::error_code ec;
        if (fs::exists(fs::path(c) / "data" / "cards.json", ec)) {
            std::string r = fs::path(c).lexically_normal().generic_string();
            if (r.empty()) r = "./";
            if (r.back() != '/') r += '/';
            return r;
        }
    }
    return "";
}

// ---------------------------------------------------------------- lifecycle
App::App(Options opt) : opt_(std::move(opt)) {
    root_ = find_root(opt_.root_override);
    if (root_.empty()) {
        std::fprintf(stderr, "Could not find the data/ folder (looked next to the program and in parent folders).\n");
        return;
    }
    try {
        engine_ = std::make_unique<CardEngine>(root_ + "data");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return;
    }
    pm_ = std::make_unique<ProfileManager>(root_ + "saves");
    if (opt_.seed) seed_rng(*opt_.seed);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(kW, kH, "Faust's Deck");
    if (!IsWindowReady()) return;
    SetExitKey(KEY_NULL);                 // ESC is "back", not "quit"
    SetTargetFPS(60);
    SetWindowMinSize(640, 360);
    {   // open at a size that suits the monitor (or --size) instead of a fixed 1280x720
        int w = opt_.win_w, h = opt_.win_h;
        const int mon = GetCurrentMonitor();
        const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (w <= 0 && opt_.script_path.empty() && mw > 0 && mh > 0) {
            float s = std::min(0.92f * mw / kW, 0.88f * mh / kH);
            s = std::max(0.8f, std::min(s, 2.5f));
            w = static_cast<int>(std::lround(kW * s));
            h = static_cast<int>(std::lround(kH * s));
        }
        if (w > 0 && h > 0) {
            SetWindowSize(w, h);
            if (mw > 0 && mh > 0) SetWindowPosition(std::max(0, (mw - w) / 2), std::max(0, (mh - h) / 2 - 24));
        }
    }
    if (!opt_.script_path.empty()) load_script(opt_.script_path);
    f_id_.max_len = 16;
    f_name_.max_len = 24;
    f_class_.max_len = 8;
    deck_ui_.name_field.max_len = 24;
    screen_ = pm_->list_students().empty() ? Screen::NewProfile : Screen::ProfileSelect;
    ok_ = true;
}

App::~App() {
    if (IsWindowReady()) {
        clear_fonts();
        CloseWindow();
    }
}

void App::clear_fonts() {
    for (auto& cache : font_cache_) {
        for (auto& kv : cache)
            if (kv.second.texture.id != 0 && kv.second.texture.id != GetFontDefault().texture.id) UnloadFont(kv.second);
        cache.clear();
    }
}

// Fonts are rasterised at the exact pixel size they are drawn at (logical size x window scale), so text
// stays sharp at any resolution instead of being stretched from a 1280x720 picture.
const Font& App::font_for(float size, bool bold) {
    const int px = std::max(6, static_cast<int>(std::lround(size * font_scale_)));
    auto& cache = font_cache_[bold ? 1 : 0];
    const auto it = cache.find(px);
    if (it != cache.end()) return it->second;
    static std::vector<int> cps;
    if (cps.empty()) {
        for (int c = 32; c <= 126; ++c) cps.push_back(c);
        for (int c = 160; c <= 255; ++c) cps.push_back(c);
        for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25BC, 0x25CF, 0x2713, 0x2715})
            cps.push_back(c);
    }
    const std::string path = root_ + (bold ? "assets/fonts/DejaVuSans-Bold.ttf" : "assets/fonts/DejaVuSans.ttf");
    Font f = LoadFontEx(path.c_str(), px, cps.data(), static_cast<int>(cps.size()));
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return cache.emplace(px, f).first->second;
}

float App::frame_dt() const { return script_active_ ? 1.0f / 60.0f : std::min(GetFrameTime(), 0.05f); }

static float luminance(Color c) {
    auto f = [](float v) { v /= 255.0f; return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * f(c.r) + 0.7152f * f(c.g) + 0.0722f * f(c.b);
}
Color App::on_color(Color bg) const {      // whichever of white / near-black has the higher WCAG contrast on `bg`
    const float L = luminance(bg);
    const float vs_white = 1.05f / (L + 0.05f), vs_dark = (L + 0.05f) / (luminance(col::bg) + 0.05f);
    return vs_white >= vs_dark ? col::white : Color{12, 14, 22, 255};
}

// ----------------------------------------------------------- script driver
static int key_from_name(const std::string& name) {
    const std::string u = to_upper(name);
    if (u == "ENTER") return KEY_ENTER;
    if (u == "ESC" || u == "ESCAPE") return KEY_ESCAPE;
    if (u == "BACKSPACE") return KEY_BACKSPACE;
    if (u == "TAB") return KEY_TAB;
    if (u == "SPACE") return KEY_SPACE;
    if (u.size() == 1 && u[0] >= 'A' && u[0] <= 'Z') return KEY_A + (u[0] - 'A');
    if (u.size() == 1 && u[0] >= '0' && u[0] <= '9') return KEY_ZERO + (u[0] - '0');
    return 0;
}

void App::load_script(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        ScriptCmd c;
        std::istringstream ss(line);
        ss >> c.op >> c.a >> c.b;
        const auto sp = line.find(' ');
        c.rest = sp == std::string::npos ? "" : line.substr(sp + 1);
        script_.push_back(c);
    }
    script_active_ = true;
}

void App::poll_input() {
    fb_w_ = std::max(1, GetRenderWidth());
    fb_h_ = std::max(1, GetRenderHeight());
    view_scale_ = std::min(static_cast<float>(fb_w_) / kW, static_cast<float>(fb_h_) / kH);
    view_off_ = {std::floor((fb_w_ - kW * view_scale_) * 0.5f), std::floor((fb_h_ - kH * view_scale_) * 0.5f)};
    if (font_scale_ <= 0.0f) font_scale_ = view_scale_;
    in_ = Input{};
    now_ = GetTime();

    if (script_active_) {
        in_.mouse = script_mouse_;
        if (script_release_) { in_.released = true; script_release_ = false; }
        if (script_wait_ > 0) { --script_wait_; return; }
        if (script_pos_ >= script_.size()) { quit_ = true; return; }
        while (script_pos_ < script_.size()) {
            const ScriptCmd c = script_[script_pos_++];
            if (c.op == "wait") { script_wait_ = std::max(0, std::atoi(c.a.c_str())); break; }
            if (c.op == "move") { script_mouse_ = {std::stof(c.a), std::stof(c.b)}; in_.mouse = script_mouse_; continue; }
            if (c.op == "click") {
                script_mouse_ = {std::stof(c.a), std::stof(c.b)};
                in_.mouse = script_mouse_;
                in_.pressed = in_.down = true;
                script_release_ = true;
                break;
            }
            if (c.op == "key") { if (int k = key_from_name(c.a)) in_.keys.insert(k); break; }
            if (c.op == "type") {
                for (unsigned char ch : c.rest) in_.chars.push_back(ch);
                break;
            }
            if (c.op == "wheel") { in_.wheel = std::stof(c.a); break; }
            if (c.op == "shot") { pending_shot_ = c.a; break; }
            if (c.op == "quit") { quit_ = true; break; }
        }
        return;
    }

    if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
    const Vector2 m = GetMousePosition();                 // window coordinates -> framebuffer pixels -> layout units
    const float rx = GetScreenWidth() > 0 ? static_cast<float>(fb_w_) / static_cast<float>(GetScreenWidth()) : 1.0f;
    const float ry = GetScreenHeight() > 0 ? static_cast<float>(fb_h_) / static_cast<float>(GetScreenHeight()) : 1.0f;
    in_.mouse = {(m.x * rx - view_off_.x) / view_scale_, (m.y * ry - view_off_.y) / view_scale_};
    in_.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in_.released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    in_.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in_.right_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    in_.wheel = GetMouseWheelMove();
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) in_.chars.push_back(c);
    static const int watch[] = {KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_SPACE, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN};
    for (int k : watch)
        if (IsKeyPressed(k) || (k == KEY_BACKSPACE && IsKeyPressedRepeat(k))) in_.keys.insert(k == KEY_KP_ENTER ? KEY_ENTER : k);
    for (int k = KEY_A; k <= KEY_Z; ++k) if (IsKeyPressed(k)) in_.keys.insert(k);
    for (int k = KEY_ZERO; k <= KEY_NINE; ++k) if (IsKeyPressed(k)) in_.keys.insert(k);
}

// -------------------------------------------------------------------- frames
bool App::begin_frame() {
    if (quit_) return false;
    if (WindowShouldClose()) { quit_ = true; return false; }
    poll_input();
    if (quit_) return false;
    if (std::fabs(view_scale_ - font_scale_) > 0.04f * font_scale_) {      // window resized: re-rasterise the fonts once it settles
        if (++font_settle_ >= 12) { clear_fonts(); font_scale_ = view_scale_; font_settle_ = 0; }
    } else {
        font_settle_ = 0;
    }
    BeginDrawing();
    ClearBackground(col::bg);
    rlPushMatrix();
    rlTranslatef(view_off_.x, view_off_.y, 0.0f);
    rlScalef(view_scale_, view_scale_, 1.0f);
    hot_ = false;
    blocked_ = false;
    return true;
}

void App::toast(const std::string& msg) {
    toast_msg_ = msg;
    toast_until_ = GetTime() + 3.0;
}

void App::end_frame() {
    if (now_ < toast_until_ && !toast_msg_.empty()) {
        const float w = std::min(900.0f, text_w(toast_msg_, 18, true) + 44);
        const Rectangle r = R((kW - w) / 2, kH - 78, w, 42);
        panel(r, Color{38, 46, 70, 245}, col::accent, 10, 1.5f);
        text_c(fit(toast_msg_, 18, w - 28, true), r, 18, col::text, true);
    }
    SetMouseCursor(hot_ ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
    rlPopMatrix();
    if (!pending_shot_.empty()) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, pending_shot_.c_str());
        UnloadImage(img);
        pending_shot_.clear();
    }
    EndDrawing();
}

int App::run() {
    while (begin_frame()) {
        dispatch();
        end_frame();
        run_pending();
    }
    return 0;
}

void App::run_pending() {
    auto todo = std::move(pending_);
    pending_.clear();
    for (auto& fn : todo) fn();
}

// ---------------------------------------------------------------------- text
void App::text(const std::string& s, float x, float y, float size, Color c, bool bold) {
    if (s.empty()) return;
    const Font& f = font_for(size, bold);
    // snap the origin to a whole physical pixel so the glyphs are not blurred by sub-pixel placement
    const float px = std::round(x * view_scale_ + view_off_.x), py = std::round(y * view_scale_ + view_off_.y);
    DrawTextEx(f, s.c_str(), Vector2{(px - view_off_.x) / view_scale_, (py - view_off_.y) / view_scale_}, size, 0.0f, c);
}
float App::text_w(const std::string& s, float size, bool bold) {
    const Font& f = font_for(size, bold);
    return MeasureTextEx(f, s.c_str(), size, 0.0f).x;
}
void App::text_c(const std::string& s, Rectangle r, float size, Color c, bool bold) {
    text(s, r.x + (r.width - text_w(s, size, bold)) / 2, r.y + (r.height - size) / 2, size, c, bold);
}
void App::text_r(const std::string& s, float right_x, float y, float size, Color c, bool bold) {
    text(s, right_x - text_w(s, size, bold), y, size, c, bold);
}

std::string App::fit(const std::string& s, float size, float max_w, bool bold) {
    if (text_w(s, size, bold) <= max_w) return s;
    std::string t = s;
    while (!t.empty()) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
        const std::string cand = t + "...";
        if (text_w(cand, size, bold) <= max_w) return cand;
    }
    return "...";
}

std::vector<std::string> App::wrap(const std::string& s, float size, float max_w, bool bold) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = s.find('\n', start);
        const std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        std::size_t i = 0;
        while (i <= para.size()) {
            const std::size_t sp = para.find(' ', i);
            const std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_w(trial, size, bold) > max_w) { out.push_back(line); line = word; }
            else line = trial;
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

float App::wrapped_height(const std::string& s, float width, float size, bool bold, float gap) {
    return static_cast<float>(wrap(s, size, width, bold).size()) * (size * 1.2f + gap);
}

float App::text_wrapped(const std::string& s, Rectangle r, float size, Color c, bool bold, float gap) {
    float y = r.y;
    for (const auto& line : wrap(s, size, r.width, bold)) {
        text(line, r.x, y, size, c, bold);
        y += size * 1.2f + gap;
    }
    return y - r.y;
}

// ------------------------------------------------------------------- widgets
Color App::school_color(const std::string& id) const {
    if (id == "STO") return Color{96, 200, 208, 255};
    if (id == "EXI") return Color{240, 170, 80, 255};
    if (id == "ABS") return Color{200, 112, 222, 255};
    if (id == "NIH") return Color{150, 162, 190, 255};
    if (id == "UTI") return Color{112, 206, 122, 255};
    if (id == "HED") return Color{242, 122, 152, 255};
    return col::accent;
}
Color App::type_color(CardType t) const {
    return t == CardType::Monster ? Color{226, 160, 84, 255} : t == CardType::Spell ? Color{78, 196, 170, 255} : Color{230, 142, 212, 255};
}

void App::school_icon(const std::string& id, float cx, float cy, float r, Color c) {
    const Vector2 p{cx, cy};
    if (id == "STO") {
        DrawRectangleRounded(R(cx - r * 0.8f, cy - r * 0.8f, r * 1.6f, r * 1.6f), 0.25f, 4, c);
    } else if (id == "EXI") {
        DrawPoly(p, 4, r * 1.15f, 0.0f, c);
    } else if (id == "ABS") {
        DrawPoly(p, 4, r * 1.1f, 0.0f, c);
        DrawPoly(p, 4, r * 1.1f, 45.0f, c);
    } else if (id == "NIH") {
        DrawRing(p, r * 0.62f, r, 0.0f, 360.0f, 24, c);
    } else if (id == "UTI") {
        DrawPoly(Vector2{cx, cy + r * 0.12f}, 3, r * 1.1f, 270.0f, c);   // 270 degrees points the triangle upwards
    } else if (id == "HED") {
        DrawPoly(p, 6, r, 0.0f, c);
        DrawCircleV(p, r * 0.35f, mix(c, col::black, 0.5f));
    } else {
        DrawCircleV(p, r, c);
    }
}

bool App::hover(Rectangle r) const { return !blocked_ && CheckCollisionPointRec(in_.mouse, r); }
bool App::clicked(Rectangle r) const { return hover(r) && in_.pressed; }

void App::panel(Rectangle r, Color fill, Color border, float radius, float bw) {
    const float m = std::min(r.width, r.height);
    const float round = std::min(1.0f, 2.0f * radius / m);
    DrawRectangleRounded(r, round, 8, border);
    const Rectangle inner = R(r.x + bw, r.y + bw, r.width - 2 * bw, r.height - 2 * bw);
    DrawRectangleRounded(inner, std::min(1.0f, 2.0f * std::max(0.0f, radius - bw) / std::min(inner.width, inner.height)), 8, fill);
}

bool App::button(Rectangle r, const std::string& label, BtnStyle style, bool enabled, float size) {
    const bool hov = enabled && hover(r);
    if (hov) hot_ = true;
    Color fill, edge, txt = col::text;
    switch (style) {
        case BtnStyle::Primary: fill = hov ? Color{78, 130, 224, 255} : Color{54, 104, 200, 255}; edge = Color{112, 160, 245, 255}; txt = col::white; break;
        case BtnStyle::Good:    fill = hov ? Color{44, 134, 88, 255} : Color{32, 106, 68, 255};  edge = Color{110, 210, 150, 255}; txt = col::white; break;
        case BtnStyle::Danger:  fill = hov ? Color{186, 74, 80, 255} : Color{150, 56, 62, 255};   edge = Color{236, 120, 120, 255}; txt = col::white; break;
        case BtnStyle::Ghost:   fill = hov ? Color{255, 255, 255, 24} : Color{0, 0, 0, 0};          edge = Color{0, 0, 0, 0}; txt = hov ? col::text : col::muted; break;
        default:                fill = hov ? Color{58, 68, 98, 255} : Color{42, 50, 74, 255};        edge = Color{74, 86, 122, 255}; break;
    }
    if (!enabled) { fill = Color{28, 32, 46, 255}; edge = Color{44, 50, 70, 255}; txt = Color{98, 106, 128, 255}; }
    if (style == BtnStyle::Ghost && enabled) { if (fill.a) DrawRectangleRounded(r, 0.3f, 8, fill); }
    else panel(r, fill, edge, 9, 1.5f);
    text_c(fit(label, size, r.width - 16, true), r, size, txt, true);
    return hov && in_.pressed;
}

bool App::text_field(Rectangle r, TextField& f, const char* placeholder) {
    bool submitted = false;
    const bool hov = hover(r);
    if (hov) hot_ = true;
    if (in_.pressed) {
        if (hov) focus_ = &f;
        else if (focus_ == &f) focus_ = nullptr;
    }
    const bool focused = focus_ == &f;
    if (focused) {
        for (int ch : in_.chars) {
            const bool allowed = (ch >= 32 && ch <= 126) || (ch >= 160 && ch <= 255);
            if (allowed && f.text.size() < f.max_len) {
                int n = 0;
                const char* u = CodepointToUTF8(ch, &n);
                f.text.append(u, static_cast<std::size_t>(n));
            }
        }
        if (key(KEY_BACKSPACE) && !f.text.empty()) {
            while (!f.text.empty() && (static_cast<unsigned char>(f.text.back()) & 0xC0) == 0x80) f.text.pop_back();
            if (!f.text.empty()) f.text.pop_back();
        }
        if (key(KEY_ENTER)) submitted = true;
    }
    panel(r, Color{20, 24, 36, 255}, focused ? col::accent : col::border, 8, focused ? 2.0f : 1.5f);
    const float ty = r.y + (r.height - 20) / 2;
    if (f.text.empty() && !focused) text(placeholder, r.x + 12, ty, 20, Color{96, 106, 132, 255});
    else text(f.text, r.x + 12, ty, 20, col::text);
    if (focused && std::fmod(now_, 1.0) < 0.55) {
        const float cx = r.x + 12 + text_w(f.text, 20) + 2;
        DrawRectangle(static_cast<int>(cx), static_cast<int>(ty), 2, 22, col::text);
    }
    return submitted;
}

void App::bar(Rectangle r, float frac, Color fill) {
    frac = std::max(0.0f, std::min(1.0f, frac));
    DrawRectangleRounded(r, 1.0f, 6, col::barback);
    if (frac > 0.0f) DrawRectangleRounded(R(r.x, r.y, std::max(r.height, r.width * frac), r.height), 1.0f, 6, fill);
}

void App::wisdom_bar(Rectangle r, int pct, Color c) {
    bar(r, pct / 100.0f, c);
    text_r(std::to_string(pct) + "%", r.x + r.width + 52, r.y + (r.height - 18) / 2, 18, col::text, true);
}

void App::begin_clip(Rectangle r) {        // r is in layout units; the scissor box is in framebuffer pixels
    rlDrawRenderBatchActive();
    const int x = static_cast<int>(std::floor(view_off_.x + r.x * view_scale_));
    const int y = static_cast<int>(std::floor(view_off_.y + r.y * view_scale_));
    const int w = static_cast<int>(std::ceil(r.width * view_scale_)), h = static_cast<int>(std::ceil(r.height * view_scale_));
    rlEnableScissorTest();
    rlScissor(x, fb_h_ - (y + h), w, h);
}
void App::end_clip() {
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
}

float App::scroll_area(Rectangle r, float content_h, float& scroll) {
    const float max_scroll = std::max(0.0f, content_h - r.height);
    if (hover(r)) scroll -= in_.wheel * 48.0f;
    scroll = std::max(0.0f, std::min(max_scroll, scroll));
    if (max_scroll > 0.0f) {
        const float thumb = std::max(30.0f, r.height * r.height / content_h);
        const float ty = r.y + (r.height - thumb) * (scroll / max_scroll);
        DrawRectangleRounded(R(r.x + r.width - 6, r.y, 4, r.height), 1.0f, 4, alpha(col::border, 90));
        DrawRectangleRounded(R(r.x + r.width - 6, ty, 4, thumb), 1.0f, 4, col::muted);
    }
    return scroll;
}

void App::dim_background(int a) { DrawRectangle(0, 0, kW, kH, Color{0, 0, 0, static_cast<unsigned char>(a)}); }

void App::title_bar(const std::string& title, const std::string& subtitle) {
    text(title, 40, 22, 32, col::text, true);
    if (!subtitle.empty()) text(subtitle, 42, 62, 17, col::muted);
    if (logged_in_) {
        text_r(profile_.name + "  (" + profile_.student_id + ")", kW - 40, 24, 18, col::text, true);
        text_r(std::to_string(profile_.lifetime_points) + " lifetime points", kW - 40, 48, 15, col::muted);
    }
    DrawRectangle(40, 86, kW - 80, 1, alpha(col::border, 160));
}

// --------------------------------------------------------------------- cards
CardView App::view_of(const CardInstance& i, bool hidden) const {
    CardView v;
    v.valid = true; v.hidden = hidden && !i.revealed;
    v.name = i.name; v.school_id = i.school_id; v.type = i.card_type; v.card_id = i.card_id;
    if (const Card* c = engine_->get_card(i.card_id)) v.tag = c->tag;
    v.level = i.level; v.cost = i.cost; v.atk = i.atk; v.hp = i.hp; v.max_hp = i.max_hp;
    v.pending_penalty = i.pending_penalty; v.can_attack = i.can_attack_this_turn; v.revived = i.revived;
    v.frozen = i.frozen; v.silenced = i.silenced; v.revealed = i.revealed;
    return v;
}
CardView App::view_of(const GraveEntry& g, const std::string& note) const {
    CardView v;
    v.valid = true;
    v.name = g.name; v.school_id = g.school_id; v.type = g.card_type; v.card_id = g.card_id;
    v.level = g.level; v.revived = g.revived; v.note = note;
    if (const Card* c = engine_->get_card(g.card_id)) {
        const auto s = duel_cards::stats_at_level(*c, g.level);
        v.cost = s.cost; v.atk = s.atk; v.hp = s.hp; v.max_hp = s.hp; v.tag = c->tag;
    }
    return v;
}
CardView App::view_of(const Card& c, int level) const {
    CardView v;
    v.valid = true;
    v.name = c.collectible; v.school_id = c.school_id; v.type = c.card_type; v.card_id = c.id; v.tag = c.tag;
    v.level = level;
    const auto s = duel_cards::stats_at_level(c, level);
    v.cost = s.cost; v.atk = s.atk; v.hp = s.hp; v.max_hp = s.hp;
    return v;
}

void App::draw_card_face(Rectangle r, const CardView& v, const CardDraw& d) {
    if (d.face_down || v.hidden) {
        panel(r, Color{38, 44, 72, 255}, d.selected ? col::warn : d.hovered ? col::accent : col::border, 7, 2.0f);
        const float cx = r.x + r.width / 2, cy = r.y + r.height / 2, rr = std::min(r.width, r.height) * 0.3f;
        DrawPoly(Vector2{cx, cy}, 4, rr, 0.0f, alpha(col::accent, 110));
        DrawPoly(Vector2{cx, cy}, 4, rr * 0.62f, 0.0f, Color{38, 44, 72, 255});
        if (std::min(r.width, r.height) > 50) text_c("?", R(cx - 20, cy - 20, 40, 40), std::min(r.width, r.height) * 0.28f, alpha(col::muted, 230), true);
        return;
    }
    const Color sc = school_color(v.school_id), tc = type_color(v.type);
    const Color edge = d.selected ? col::warn : d.target ? col::bad : d.ready ? col::good : d.hovered ? col::accent : mix(sc, col::white, 0.15f);
    const float bw = (d.selected || d.target || d.ready || d.hovered) ? 3.0f : 1.5f;
    panel(r, d.dim ? Color{24, 28, 40, 255} : col::panel2, edge, 7, bw);
    const float pad = bw + 2;
    const float hh = std::max(24.0f, std::min(36.0f, r.height * 0.2f));
    const Rectangle head = R(r.x + pad, r.y + pad, r.width - 2 * pad, hh);
    const Color head_bg = mix(sc, col::black, d.dim ? 0.78f : 0.62f);
    DrawRectangleRounded(head, 0.3f, 6, head_bg);
    const float ns = r.width >= 110 ? 15.0f : 13.0f;
    float name_x = r.x + pad + 7;
    if (v.show_cost) {
        const float bx = r.x + pad + 14, by = head.y + hh / 2;
        const Color cc = d.dim ? Color{50, 74, 120, 255} : col::accent;
        DrawCircleV(Vector2{bx, by}, 12.0f, cc);
        text_c(std::to_string(v.cost), R(bx - 12, by - 12, 24, 24), 15, on_color(cc), true);
        name_x = r.x + pad + 31;
    }
    text(fit(v.name, ns, r.x + r.width - pad - 5 - name_x, true), name_x, head.y + (hh - ns) / 2, ns, d.dim ? col::muted : on_color(head_bg), true);

    const float ty = head.y + hh + 5;
    std::string tl = duel_cards::type_label(v.type);
    for (auto& ch : tl) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    text(tl, r.x + pad + 6, ty, 12, tc, true);
    if (v.on_field && v.type == CardType::Monster)
        text_r(v.can_attack ? "READY" : "WAIT", r.x + r.width - pad - 6, ty, 12, v.can_attack ? col::good : col::muted, true);
    else
        text_r("Lv " + std::to_string(v.level), r.x + r.width - pad - 6, ty, 12, col::muted, true);

    school_icon(v.school_id, r.x + r.width / 2, r.y + r.height * 0.5f, std::min(r.width, r.height) * 0.17f, alpha(sc, d.dim ? 26 : 46));

    const float by = r.y + r.height - pad - 22;
    if (v.type == CardType::Monster) {
        const bool boosted = v.pending_penalty > 0;
        text("ATK " + std::to_string(v.atk), r.x + pad + 7, by, 16, boosted ? col::warn : Color{248, 178, 108, 255}, true);
        std::string hp = "HP " + std::to_string(v.hp);
        if (v.on_field && v.hp < v.max_hp) hp += "/" + std::to_string(v.max_hp);
        text_r(hp, r.x + r.width - pad - 7, by, 16, v.on_field && v.hp < v.max_hp ? col::warn : col::good, true);
    }
    if (r.height >= 100) {
        text(fit(v.tag, 13, r.width - 2 * pad - 14, true), r.x + pad + 7, by - 21, 13, d.dim ? Color{128, 136, 158, 255} : Color{214, 222, 242, 255}, true);
        float bx = r.x + pad + 6;                                           // status badges
        auto badge = [&](const char* s, Color bg) {
            const float w = text_w(s, 11, true) + 12;
            DrawRectangleRounded(R(bx, ty + 19, w, 17), 0.4f, 6, bg);
            text_c(s, R(bx, ty + 19, w, 17), 11, on_color(bg), true);
            bx += w + 4;
        };
        if (v.frozen) badge("FROZEN", Color{96, 190, 232, 255});
        if (v.silenced) badge("SILENCED", Color{206, 108, 108, 255});
        if (v.revealed) badge("REVEALED", Color{210, 150, 235, 255});
    }
    if (v.grave_ready) {
        const Rectangle b = R(r.x + r.width - 54, r.y + r.height - 26, 48, 20);
        DrawRectangleRounded(b, 0.4f, 6, col::warn);
        text_c("READY", b, 12, on_color(col::warn), true);
    }
}

void App::draw_detail(Rectangle r, const CardView* v, const char* hint) {
    panel(r);
    const float x = r.x + 14, w = r.width - 28;
    float y = r.y + 12;
    if (!v || !v->valid) {
        text_wrapped(hint, R(x, y, w, r.height - 24), 16, col::muted);
        return;
    }
    if (v->hidden) {
        text("Face-down card", x, y, 22, col::text, true);
        text_wrapped("A Set Trap. Its effect stays hidden until the moment it activates. Effects such as Reveal or Destroy Traps can expose or remove it.", R(x, y + 34, w, 140), 16, col::muted);
        return;
    }
    const Color sc = school_color(v->school_id);
    school_icon(v->school_id, x + 11, y + 14, 10, sc);
    text(fit(v->name, 22, w - 34, true), x + 30, y + 2, 22, col::text, true);
    y += 36;
    const School* sch = engine_->get_school(v->school_id);
    text(std::string(duel_cards::type_label(v->type)) + "  -  " + (sch ? sch->name : v->school_id) + "  -  Level " + std::to_string(v->level),
         x, y, 15, type_color(v->type), true);
    y += 25;
    if (v->type == CardType::Monster) {
        text("ATK " + std::to_string(v->atk), x, y, 20, Color{248, 178, 108, 255}, true);
        std::string hp = "HP " + std::to_string(v->hp);
        if (v->on_field && v->hp < v->max_hp) hp += "/" + std::to_string(v->max_hp);
        text(hp, x + 96, y, 20, col::good, true);
        if (v->show_cost && !v->on_field) text_r("Cost " + std::to_string(v->cost), x + w, y + 2, 17, col::accent, true);
        y += 29;
    } else if (v->show_cost && !v->on_field) {
        text("Insight cost " + std::to_string(v->cost), x, y, 17, col::accent, true);
        y += 25;
    }
    auto note = [&](const std::string& s, Color c) { y += text_wrapped(s, R(x, y, w, 40), 14, c, false, 1) + 3; };
    if (v->on_field && v->type == CardType::Monster) {
        note(v->can_attack ? "Ready to attack this turn." : "Can't attack right now (just played, frozen or already attacked).", v->can_attack ? col::good : col::muted);
        if (v->pending_penalty > 0) note("Loses " + std::to_string(v->pending_penalty) + " ATK at the start of its next turn.", col::warn);
    }
    if (v->frozen) note("Frozen: it can't attack on its owner's next turn.", Color{110, 200, 240, 255});
    if (v->silenced) note("Silenced: its abilities are negated.", Color{236, 130, 130, 255});
    if (v->revived) note("Special Summoned back from a Graveyard - it can't be revived again.", col::warn);
    if (v->grave_ready) note("Its Graveyard effect can be used right now.", col::warn);
    y += 4;
    text("Effect", x, y, 16, col::accent, true);
    y += 23;
    const Card* card = engine_->get_card(v->card_id);
    const float bottom = r.y + r.height - (v->note.empty() ? 10 : 30);
    if (card) {
        const auto lines = duel_cards::describe(*card, v->level);
        float fs = 15.0f;
        auto total = [&](float size) { float h = 0; for (const auto& l : lines) h += wrapped_height(l, w, size, false, 2) + 6; return h; };
        while (fs > 12.0f && y + total(fs) > bottom) fs -= 1.0f;
        for (const auto& line : lines) y += text_wrapped(line, R(x, y, w, bottom - y), fs, col::text, false, 2) + 6;
    }
    if (!v->note.empty()) text(fit(v->note, 14, w), x, r.y + r.height - 26, 14, col::muted);
}

void App::draw_effect_glossary(Rectangle r, float& scroll) {
    const float w = r.width - 22;
    const auto& kw = duel_cards::keywords();
    float content = 8;
    for (const auto& k : kw) content += 28 + wrapped_height(k.text, w, 15, false, 2) + 12;
    scroll_area(r, content, scroll);
    begin_clip(r);
    float y = r.y + 4 - scroll;
    for (const auto& k : kw) {
        text(k.label, r.x + 4, y, 18, col::accent, true);
        y += 26;
        y += text_wrapped(k.text, R(r.x + 4, y, w, 300), 15, col::text, false, 2) + 12;
    }
    end_clip();
}

}  // namespace fd::ui
