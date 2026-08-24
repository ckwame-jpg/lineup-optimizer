#pragma once

#include <array>
#include <string>
#include <vector>

#include "lineup/player.hpp"

namespace lineup {

/// One roster slot: the set of positions it will accept.
///
/// A FLEX slot is just a slot that accepts several positions, so dedicated
/// slots and flex slots need no special-casing anywhere in the solver.
struct Slot {
    std::string label;
    std::array<bool, kPositionCount> accepts{};

    bool allows(Position p) const { return accepts[static_cast<std::size_t>(p)]; }
};

Slot make_slot(std::string label, std::initializer_list<Position> positions);

/// A roster shape plus the salary cap it must satisfy.
struct RosterRules {
    std::vector<Slot> slots;
    int salary_cap{0};

    std::size_t size() const { return slots.size(); }
};

/// Classic DraftKings NFL main-slate roster:
/// QB, RB, RB, WR, WR, WR, TE, FLEX (RB/WR/TE), DST under a $50,000 cap.
RosterRules draftkings_nfl_classic();

}  // namespace lineup
