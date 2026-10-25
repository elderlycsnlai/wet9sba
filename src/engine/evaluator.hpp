#pragma once
// Module M2: Response Evaluation & Scoring (DFD Process 2.0).
#include "types.hpp"

namespace fd::evaluator {

constexpr int POINTS_CORRECT = 10;
constexpr int POINTS_PARTICIPATION = 2;

struct Result {
    bool is_correct = false;
    int points = 0;
    std::string explanation;
    char correct_choice = 'A';
};

inline Result evaluate(const Card& card, char student_choice) {
    bool ok = std::toupper(static_cast<unsigned char>(student_choice)) ==
              std::toupper(static_cast<unsigned char>(card.correct));
    return {ok, ok ? POINTS_CORRECT : POINTS_PARTICIPATION, card.explanation, card.correct};
}

}  // namespace fd::evaluator
