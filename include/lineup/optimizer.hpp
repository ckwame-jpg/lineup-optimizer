#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "lineup/player.hpp"
#include "lineup/roster.hpp"

namespace lineup {

/// A complete, cap-legal roster. `player_indices` are indices into the pool the
/// optimizer was constructed with, always in ascending order.
struct Lineup {
    std::vector<int> player_indices;
    int total_salary{0};
    int total_projection_centipoints{0};

    double projection() const { return total_projection_centipoints / 100.0; }
};

struct SolveStats {
    /// Dynamic-programming cell updates performed across all position tables.
    std::uint64_t dp_cell_updates{0};
    /// Exact subproblems solved. One for the optimum, plus Murty partitions.
    std::uint64_t subproblems_solved{0};
    std::uint64_t lineups_completed{0};
    double elapsed_ms{0.0};
};

/// Exact top-K solver for salary-capped roster construction.
///
/// Positions partition the player pool, so once the number of players taken at
/// each position is fixed the problem separates: each position independently
/// solves "best j players for exactly this much salary", and the results are
/// combined over the salary axis. That is a bounded knapsack per position plus
/// a convolution, which is polynomial — unlike searching player subsets, which
/// is not.
///
/// The set of position-count vectors that can fill the roster is precomputed
/// with bipartite matching, so FLEX slots need no special handling: a running
/// back in RB2 versus FLEX is the same count vector, and therefore solved once.
///
/// Ranked lineups after the first come from Murty's partition: having found the
/// best roster, the remaining solution space is split into subproblems that
/// each force one of its players out, and the best of those is the runner-up.
class Optimizer {
  public:
    Optimizer(RosterRules rules, PlayerPool pool);

    /// Returns up to `k` lineups sorted by projection, highest first.
    /// Returns an empty vector when the pool cannot fill the roster under cap.
    std::vector<Lineup> solve(int k, SolveStats* stats = nullptr) const;

    /// Assigns a solved lineup's players to concrete roster slots.
    /// Index `i` of the result is the player index filling `rules().slots[i]`.
    /// Returns nullopt only if `lineup` is not slot-legal.
    std::optional<std::vector<int>> assign_to_slots(const Lineup& lineup) const;

    const RosterRules& rules() const { return rules_; }
    const PlayerPool& pool() const { return pool_; }

  private:
    /// Constraints defining one node of the Murty partition.
    struct Restriction {
        std::vector<int> forced_in;   // pool indices that must appear
        std::vector<bool> excluded;   // pool indices that must not appear
    };

    struct PositionTable;

    bool counts_can_fill_roster(const std::array<int, kPositionCount>& counts) const;
    void build_feasible_counts();

    PositionTable build_position_table(Position position, int max_take,
                                       const Restriction& restriction,
                                       SolveStats& stats) const;

    std::optional<Lineup> solve_restricted(const Restriction& restriction,
                                           SolveStats& stats) const;

    RosterRules rules_;
    PlayerPool pool_;

    /// Salary quantum: the GCD of every salary and the cap. Real slates price in
    /// round numbers, so this collapses the salary axis by 100x or more, and it
    /// is exact rather than an approximation.
    int salary_step_{1};
    int cap_buckets_{0};

    std::array<std::vector<int>, kPositionCount> by_position_;
    std::vector<std::array<int, kPositionCount>> feasible_counts_;
    std::array<int, kPositionCount> max_count_{};
    std::size_t roster_size_{0};
};

}  // namespace lineup
