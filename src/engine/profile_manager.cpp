#include "profile_manager.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fd {
namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static std::string safe_id(const std::string& id) {
    std::string out;
    for (unsigned char ch : id)
        if (std::isalnum(ch) || ch == '-' || ch == '_') out.push_back(static_cast<char>(ch));
    return out.empty() ? "student" : out;
}

static std::string join(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    char last = dir.back();
    return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
}

// Write to a temp file, then move into place, so a crash can never leave half a save.
static void atomic_write(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path);
    }
}

ProfileManager::ProfileManager(std::string saves_dir) : saves_dir_(std::move(saves_dir)) {
    std::error_code ec;
    fs::create_directories(saves_dir_, ec);
}

std::string ProfileManager::profile_path(const std::string& id) const { return join(saves_dir_, safe_id(id) + "_profile.json"); }
std::string ProfileManager::log_path(const std::string& id) const { return join(saves_dir_, safe_id(id) + "_log.json"); }

static Profile profile_from_json(const json& j, const std::string& fallback_id) {
    Profile p;
    p.student_id = j.value("student_id", fallback_id);
    p.name = j.value("name", p.student_id);
    p.class_name = j.value("class_name", std::string());
    p.date_joined = j.value("date_joined", std::string());
    p.lifetime_points = j.value("lifetime_points", 0);
    if (j.contains("wisdom") && j["wisdom"].is_object())
        for (const auto& [k, v] : j["wisdom"].items()) p.wisdom[k] = v.get<int>();
    if (j.contains("unlocked_schools") && j["unlocked_schools"].is_array())
        for (const auto& v : j["unlocked_schools"]) p.unlocked_schools.push_back(v.get<std::string>());
    if (j.contains("collection") && j["collection"].is_object())
        for (const auto& [k, v] : j["collection"].items()) p.collection[k] = v.get<int>();
    if (j.contains("decks") && j["decks"].is_object())
        for (const auto& [name, arr] : j["decks"].items()) {
            Deck d;
            d.name = name;
            for (const auto& c : arr) d.cards.push_back(c.get<std::string>());
            p.decks.push_back(std::move(d));
        }
    if (j.contains("active_deck") && j["active_deck"].is_string()) p.active_deck = j["active_deck"].get<std::string>();
    return p;
}

static json profile_to_json(const Profile& p) {
    json j = json::object();
    j["student_id"] = p.student_id;
    j["name"] = p.name;
    j["class_name"] = p.class_name;
    j["date_joined"] = p.date_joined;
    json w = json::object();
    for (const auto& [k, v] : p.wisdom) w[k] = v;
    j["wisdom"] = w;
    j["unlocked_schools"] = p.unlocked_schools;
    j["lifetime_points"] = p.lifetime_points;
    json c = json::object();
    for (const auto& [k, v] : p.collection) c[k] = v;
    j["collection"] = c;
    json d = json::object();
    for (const auto& deck : p.decks) d[deck.name] = deck.cards;
    j["decks"] = d;
    j["active_deck"] = p.active_deck.empty() ? json(nullptr) : json(p.active_deck);
    return j;
}

std::vector<std::pair<std::string, std::string>> ProfileManager::list_students() const {
    std::vector<std::pair<std::string, std::string>> results;
    std::error_code ec;
    if (!fs::is_directory(saves_dir_, ec)) return results;
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(saves_dir_, ec)) {
        std::string fname = entry.path().filename().string();
        const std::string suffix = "_profile.json";
        if (fname.size() > suffix.size() && fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0)
            files.push_back(fname);
    }
    std::sort(files.begin(), files.end());   // sorted(os.listdir(...))
    for (const auto& fname : files) {
        try {
            std::ifstream f(join(saves_dir_, fname), std::ios::binary);
            json data = json::parse(f);
            results.emplace_back(data.value("student_id", fname), data.value("name", std::string("Unknown")));
        } catch (const json::exception&) {
            continue;
        }
    }
    return results;
}

