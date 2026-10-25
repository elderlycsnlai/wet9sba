#pragma once
// Module M4: Save/Load Profile Manager (supports DFD Process 3.0).
// Save files are the same JSON the original Python version wrote, so existing
// profiles (including maxtest_profile.json) load unchanged.
#include "card_engine.hpp"
#include "evaluator.hpp"

namespace fd {

class ProfileManager {
public:
    explicit ProfileManager(std::string saves_dir);

    const std::string& saves_dir() const { return saves_dir_; }
    std::vector<std::pair<std::string, std::string>> list_students() const;   // (student_id, name)
    Profile load_or_create(const std::string& student_id, const std::string& name = "", const std::string& class_name = "");
    void save_profile(const Profile& p) const;

    std::string new_session_id() const;
    SessionRec start_session(const std::string& student_id) const;
    void record_round(const std::string& student_id, SessionRec& session, const Card& card, char student_choice,
                      const evaluator::Result& result, double time_taken_sec) const;
    LogData get_log(const std::string& student_id) const;

    // Set when a damaged save file had to be set aside; the UI shows it once and clears it.
    std::string last_warning;

private:
    std::string profile_path(const std::string& student_id) const;
    std::string log_path(const std::string& student_id) const;
    std::string saves_dir_;
};

}  // namespace fd
