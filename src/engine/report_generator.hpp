#pragma once
// Module M5: Tutor Report Generator (DFD Process 4.0).
#include "card_engine.hpp"

namespace fd::report_generator {

struct Row {
    std::string school_id, school_name;
    int attempted = 0, correct = 0;
    std::optional<double> accuracy_pct;   // empty == "n/a"
    int wisdom_pct = 0;
};

struct Report {
    std::string txt_path, csv_path;
    std::vector<Row> rows;
    std::vector<std::string> weakest;
    std::vector<std::string> text_lines;   // the same lines written to the .txt file
};

Report generate_report(const Profile& profile, const LogData& log, const std::vector<School>& schools,
                       const std::string& reports_dir);

}  // namespace fd::report_generator