Profile ProfileManager::load_or_create(const std::string& student_id, const std::string& name, const std::string& class_name) {
    const std::string path = profile_path(student_id);
    if (fs::exists(path)) {
        try {
            std::ifstream f(path, std::ios::binary);
            json j = json::parse(f);
            Profile p = profile_from_json(j, student_id);
            return p;
        } catch (const json::exception& exc) {
            // The save file exists but is damaged (killed mid-write, or hand-edited wrongly).
            // Keep it for inspection instead of silently losing it, then start a fresh profile.
            const std::string backup = path + ".corrupt-" + now_formatted("%Y%m%d%H%M%S");
            std::error_code ec;
            fs::rename(path, backup, ec);
            last_warning = "Could not read the save file for '" + student_id + "'. The damaged file was kept as '" +
                           fs::path(backup).filename().string() + "' and a fresh profile was started.";
            std::fprintf(stderr, "[warning] %s (%s)\n", last_warning.c_str(), exc.what());
        }
    }
    Profile p;
    p.student_id = student_id;
    p.name = name.empty() ? student_id : name;
    p.class_name = class_name;
    p.date_joined = now_formatted("%Y-%m-%d");
    save_profile(p);
    return p;
}

void ProfileManager::save_profile(const Profile& p) const {
    atomic_write(profile_path(p.student_id), profile_to_json(p).dump(2) + "\n");
}

std::string ProfileManager::new_session_id() const {
    return "S-" + now_formatted("%Y%m%d%H%M%S") + "-" + std::to_string(rand_int(100, 999));
}

SessionRec ProfileManager::start_session(const std::string& student_id) const {
    SessionRec s;
    s.session_id = new_session_id();
    s.student_id = student_id;
    s.datetime = now_formatted("%Y-%m-%dT%H:%M:%S");
    return s;
}

static json session_to_json(const SessionRec& s) {
    json j = json::object();
    j["session_id"] = s.session_id;
    j["student_id"] = s.student_id;
    j["datetime"] = s.datetime;
    j["total_score"] = s.total_score;
    j["rounds_played"] = s.rounds_played;
    return j;
}

LogData ProfileManager::get_log(const std::string& student_id) const {
    LogData log;
    const std::string path = log_path(student_id);
    if (!fs::exists(path)) return log;
    try {
        std::ifstream f(path, std::ios::binary);
        json j = json::parse(f);
        for (const auto& s : j.value("sessions", json::array())) {
            SessionRec r;
            r.session_id = s.value("session_id", std::string());
            r.student_id = s.value("student_id", std::string());
            r.datetime = s.value("datetime", std::string());
            r.total_score = s.value("total_score", 0);
            r.rounds_played = s.value("rounds_played", 0);
            log.sessions.push_back(r);
        }
        for (const auto& s : j.value("responses", json::array())) {
            ResponseRec r;
            r.log_id = s.value("log_id", std::string());
            r.session_id = s.value("session_id", std::string());
            r.card_id = s.value("card_id", std::string());
            r.school_id = s.value("school_id", std::string());
            r.student_choice = s.value("student_choice", std::string());
            r.is_correct = s.value("is_correct", false);
            r.time_taken_sec = s.value("time_taken_sec", 0.0);
            r.timestamp = s.value("timestamp", std::string());
            log.responses.push_back(r);
        }
    } catch (const json::exception&) {
        return LogData{};
    }
    return log;
}

void ProfileManager::record_round(const std::string& student_id, SessionRec& session, const Card& card, char student_choice,
                                  const evaluator::Result& result, double time_taken_sec) const {
    session.total_score += result.points;
    session.rounds_played += 1;
    LogData log = get_log(student_id);
    bool found = false;
    for (auto& s : log.sessions)
        if (s.session_id == session.session_id) { s = session; found = true; }
    if (!found) log.sessions.push_back(session);

    char num[16];
    std::snprintf(num, sizeof num, "%03d", session.rounds_played);
    ResponseRec r;
    r.log_id = "L-" + session.session_id + "-" + num;
    r.session_id = session.session_id;
    r.card_id = card.id;
    r.school_id = card.school_id;
    r.student_choice = std::string(1, student_choice);
    r.is_correct = result.is_correct;
    r.time_taken_sec = time_taken_sec;
    r.timestamp = now_formatted("%Y-%m-%dT%H:%M:%S");
    log.responses.push_back(r);

    json j = json::object();
    json sessions = json::array(), responses = json::array();
    for (const auto& s : log.sessions) sessions.push_back(session_to_json(s));
    for (const auto& x : log.responses) {
        json o = json::object();
        o["log_id"] = x.log_id;
        o["session_id"] = x.session_id;
        o["card_id"] = x.card_id;
        o["school_id"] = x.school_id;
        o["student_choice"] = x.student_choice;
        o["is_correct"] = x.is_correct;
        o["time_taken_sec"] = x.time_taken_sec;
        o["timestamp"] = x.timestamp;
        responses.push_back(o);
    }
    j["sessions"] = sessions;
    j["responses"] = responses;
    atomic_write(log_path(student_id), j.dump(2) + "\n");
}

}  // namespace fd
