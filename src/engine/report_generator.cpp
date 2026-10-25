#include "report_generator.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fd::report_generator {

static double round1(double v) { return std::nearbyint(v * 10.0) / 10.0; }

static std::string fmt1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

static std::string pad_left(const std::string& s, std::size_t w) { return s.size() >= w ? s : std::string(w - s.size(), ' ') + s; }
static std::string pad_right(const std::string& s, std::size_t w) { return s.size() >= w ? s : s + std::string(w - s.size(), ' '); }

// Python's str.center(width)
static std::string center(const std::string& s, std::size_t width) {
    if (s.size() >= width) return s;
    std::size_t pad = width - s.size();
    std::size_t left = pad / 2 + (pad & width & 1);
    return std::string(left, ' ') + s + std::string(pad - left, ' ');
}

static std::string csv_field(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

static std::vector<Row> aggregate(const Profile& profile, const LogData& log, const std::vector<School>& schools) {
    std::vector<Row> rows;
    for (const auto& s : schools) {
        Row r;
        r.school_id = s.id;
        r.school_name = s.name;
        for (const auto& resp : log.responses) {
            if (resp.school_id != s.id) continue;
            ++r.attempted;
            if (resp.is_correct) ++r.correct;
        }
        if (r.attempted) r.accuracy_pct = round1(100.0 * r.correct / r.attempted);
        auto it = profile.wisdom.find(s.id);
        r.wisdom_pct = it == profile.wisdom.end() ? 0 : it->second;
        rows.push_back(r);
    }
    return rows;
}

static std::vector<std::string> weakest_schools(const std::vector<Row>& rows, std::size_t max_items = 2) {
    std::vector<Row> attempted;
    for (const auto& r : rows) if (r.attempted > 0) attempted.push_back(r);
    std::stable_sort(attempted.begin(), attempted.end(), [](const Row& a, const Row& b) {
        if (*a.accuracy_pct != *b.accuracy_pct) return *a.accuracy_pct < *b.accuracy_pct;
        return a.attempted > b.attempted;
    });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < attempted.size() && i < max_items; ++i)
        if (*attempted[i].accuracy_pct < 80) out.push_back(attempted[i].school_name);
    return out;
}

Report generate_report(const Profile& profile, const LogData& log, const std::vector<School>& schools, const std::string& reports_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(reports_dir, ec);

    Report rep;
    rep.rows = aggregate(profile, log, schools);
    rep.weakest = weakest_schools(rep.rows);
    int total_rounds = 0, total_correct = 0;
    for (const auto& r : rep.rows) { total_rounds += r.attempted; total_correct += r.correct; }
    std::optional<double> overall;
    if (total_rounds) overall = round1(100.0 * total_correct / total_rounds);

    const std::string generated_at = now_formatted("%Y-%m-%d %H:%M");
    const std::string stamp = now_formatted("%Y%m%d_%H%M%S");
    std::string safe;
    for (unsigned char ch : profile.student_id)
        if (std::isalnum(ch) || ch == '-' || ch == '_') safe.push_back(static_cast<char>(ch));
    if (safe.empty()) safe = "student";
    const std::string sep = (!reports_dir.empty() && reports_dir.back() != '/' && reports_dir.back() != '\\') ? "/" : "";
    rep.txt_path = reports_dir + sep + "TutorReport_" + safe + "_" + stamp + ".txt";
    rep.csv_path = reports_dir + sep + "TutorReport_" + safe + "_" + stamp + ".csv";

    auto& lines = rep.text_lines;
    lines.push_back(std::string(60, '='));
    lines.push_back(center("FAUST'S DECK -- TUTOR PROGRESS REPORT", 60));
    lines.push_back(std::string(60, '='));
    lines.push_back("Student: " + profile.name + "   Class: " + profile.class_name + "   ID: " + profile.student_id);
    lines.push_back("Generated: " + generated_at);
    lines.push_back(std::string(60, '-'));
    lines.push_back(pad_right("School", 16) + pad_left("Attempted", 10) + pad_left("Correct", 9) + pad_left("Accuracy", 10) + pad_left("Wisdom %", 11));
    for (const auto& r : rep.rows) {
        std::string acc = r.accuracy_pct ? fmt1(*r.accuracy_pct) + "%" : "n/a";
        lines.push_back(pad_right(r.school_name, 16) + pad_left(std::to_string(r.attempted), 10) + pad_left(std::to_string(r.correct), 9) +
                        pad_left(acc, 10) + pad_left(std::to_string(r.wisdom_pct), 10) + "%");
    }
    lines.push_back(std::string(60, '-'));
    lines.push_back("Total rounds played: " + std::to_string(total_rounds) + "    Overall accuracy: " +
                    (overall ? fmt1(*overall) : std::string("n/a")) + "%    Lifetime points: " + std::to_string(profile.lifetime_points));
    int size = static_cast<int>(profile.collection.size());
    lines.push_back("Cards collected: " + std::to_string(size) + "/60" +
                    (size >= 40 ? std::string("  (Deck Builder & Duel unlocked)")
                                : "  (" + std::to_string(std::max(0, 40 - size)) + " more to unlock Duel Mode)"));
    std::string weak_text;
    for (std::size_t i = 0; i < rep.weakest.size(); ++i) weak_text += (i ? ", " : "") + rep.weakest[i];
    lines.push_back(rep.weakest.empty() ? "Suggested focus for next lesson: no clear weak area yet (or not enough data)."
                                        : "Suggested focus for next lesson: " + weak_text);
    lines.push_back(std::string(60, '='));

    {
        std::ofstream f(rep.txt_path, std::ios::binary | std::ios::trunc);
        for (const auto& l : lines) f << l << "\n";
    }
    {
        std::ofstream f(rep.csv_path, std::ios::binary | std::ios::trunc);
        auto row = [&](std::initializer_list<std::string> cells) {
            bool first = true;
            for (const auto& c : cells) { if (!first) f << ","; f << csv_field(c); first = false; }
            f << "\r\n";
        };
        row({"Student", profile.name});
        row({"Class", profile.class_name});
        row({"Student ID", profile.student_id});
        row({"Generated", generated_at});
        f << "\r\n";
        row({"School", "Attempted", "Correct", "Accuracy %", "Wisdom %"});
        for (const auto& r : rep.rows)
            row({r.school_name, std::to_string(r.attempted), std::to_string(r.correct),
                 r.accuracy_pct ? fmt1(*r.accuracy_pct) : std::string(), std::to_string(r.wisdom_pct)});
        f << "\r\n";
        row({"Total rounds played", std::to_string(total_rounds)});
        row({"Overall accuracy %", overall ? fmt1(*overall) : std::string()});
        row({"Lifetime points", std::to_string(profile.lifetime_points)});
        row({"Cards collected", std::to_string(size) + "/60"});
        row({"Suggested focus", rep.weakest.empty() ? std::string("None") : weak_text});
    }
    return rep;
}

}  // namespace fd::report_generator
