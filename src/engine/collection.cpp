#include "collection.hpp"

namespace fd::collection {

Result record_correct_answer(Profile& p, const std::string& card_id) {
    auto it = p.collection.find(card_id);
    if (it == p.collection.end()) {
        p.collection[card_id] = 1;
        return {"new", 1};
    }
    if (it->second < MAX_LEVEL) {
        ++it->second;
        return {"levelup", it->second};
    }
    return {"maxed", it->second};
}

int level_of(const Profile& p, const std::string& card_id) {
    auto it = p.collection.find(card_id);
    return it == p.collection.end() ? 0 : it->second;
}
int collection_size(const Profile& p) { return static_cast<int>(p.collection.size()); }
bool duel_unlocked(const Profile& p) { return collection_size(p) >= COLLECTION_UNLOCK_THRESHOLD; }

std::map<std::string, std::vector<Entry>> collection_by_school(const Profile& p, const CardEngine& engine) {
    std::map<std::string, std::vector<Entry>> grouped;
    for (const auto& s : engine.all_schools()) grouped[s.id];
    for (const auto& [card_id, level] : p.collection) {   // std::map => already sorted by card id
        const Card* card = engine.get_card(card_id);
        if (!card) continue;
        grouped[card->school_id].push_back({card, level});
    }
    return grouped;
}

}  // namespace fd::collection
