#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lineup {

enum class Position : std::uint8_t { QB = 0, RB = 1, WR = 2, TE = 3, DST = 4 };

inline constexpr int kPositionCount = 5;

const char* to_string(Position p);

/// Parses "QB", "RB", "WR", "TE", "DST" (case-sensitive).
/// Returns false for anything else rather than throwing, so the CSV reader can
/// report the offending line number.
bool parse_position(std::string_view text, Position& out);

struct Player {
    std::string name;
    std::string team;
    Position position{Position::QB};
    int salary{0};
    /// Projected fantasy points, scaled by 100 and held as an integer.
    ///
    /// Branch and bound compares partial sums against a bound millions of times
    /// per solve. Doing that in floating point makes the pruning test sensitive
    /// to accumulated rounding: two lineups that are mathematically equal can
    /// compare unequal depending on the order their players were summed, which
    /// makes results depend on input ordering. Fixed-point keeps every
    /// comparison exact.
    int projection_centipoints{0};

    double projection() const { return projection_centipoints / 100.0; }
};

/// Converts a decimal projection ("18.4") to centipoints, rounding half away
/// from zero. Returns false if the text is not a number.
bool parse_projection(std::string_view text, int& centipoints_out);

using PlayerPool = std::vector<Player>;

}  // namespace lineup
