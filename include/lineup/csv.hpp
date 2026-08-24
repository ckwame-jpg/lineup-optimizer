#pragma once

#include <istream>
#include <string>

#include "lineup/player.hpp"

namespace lineup {

struct CsvResult {
    PlayerPool players;
    bool ok{false};
    std::string error;  // empty when ok
};

/// Reads a player pool from CSV with the header
/// `name,position,salary,projection[,team]` in any column order.
///
/// Errors carry the 1-based line number, because a pool with one malformed row
/// out of six hundred is the common case and "parse error" alone is useless.
CsvResult read_players(std::istream& in);

}  // namespace lineup
