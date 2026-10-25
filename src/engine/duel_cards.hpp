#pragma once
// Turns a scenario card plus a collection level into a battle-ready card instance
// (Monster / Spell / Trap) and writes the exact, number-filled card text shown to the player.
#include "types.hpp"

namespace fd::duel_cards {

struct Keyword { const char* label; const char* text; };
const std::vector<Keyword>& keywords();            // the in-game "Effects" glossary

const char* op_name(OpKind k);
bool parse_op(const std::string& s, OpKind& out);
bool parse_timing(const std::string& s, Timing& out);
const char* timing_label(Timing t);                // "On Summon", "When destroyed", ...
bool op_active(const AbilityOp& op, int level);    // false when the level-scaled amount is 0 (the op is skipped)

struct Stats { int cost, atk, hp; };
Stats stats_at_level(const Card& c, int level);    // cost is fixed per card; level 2 gives +1 ATK, level 3 +1 HP

CardInstance make_instance(const Card& c, int level, const std::string& instance_id);
GraveEntry graveyard_entry(const CardInstance& inst);
GraveEntry graveyard_entry_from_card(const Card& c, int level);   // used when a card is milled from the deck

std::vector<const Ability*> abilities_of(const Card& c, Timing t);
const Ability* grave_ability(const Card& c);        // nullptr when the card has no Graveyard effect
bool has_grave_ability(const Card& c);

std::string describe_ability(const Card& c, const Ability& ab, int level);   // one line, exact numbers for `level`
std::vector<std::string> describe(const Card& c, int level);                 // every line of card text
const std::string& short_label(const Card& c);

const char* type_label(CardType t);   // "Monster" / "Spell" / "Trap"
const char* type_tag(CardType t);     // "Mon" / "Spl" / "Trp"

}  // namespace fd::duel_cards
