#pragma once
// Manages a student's own Duel decks. Every deck lives inside that student's profile.
// Each deck is built from obtained cards, up to 3 copies of any one card, and must
// reach exactly 40 cards to be battle-ready.
#include "card_engine.hpp"

namespace fd::deck_builder {

constexpr int DECK_SIZE = 40;
constexpr int MAX_COPIES = 3;

struct Result { bool ok; std::string reason; };
struct Validation { bool legal; std::vector<std::string> reasons; };

void ensure(Profile& p);   // makes active_deck point at a real deck when possible
std::vector<std::string> list_decks(const Profile& p);
const Deck* find_deck(const Profile& p, const std::string& name);

Result create_deck(Profile& p, const std::string& name);
Result rename_deck(Profile& p, const std::string& old_name, const std::string& new_name);
Result delete_deck(Profile& p, const std::string& name);
Result set_active_deck(Profile& p, const std::string& name);

std::vector<std::string> get_deck_cards(const Profile& p, const std::string& name);
std::vector<std::string> get_active_deck_cards(const Profile& p);
std::map<std::string, int> deck_counts(const Profile& p, const std::string& name);
int deck_size(const Profile& p, const std::string& name);

Result add_card(Profile& p, const std::string& name, const std::string& card_id);
Result remove_card(Profile& p, const std::string& name, const std::string& card_id);
void clear_deck(Profile& p, const std::string& name);

bool is_battle_ready(const Profile& p, const std::string& name);
// Validates the named deck (or the active deck when name is empty).
Validation validate_full(const Profile& p, const CardEngine& engine, const std::string& name = "");

}  // namespace fd::deck_builder
