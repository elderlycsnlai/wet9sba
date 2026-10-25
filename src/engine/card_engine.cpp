#include "card_engine.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "duel_cards.hpp"

namespace fd {
using json = nlohmann::ordered_json;

static json load_json(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw CardEngineError("Required data file not found: " + path);
    try {
        return json::parse(f);
    } catch (const json::parse_error& e) {
        throw CardEngineError("Data file is not valid JSON (" + path + "): " + e.what());
    }
}

static Lv3 parse_lv3(const json& o, const char* key, const std::string& card_id) {
    Lv3 out;
    if (!o.contains(key)) return out;
    const json& j = o.at(key);
    if (j.is_number_integer()) { for (int i = 0; i < 3; ++i) out.v[i] = j.get<int>(); return out; }
    if (j.is_array() && j.size() == 3) { for (int i = 0; i < 3; ++i) out.v[i] = j.at(i).get<int>(); return out; }
    throw CardEngineError(std::string("Bad '") + key + "' value on card " + card_id + " (expected a number or [lv1, lv2, lv3]).");
}
static std::vector<AbilityOp> parse_ops(const json& arr, const std::string& card_id) {
    std::vector<AbilityOp> out;
    for (const auto& o : arr) {
        AbilityOp op;
        const std::string name = o.at("op").get<std::string>();
        if (!duel_cards::parse_op(name, op.kind)) throw CardEngineError("Unknown effect '" + name + "' on card " + card_id);
        op.n = parse_lv3(o, "n", card_id);
        op.m = parse_lv3(o, "m", card_id);
        op.fb = parse_lv3(o, "fb", card_id);
        op.k = o.value("k", 0);
        if (o.contains("heads")) op.a = parse_ops(o.at("heads"), card_id);
        if (o.contains("tails")) op.b = parse_ops(o.at("tails"), card_id);
        if (o.contains("options")) op.a = parse_ops(o.at("options"), card_id);
        out.push_back(std::move(op));
    }
    return out;
}

CardEngine::CardEngine(const std::string& data_dir) {
    const std::string sep = (!data_dir.empty() && data_dir.back() != '/' && data_dir.back() != '\\') ? "/" : "";
    json js = load_json(data_dir + sep + "schools.json");
    json jc = load_json(data_dir + sep + "cards.json");
    try {
        for (const auto& s : js) {
            School sc;
            sc.id = s.at("id").get<std::string>();
            sc.name = s.at("name").get<std::string>();
            sc.effect_type = s.value("effect_type", std::string());
            sc.core_belief = s.value("core_belief", std::string());
            sc.description = s.value("description", std::string());
            schools_.push_back(sc);
        }
        for (const auto& c : jc) {
            Card cd;
            cd.id = c.at("id").get<std::string>();
            cd.school_id = c.at("school_id").get<std::string>();
            cd.scenario = c.at("scenario").get<std::string>();
            const auto& ch = c.at("choices");
            for (int i = 0; i < 4; ++i) cd.choices[i] = ch.at(std::string(1, char('A' + i))).get<std::string>();
            cd.correct = c.at("correct").get<std::string>().at(0);
            cd.explanation = c.at("explanation").get<std::string>();
            cd.difficulty = c.at("difficulty").get<int>();
            cd.collectible = c.at("collectible").get<std::string>();
            const std::string ct = c.value("card_type", std::string("MONSTER"));
            cd.card_type = ct == "SPELL" ? CardType::Spell : ct == "TRAP" ? CardType::Trap : CardType::Monster;
            cd.cost = c.value("cost", 0);
            cd.atk = c.value("atk", 0);
            cd.hp = c.value("hp", 0);
            cd.tag = c.value("tag", cd.collectible);
            cd.rush = c.value("rush", false);
            const std::string trig = c.value("trigger", std::string());
            cd.trigger = trig == "ON_ATTACK" ? Trigger::OnAttack : trig == "ON_SUMMON" ? Trigger::OnSummon
                       : trig == "ON_SPELL" ? Trigger::OnSpell : Trigger::None;
            if (cd.card_type == CardType::Trap && cd.trigger == Trigger::None)
                throw CardEngineError("Trap card " + cd.id + " has no trigger.");
            if (c.contains("abilities"))
                for (const auto& a : c.at("abilities")) {
                    Ability ab;
                    if (!duel_cards::parse_timing(a.at("on").get<std::string>(), ab.on))
                        throw CardEngineError("Unknown ability timing on card " + cd.id);
                    ab.grave_cost = a.value("cost", 0);
                    ab.ops = parse_ops(a.at("do"), cd.id);
                    cd.abilities.push_back(std::move(ab));
                }
            cards_.push_back(cd);
        }
    } catch (const json::exception& e) {
        throw CardEngineError(std::string("Data file has an unexpected shape: ") + e.what());
    }
    for (std::size_t i = 0; i < schools_.size(); ++i) school_index_[schools_[i].id] = i;
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        card_index_[cards_[i].id] = i;
        cards_by_school_[cards_[i].school_id].push_back(i);
    }
    reset_session();
}

const School* CardEngine::get_school(const std::string& id) const {
    auto it = school_index_.find(id);
    return it == school_index_.end() ? nullptr : &schools_[it->second];
}
const Card* CardEngine::get_card(const std::string& id) const {
    auto it = card_index_.find(id);
    return it == card_index_.end() ? nullptr : &cards_[it->second];
}

void CardEngine::reset_session() {
    drawn_.clear();
    for (const auto& s : schools_) drawn_[s.id];
}

std::pair<const School*, const Card*> CardEngine::draw_card(const std::string& school_id_in) {
    std::string school_id = school_id_in;
    if (school_id.empty()) school_id = schools_[rand_index(schools_.size())].id;
    auto pit = cards_by_school_.find(school_id);
    if (pit == cards_by_school_.end() || pit->second.empty())
        throw CardEngineError("No cards found for school '" + school_id + "'.");
    auto& drawn = drawn_[school_id];
    std::vector<std::size_t> available;
    for (auto idx : pit->second)
        if (!drawn.count(cards_[idx].id)) available.push_back(idx);
    if (available.empty()) {
        drawn.clear();
        available = pit->second;
    }
    std::size_t pick = available[rand_index(available.size())];
    drawn.insert(cards_[pick].id);
    return {&schools_[school_index_.at(school_id)], &cards_[pick]};
}

}  // namespace fd
