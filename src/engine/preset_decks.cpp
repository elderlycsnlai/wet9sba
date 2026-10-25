#include "preset_decks.hpp"

namespace fd::preset_decks {

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"stoic_bastion", "Stoic Bastion", {"STO", "UTI"},
         "A defensive, team-oriented deck. Guard cards build up Shield to soak up damage while Rally cards make the whole field hit harder."},
        {"wild_freedom", "Wild Freedom", {"EXI", "ABS"},
         "A fast, unpredictable deck. Freedom cards keep your hand full of options while Chaos cards strike at random, keeping the opponent guessing."},
        {"fleeting_pleasures", "Fleeting Pleasures", {"NIH", "HED"},
         "An aggressive, swingy deck. Void cards tear down the opponent's board and Indulge cards can attack the instant they're played."},
    };
    return list;
}

const Preset* get_preset(const std::string& id) {
    for (const auto& p : presets())
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<DeckEntry> build_preset_deck(const CardEngine& engine, const std::string& preset_id) {
    std::vector<DeckEntry> deck;
    const Preset* preset = get_preset(preset_id);
    if (!preset) return deck;
    for (const auto& c : engine.cards()) {
        if (std::find(preset->schools.begin(), preset->schools.end(), c.school_id) == preset->schools.end()) continue;
        deck.push_back({c.id, PRESET_LEVEL});
        deck.push_back({c.id, PRESET_LEVEL});
    }
    return deck;
}

}  // namespace fd::preset_decks
