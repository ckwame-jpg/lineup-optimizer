#include "lineup/csv.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace lineup {
namespace {

std::string trim(std::string_view s) {
    const auto not_space = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r'; };
    auto begin = std::find_if(s.begin(), s.end(), not_space);
    auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string();
}

/// Splits one CSV record, honouring double-quoted fields so that a player name
/// containing a comma does not shift every later column.
std::vector<std::string> split_record(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(trim(field));
    return fields;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

CsvResult fail(std::size_t line_no, const std::string& message) {
    CsvResult r;
    r.ok = false;
    r.error = "line " + std::to_string(line_no) + ": " + message;
    return r;
}

}  // namespace

CsvResult read_players(std::istream& in) {
    std::string line;
    std::size_t line_no = 0;

    // Header
    std::unordered_map<std::string, std::size_t> column;
    while (std::getline(in, line)) {
        ++line_no;
        if (trim(line).empty()) continue;
        const auto header = split_record(line);
        for (std::size_t i = 0; i < header.size(); ++i) {
            column[lower(header[i])] = i;
        }
        break;
    }
    if (column.empty()) return fail(line_no, "file contains no header row");

    for (const char* required : {"name", "position", "salary", "projection"}) {
        if (column.find(required) == column.end()) {
            return fail(line_no, std::string("header is missing required column '") + required + "'");
        }
    }

    CsvResult result;
    const std::size_t widest =
        std::max_element(column.begin(), column.end(),
                         [](const auto& a, const auto& b) { return a.second < b.second; })
            ->second;

    while (std::getline(in, line)) {
        ++line_no;
        if (trim(line).empty()) continue;

        const auto fields = split_record(line);
        if (fields.size() <= widest) {
            return fail(line_no, "expected " + std::to_string(widest + 1) + " columns, found " +
                                     std::to_string(fields.size()));
        }

        Player p;
        p.name = fields[column.at("name")];
        if (p.name.empty()) return fail(line_no, "name is empty");

        const std::string& pos_text = fields[column.at("position")];
        if (!parse_position(pos_text, p.position)) {
            return fail(line_no, "unrecognised position '" + pos_text + "'");
        }

        const std::string& salary_text = fields[column.at("salary")];
        auto [ptr, ec] = std::from_chars(salary_text.data(),
                                         salary_text.data() + salary_text.size(), p.salary);
        if (ec != std::errc{} || ptr != salary_text.data() + salary_text.size()) {
            return fail(line_no, "salary '" + salary_text + "' is not an integer");
        }
        if (p.salary < 0) return fail(line_no, "salary is negative");

        const std::string& proj_text = fields[column.at("projection")];
        if (!parse_projection(proj_text, p.projection_centipoints)) {
            return fail(line_no, "projection '" + proj_text + "' is not a number");
        }

        if (auto it = column.find("team"); it != column.end() && it->second < fields.size()) {
            p.team = fields[it->second];
        }

        result.players.push_back(std::move(p));
    }

    result.ok = true;
    return result;
}

}  // namespace lineup
