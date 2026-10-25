#pragma once
// Ready-made 40-card decks a student can duel with directly. Each preset pairs two schools
// using all 20 of their cards at 2 copies each, fixed at PRESET_LEVEL.
#include "card_engine.hpp"

namespace fd::preset_decks {

constexpr int PRESET_LEVEL = 2;

struct Preset {
    std::string id, name;
    std::vector<std::string> schools;
    std::string description;
};

const std::vector<Preset>& presets();
const Preset* get_preset(const std::string& id);
std::vector<DeckEntry> build_preset_deck(const CardEngine& engine, const std::string& preset_id);

}  // namespace fd::preset_decks
