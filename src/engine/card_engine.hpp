#pragma once
// Module M1: Card & Scenario Engine (DFD Process 1.0).
// Loads data/schools.json + data/cards.json and draws a scenario card at random
// without repeating one within a session until the school's pool is exhausted.
#include <set>
#include <stdexcept>
#include "types.hpp"

namespace fd {

class CardEngineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CardEngine {
public:
    explicit CardEngine(const std::string& data_dir);

    const std::vector<School>& all_schools() const { return schools_; }
    const std::vector<Card>& cards() const { return cards_; }
    const School* get_school(const std::string& id) const;
    const Card* get_card(const std::string& id) const;

    void reset_session();
    // school_id empty => random school. Returns the school and the drawn card.
    std::pair<const School*, const Card*> draw_card(const std::string& school_id = "");

private:
    std::vector<School> schools_;
    std::vector<Card> cards_;
    std::map<std::string, std::size_t> school_index_, card_index_;
    std::map<std::string, std::vector<std::size_t>> cards_by_school_;
    std::map<std::string, std::set<std::string>> drawn_;
};

}  // namespace fd
