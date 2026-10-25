#include "deck_builder.hpp"

namespace fd::deck_builder {

static Deck* find_mut(Profile& p, const std::string& name) {
    for (auto& d : p.decks)
        if (d.name == name) return &d;
    return nullptr;
}
const Deck* find_deck(const Profile& p, const std::string& name) {
    for (const auto& d : p.decks)
        if (d.name == name) return &d;
    return nullptr;
}

void ensure(Profile& p) {
    if (!find_deck(p, p.active_deck) && !p.decks.empty()) p.active_deck = p.decks.front().name;
    if (p.decks.empty()) p.active_deck.clear();
}

std::vector<std::string> list_decks(const Profile& p) {
    std::vector<std::string> names;
    for (const auto& d : p.decks) names.push_back(d.name);
    return names;
}

Result create_deck(Profile& p, const std::string& name_in) {
    ensure(p);
    std::string name = trim(name_in);
    if (name.empty()) return {false, "Deck name can't be empty."};
    if (find_deck(p, name)) return {false, "You already have a deck with that name."};
    p.decks.push_back({name, {}});
    if (p.active_deck.empty()) p.active_deck = name;
    return {true, ""};
}

Result rename_deck(Profile& p, const std::string& old_name, const std::string& new_name_in) {
    ensure(p);
    std::string new_name = trim(new_name_in);
    if (!find_deck(p, old_name)) return {false, "No such deck."};
    if (new_name.empty()) return {false, "Deck name can't be empty."};
    if (find_deck(p, new_name)) return {false, "You already have a deck with that name."};
    // Like Python's dict pop + re-insert, the renamed deck moves to the end of the list.
    Deck moved = *find_deck(p, old_name);
    p.decks.erase(std::remove_if(p.decks.begin(), p.decks.end(), [&](const Deck& d) { return d.name == old_name; }), p.decks.end());
    moved.name = new_name;
    p.decks.push_back(moved);
    if (p.active_deck == old_name) p.active_deck = new_name;
    return {true, ""};
}

Result delete_deck(Profile& p, const std::string& name) {
    ensure(p);
    if (!find_deck(p, name)) return {false, "No such deck."};
    p.decks.erase(std::remove_if(p.decks.begin(), p.decks.end(), [&](const Deck& d) { return d.name == name; }), p.decks.end());
    if (p.active_deck == name) p.active_deck = p.decks.empty() ? std::string() : p.decks.front().name;
    return {true, ""};
}

Result set_active_deck(Profile& p, const std::string& name) {
    ensure(p);
    if (!find_deck(p, name)) return {false, "No such deck."};
    p.active_deck = name;
    return {true, ""};
}

std::vector<std::string> get_deck_cards(const Profile& p, const std::string& name) {
    const Deck* d = find_deck(p, name);
    return d ? d->cards : std::vector<std::string>{};
}
std::vector<std::string> get_active_deck_cards(const Profile& p) {
    if (p.active_deck.empty()) return {};
    return get_deck_cards(p, p.active_deck);
}
std::map<std::string, int> deck_counts(const Profile& p, const std::string& name) {
    std::map<std::string, int> counts;
    for (const auto& id : get_deck_cards(p, name)) ++counts[id];
    return counts;
}
int deck_size(const Profile& p, const std::string& name) {
    const Deck* d = find_deck(p, name);
    return d ? static_cast<int>(d->cards.size()) : 0;
}

Result add_card(Profile& p, const std::string& name, const std::string& card_id) {
    ensure(p);
    Deck* d = find_mut(p, name);
    if (!d) return {false, "No such deck."};
    if (!p.collection.count(card_id)) return {false, "You haven't obtained that card yet."};
    if (static_cast<int>(d->cards.size()) >= DECK_SIZE)
        return {false, "This deck is already full (" + std::to_string(DECK_SIZE) + " cards)."};
    int copies = static_cast<int>(std::count(d->cards.begin(), d->cards.end(), card_id));
    if (copies >= MAX_COPIES)
        return {false, "You can only include up to " + std::to_string(MAX_COPIES) + " copies of one card."};
    d->cards.push_back(card_id);
    return {true, ""};
}

Result remove_card(Profile& p, const std::string& name, const std::string& card_id) {
    ensure(p);
    Deck* d = find_mut(p, name);
    if (!d) return {false, "That card isn't currently in this deck."};
    auto it = std::find(d->cards.begin(), d->cards.end(), card_id);
    if (it == d->cards.end()) return {false, "That card isn't currently in this deck."};
    d->cards.erase(it);   // removes the first copy, like list.remove()
    return {true, ""};
}

void clear_deck(Profile& p, const std::string& name) {
    ensure(p);
    if (Deck* d = find_mut(p, name)) d->cards.clear();
}

bool is_battle_ready(const Profile& p, const std::string& name) { return deck_size(p, name) == DECK_SIZE; }

Validation validate_full(const Profile& p, const CardEngine& engine, const std::string& name_in) {
    std::string name = name_in.empty() ? p.active_deck : name_in;
    if (name.empty() || !find_deck(p, name)) return {false, {"No deck selected."}};
    std::vector<std::string> reasons;
    auto cards = get_deck_cards(p, name);
    if (static_cast<int>(cards.size()) != DECK_SIZE)
        reasons.push_back("Deck must be exactly " + std::to_string(DECK_SIZE) + " cards (currently " + std::to_string(cards.size()) + ").");
    std::map<std::string, int> counts;
    for (const auto& id : cards) ++counts[id];
    for (const auto& [id, count] : counts) {
        if (count > MAX_COPIES)
            reasons.push_back(id + " appears " + std::to_string(count) + " times (max " + std::to_string(MAX_COPIES) + ").");
        if (!p.collection.count(id)) reasons.push_back(id + " is in the deck but not in your collection.");
        if (!engine.get_card(id)) reasons.push_back(id + " is not a recognised card.");
    }
    return {reasons.empty(), reasons};
}

}  // namespace fd::deck_builder
