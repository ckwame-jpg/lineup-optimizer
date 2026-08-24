#include "lineup/roster.hpp"

namespace lineup {

Slot make_slot(std::string label, std::initializer_list<Position> positions) {
    Slot slot;
    slot.label = std::move(label);
    slot.accepts.fill(false);
    for (Position p : positions) {
        slot.accepts[static_cast<std::size_t>(p)] = true;
    }
    return slot;
}

RosterRules draftkings_nfl_classic() {
    RosterRules rules;
    rules.salary_cap = 50000;
    rules.slots = {
        make_slot("QB", {Position::QB}),
        make_slot("RB1", {Position::RB}),
        make_slot("RB2", {Position::RB}),
        make_slot("WR1", {Position::WR}),
        make_slot("WR2", {Position::WR}),
        make_slot("WR3", {Position::WR}),
        make_slot("TE", {Position::TE}),
        make_slot("FLEX", {Position::RB, Position::WR, Position::TE}),
        make_slot("DST", {Position::DST}),
    };
    return rules;
}

}  // namespace lineup
