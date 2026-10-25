#pragma once
// Module M3: Progress Tracking / Wisdom Meter (DFD Process 3.0).
#include "types.hpp"

namespace fd::progress {

constexpr int MASTERY_CAP = 100;

inline bool is_unlocked(const Profile& p, const std::string& school_id) {
    return std::find(p.unlocked_schools.begin(), p.unlocked_schools.end(), school_id) != p.unlocked_schools.end();
}
// Returns true the first time a school is unlocked.
inline bool unlock_school(Profile& p, const std::string& school_id) {
    p.wisdom.emplace(school_id, 0);
    if (!is_unlocked(p, school_id)) {
        p.unlocked_schools.push_back(school_id);
        return true;
    }
    return false;
}
inline void award_points(Profile& p, const std::string& school_id, int points) {
    int& cur = p.wisdom[school_id];
    cur = std::min(MASTERY_CAP, cur + points);
    p.lifetime_points += points;   // lifetime points are uncapped
}
inline int get_percentage(const Profile& p, const std::string& school_id) {
    auto it = p.wisdom.find(school_id);
    return it == p.wisdom.end() ? 0 : it->second;
}

}  // namespace fd::progress
