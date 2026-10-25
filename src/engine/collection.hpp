#pragma once
// Tracks which scenario-cards a student has obtained as playable Duel cards, and their
// level. A card is obtained at Level 1 the first time its scenario is answered
// correctly; a correct replay levels it up (cap 3).
#include "card_engine.hpp"

namespace fd::collection {

constexpr int MAX_LEVEL = 3;
constexpr int COLLECTION_UNLOCK_THRESHOLD = 40;

struct Result { std::string status; int level; };   // status: "new" / "levelup" / "maxed"

Result record_correct_answer(Profile& p, const std::string& card_id);
int level_of(const Profile& p, const std::string& card_id);   // 0 if not owned
int collection_size(const Profile& p);
bool duel_unlocked(const Profile& p);

struct Entry { const Card* card; int level; };
// Every school id is present in the result (possibly with an empty list); each list is sorted by card id.
std::map<std::string, std::vector<Entry>> collection_by_school(const Profile& p, const CardEngine& engine);

}  // namespace fd::collection
