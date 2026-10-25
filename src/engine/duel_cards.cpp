#include "duel_cards.hpp"

#include <algorithm>
namespace fd::duel_cards {

static const char* kOpNames[kOpCount] = {
    "dmg_face", "dmg_self", "dmg_random", "dmg_all", "dmg_per_monster", "dmg_per_banished",
    "destroy_strongest", "banish_strongest", "bounce_strongest", "freeze_strongest", "silence_strongest",
    "destroy_traps", "reveal_traps", "block_traps", "banish_gy", "discard_enemy",
    "shield", "heal", "shield_per_monster", "draw", "draw_if_behind", "insight", "pay_lp",
    "search_monster", "search_spell", "search_trap", "mill_self", "revive_strongest", "revive_cost_le", "steal_gy",
    "buff_allies_atk", "buff_allies_hp", "buff_self_atk", "indulge", "sac_for_team", "share_draw",
    "coin", "one_of", "return_self",
    "negate_attack", "shield_from_attacker", "leech_attacker", "t_bounce", "t_destroy", "t_weaken", "t_negate_effect",
    "t_dmg_summoner", "negate_spell"};

const char* op_name(OpKind k) { return kOpNames[static_cast<int>(k)]; }
bool parse_op(const std::string& s, OpKind& out) {
    for (int i = 0; i < kOpCount; ++i) if (s == kOpNames[i]) { out = static_cast<OpKind>(i); return true; }
    return false;
}
bool parse_timing(const std::string& s, Timing& out) {
    static const std::pair<const char*, Timing> table[] = {
        {"SUMMON", Timing::Summon}, {"SPELL", Timing::Spell}, {"TRAP", Timing::Trap}, {"DEATH", Timing::Death}, {"MILL", Timing::Mill},
        {"ATTACK", Timing::Attack}, {"TURN_START", Timing::TurnStart}, {"TURN_END", Timing::TurnEnd}, {"GRAVE", Timing::Grave}};
    for (const auto& p : table) if (s == p.first) { out = p.second; return true; }
    return false;
}
const char* timing_label(Timing t) {
    switch (t) {
        case Timing::Summon: return "On Summon";
        case Timing::Spell: return "When played";
        case Timing::Trap: return "Trap";
        case Timing::Death: return "When destroyed";
        case Timing::Mill: return "When milled";
        case Timing::Attack: return "When attacking";
        case Timing::TurnStart: return "Start of turn";
        case Timing::TurnEnd: return "End of turn";
        case Timing::Grave: return "From the Graveyard";
    }
    return "";
}

static bool uses_n(OpKind k) {
    switch (k) {
        case OpKind::DmgFace: case OpKind::DmgSelf: case OpKind::DmgRandom: case OpKind::DmgAll: case OpKind::DmgPerMonster:
        case OpKind::DestroyTraps: case OpKind::BanishGy: case OpKind::DiscardEnemy: case OpKind::Shield: case OpKind::Heal:
        case OpKind::ShieldPerMonster: case OpKind::Draw: case OpKind::DrawIfBehind: case OpKind::Insight: case OpKind::PayLp:
        case OpKind::SearchMonster: case OpKind::SearchSpell: case OpKind::SearchTrap: case OpKind::MillSelf:
        case OpKind::ReviveCostLe: case OpKind::BuffAlliesAtk: case OpKind::BuffAlliesHp: case OpKind::BuffSelfAtk:
        case OpKind::Indulge: case OpKind::SacForTeam: case OpKind::ShareDraw: case OpKind::TWeaken: case OpKind::TDmgSummoner:
            return true;
        default: return false;
    }
}
bool op_active(const AbilityOp& op, int level) { return !uses_n(op.kind) || op.n.at(level) > 0; }

// ------------------------------------------------------------------ card text
static std::string N_(int v) { return std::to_string(v); }
static std::string count_of(int n, const char* one, const char* many) { return N_(n) + " " + (n == 1 ? one : many); }
static std::string fb_text(const AbilityOp& op, int lv) {
    const int f = op.fb.at(lv);
    return f > 0 ? " If there is none, deal " + N_(f) + " damage to the enemy's face instead." : "";
}

static std::string ops_text(const std::vector<AbilityOp>& ops, int lv, Trigger trig, Timing when);

static std::string op_text(const AbilityOp& op, int lv, Trigger trig, Timing when) {
    const int n = op.n.at(lv), m = op.m.at(lv);
    const std::string N = N_(n), M = N_(m);
    const bool spell = when == Timing::Spell;
    switch (op.kind) {
        case OpKind::DmgFace: return "Deal " + N + " damage to the enemy's face.";
        case OpKind::DmgSelf: return "You take " + N + " damage.";
        case OpKind::DmgRandom: return "Deal " + N + " damage to a random enemy target (their face or one of their monsters).";
        case OpKind::DmgAll: return "Deal " + N + " damage to every enemy monster.";
        case OpKind::DmgPerMonster: return "Deal " + N + " damage to the enemy's face for each monster you control.";
        case OpKind::DmgPerBanished: return "Deal " + N_(op.k) + " damage to the enemy's face for each card banished this way.";
        case OpKind::DestroyStrongest: return "Destroy the enemy monster with the highest ATK." + fb_text(op, lv);
        case OpKind::BanishStrongest: return "Banish the enemy monster with the highest ATK (removed from the game for good)." + fb_text(op, lv);
        case OpKind::BounceStrongest: return "Return the enemy monster with the highest ATK to their hand." + fb_text(op, lv);
        case OpKind::FreezeStrongest: return "Freeze the enemy monster with the highest ATK: it can't attack during the enemy's next turn.";
        case OpKind::SilenceStrongest: return "Silence the enemy monster with the highest ATK: it loses its abilities for the rest of the Duel.";
        case OpKind::DestroyTraps: return "Destroy " + N + " of the enemy's Set Traps (chosen at random).";
        case OpKind::RevealTraps: return "Flip the enemy's Set Traps face-up.";
        case OpKind::BlockTraps: return "The enemy's Set Traps can't be activated for the rest of this turn.";
        case OpKind::BanishGy: return "Banish up to " + count_of(n, "card", "cards") + " from the enemy's Graveyard (strongest Monsters first).";
        case OpKind::DiscardEnemy: return "The enemy discards " + count_of(n, "random card", "random cards") + " from their hand.";
        case OpKind::Shield: return "Gain " + N + " Shield.";
        case OpKind::Heal: return "Recover " + N + " LP (max 20).";
        case OpKind::ShieldPerMonster: return "Gain " + N + " Shield for each monster you control.";
        case OpKind::Draw: return "Draw " + count_of(n, "card", "cards") + ".";
        case OpKind::DrawIfBehind: return "If the enemy controls more monsters than you, draw " + count_of(n, "card", "cards") + ".";
        case OpKind::Insight: return "Gain " + N + " Insight this turn.";
        case OpKind::PayLp: return "Pay " + N + " LP (this can't reduce you below 1).";
        case OpKind::SearchMonster:
            return "Add " + count_of(n, "random Monster", "random Monsters") + (m > 0 ? " with cost " + M + " or less" : std::string()) + " from your deck to your hand.";
        case OpKind::SearchSpell: return "Add " + count_of(n, "random Spell", "random Spells") + " from your deck to your hand.";
        case OpKind::SearchTrap: return "Add " + count_of(n, "random Trap", "random Traps") + " from your deck to your hand.";
        case OpKind::MillSelf: return "Send the top " + count_of(n, "card", "cards") + " of your deck to your Graveyard.";
        case OpKind::ReviveStrongest: return "Special Summon the strongest Monster in your Graveyard (it can't be revived again)." + fb_text(op, lv);
        case OpKind::ReviveCostLe:
            return "Special Summon up to " + count_of(n, "Monster", "Monsters") + " with cost " + M + " or less from your Graveyard (they can't be revived again).";
        case OpKind::StealGy: return "Special Summon the strongest Monster in the enemy's Graveyard to your field." + fb_text(op, lv);
        case OpKind::BuffAlliesAtk: return std::string(spell ? "All monsters you control" : "Your other monsters") + " gain +" + N + " ATK permanently.";
        case OpKind::BuffAlliesHp: return std::string(spell ? "All monsters you control" : "Your other monsters") + " gain +" + N + " HP permanently.";
        case OpKind::BuffSelfAtk: return "This card gains +" + N + " ATK permanently.";
        case OpKind::Indulge: return "This card gains +" + N + " ATK until the start of your next turn.";
        case OpKind::SacForTeam: return "Destroy your weakest other monster. If you do, your other monsters gain +" + N + " ATK and you recover " + M + " LP.";
        case OpKind::ShareDraw: return "You draw " + count_of(n, "card", "cards") + "; the enemy draws " + count_of(m, "card", "cards") + ".";
        case OpKind::Coin: {
            const std::string h = ops_text(op.a, lv, trig, when), t = ops_text(op.b, lv, trig, when);
            return "Flip a coin. Heads: " + (h.empty() ? std::string("nothing happens.") : h) + " Tails: " + (t.empty() ? std::string("nothing happens.") : t);
        }
        case OpKind::OneOf: {
            std::string s = "One of these happens at random:";
            for (std::size_t i = 0; i < op.a.size(); ++i) s += " (" + N_(static_cast<int>(i) + 1) + ") " + op_text(op.a[i], lv, trig, when);
            return s;
        }
        case OpKind::ReturnSelf: return "Return this card from the Graveyard to your hand.";
        case OpKind::NegateAttack: return "Negate the attack.";
        case OpKind::ShieldFromAttacker: return "Gain Shield equal to the attacking monster's ATK.";
        case OpKind::LeechAttacker: return "Recover LP equal to the attacking monster's ATK.";
        case OpKind::TBounce: return trig == Trigger::OnAttack ? "Return the attacking monster to its owner's hand." : "Return the summoned monster to its owner's hand.";
        case OpKind::TDestroy: return trig == Trigger::OnAttack ? "Destroy the attacking monster." : "Destroy the summoned monster.";
        case OpKind::TWeaken: return "The summoned monster permanently loses " + N + " ATK.";
        case OpKind::TNegateEffect: return "Negate the summoned monster's On Summon effect.";
        case OpKind::TDmgSummoner: return "The enemy takes " + N + " damage.";
        case OpKind::NegateSpell: return std::string("Negate the Spell") + (op.k == 1 ? " and banish it." : ".");
    }
    return "";
}

static std::string ops_text(const std::vector<AbilityOp>& ops, int lv, Trigger trig, Timing when) {
    std::string s;
    for (const auto& op : ops) {
        if (!op_active(op, lv)) continue;
        if (!s.empty()) s += ' ';
        s += op_text(op, lv, trig, when);
    }
    return s;
}

static std::string header(const Card& c, const Ability& ab) {
    switch (ab.on) {
        case Timing::Summon: return "On Summon: ";
        case Timing::Spell: return "When played: ";
        case Timing::Death: return "When this card is destroyed: ";
        case Timing::Mill: return "When this card is sent from your deck or hand to the Graveyard: ";
        case Timing::Attack: return "When this monster attacks: ";
        case Timing::TurnStart: return "At the start of your turn: ";
        case Timing::TurnEnd: return "At the end of your turn: ";
        case Timing::Grave: return "From your Graveyard (" + N_(ab.grave_cost) + " Insight, banish this card): ";
        case Timing::Trap:
            return c.trigger == Trigger::OnAttack ? "Trap - when the enemy declares an attack: "
                 : c.trigger == Trigger::OnSummon ? "Trap - when the enemy summons a Monster: "
                                                  : "Trap - when the enemy plays a Spell: ";
    }
    return "";
}

std::string describe_ability(const Card& c, const Ability& ab, int level) {
    const std::string body = ops_text(ab.ops, level, c.trigger, ab.on);
    return body.empty() ? std::string() : header(c, ab) + body;
}

std::vector<std::string> describe(const Card& c, int level) {
    std::vector<std::string> lines;
    if (c.rush) lines.push_back("Rush: this monster can attack the turn it is summoned.");
    for (const auto& ab : c.abilities) {
        std::string s = describe_ability(c, ab, level);
        if (!s.empty()) lines.push_back(std::move(s));
    }
    if (lines.empty()) lines.push_back("No special effect.");
    return lines;
}

const std::string& short_label(const Card& c) { return c.tag; }

// ----------------------------------------------------------------- card data
std::vector<const Ability*> abilities_of(const Card& c, Timing t) {
    std::vector<const Ability*> out;
    for (const auto& a : c.abilities) if (a.on == t) out.push_back(&a);
    return out;
}
const Ability* grave_ability(const Card& c) {
    for (const auto& a : c.abilities) if (a.on == Timing::Grave) return &a;
    return nullptr;
}
bool has_grave_ability(const Card& c) { return grave_ability(c) != nullptr; }

Stats stats_at_level(const Card& c, int level) {
    level = std::max(1, std::min(3, level));
    const bool monster = c.card_type == CardType::Monster;
    return {c.cost, monster ? c.atk + (level >= 2 ? 1 : 0) : 0, monster ? c.hp + (level >= 3 ? 1 : 0) : 0};
}

CardInstance make_instance(const Card& c, int level, const std::string& instance_id) {
    const Stats s = stats_at_level(c, level);
    CardInstance i;
    i.instance_id = instance_id;
    i.card_id = c.id;
    i.name = c.collectible;
    i.school_id = c.school_id;
    i.card_type = c.card_type;
    i.trigger = c.trigger;
    i.level = level;
    i.cost = s.cost;
    i.base_atk = s.atk;
    i.atk = s.atk;
    i.max_hp = s.hp;
    i.hp = s.hp;
    return i;
}

GraveEntry graveyard_entry(const CardInstance& i) {
    GraveEntry g;
    g.card_id = i.card_id; g.name = i.name; g.school_id = i.school_id; g.card_type = i.card_type;
    g.level = i.level; g.atk = i.atk; g.hp = i.max_hp; g.revived = i.revived;
    return g;
}

GraveEntry graveyard_entry_from_card(const Card& c, int level) {
    GraveEntry g;
    g.card_id = c.id; g.name = c.collectible; g.school_id = c.school_id; g.card_type = c.card_type; g.level = level;
    return g;
}

const char* type_label(CardType t) { return t == CardType::Monster ? "Monster" : t == CardType::Spell ? "Spell" : "Trap"; }
const char* type_tag(CardType t) { return t == CardType::Monster ? "Mon" : t == CardType::Spell ? "Spl" : "Trp"; }

// --------------------------------------------------------------- glossary
const std::vector<Keyword>& keywords() {
    static const std::vector<Keyword> k = {
        {"Insight", "The resource you spend to play cards. Your maximum Insight grows by 1 every turn (up to 10) and refills at the start of your turn. Some effects give extra Insight for the current turn."},
        {"Life Points (LP)", "Each side starts with 20 LP. Reach 0 and you lose. Healing can never take you above 20."},
        {"Shield", "Absorbs damage to your face before your Life Points are reduced. It stays until it has been used up."},
        {"On Summon", "Happens the moment a Monster is played from your hand. An enemy Trap may react first, and can negate it."},
        {"Rush", "A Rush monster can attack the turn it is summoned. Other monsters must wait one turn."},
        {"Special Summon", "Putting a Monster onto the field with an effect (for example from a Graveyard). It skips its On Summon effect, Traps don't react to it, and it can't attack that turn. A revived monster can never be revived again."},
        {"Destroy / Banish", "Destroyed cards go to the Graveyard and trigger 'when destroyed' effects. Banished cards leave the game: they can't be revived and don't trigger anything."},
        {"When destroyed", "Happens when this Monster is destroyed on the field, by battle or by an effect. It does not happen if the monster is banished or returned to a hand."},
        {"When milled", "Happens when this card goes from your deck or hand to the Graveyard through a mill or discard effect."},
        {"Graveyard effect", "A card in your Graveyard may carry an effect you can use on your own turn. Pay its Insight cost and the card is banished. Your Graveyard pile glows gold when one is ready to use."},
        {"Set Trap", "Traps are Set face-down (up to 3). They react to an enemy attack, summon or Spell, then go to the Graveyard. You choose whether to activate when the moment comes."},
        {"Negate", "Stops an action or effect from happening. A negated attack deals no damage, a negated Spell does nothing, and a negated On Summon effect never resolves."},
        {"Freeze", "A frozen monster can't attack during its owner's next turn."},
        {"Silence", "A silenced monster loses all of its abilities for the rest of the Duel (it keeps its ATK and HP)."},
        {"Reveal", "Flips the enemy's Set Traps face-up so that you can read them before you attack."},
        {"Mill / Discard", "Mill sends cards from the top of a deck to its Graveyard. Discard sends cards from a hand to the Graveyard."},
        {"Until your next turn", "Temporary ATK (such as Indulge) disappears at the start of your next turn."},
        {"Coin flip", "Heads or tails, fifty-fifty. The result is shown on screen and recorded in the History log."},
        {"Random effects", "Effects that pick a random target or outcome choose with equal chance among the legal options."},
    };
    return k;
}

}  // namespace fd::duel_cards
