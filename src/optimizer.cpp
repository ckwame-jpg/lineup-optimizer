#include "lineup/optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

namespace lineup {
namespace {

constexpr int kUnreachable = -1;
constexpr int kNoPlayer = -1;

/// Kuhn's algorithm, one augmenting-path attempt for `slot`, matching slots
/// against position buckets with capacities.
bool try_match(const std::vector<Slot>& slots, std::size_t slot,
               std::array<int, kPositionCount>& bucket_remaining,
               std::vector<int>& slot_assignment, std::vector<bool>& visited) {
    for (int p = 0; p < kPositionCount; ++p) {
        if (!slots[slot].allows(static_cast<Position>(p))) continue;
        if (visited[p]) continue;
        visited[p] = true;

        if (bucket_remaining[p] > 0) {
            bucket_remaining[p]--;
            slot_assignment[slot] = p;
            return true;
        }
        for (std::size_t other = 0; other < slots.size(); ++other) {
            if (slot_assignment[other] != p) continue;
            slot_assignment[other] = -1;
            bucket_remaining[p]++;
            if (try_match(slots, other, bucket_remaining, slot_assignment, visited)) {
                bucket_remaining[p]--;
                slot_assignment[slot] = p;
                return true;
            }
            bucket_remaining[p]--;
            slot_assignment[other] = p;
        }
    }
    return false;
}

}  // namespace

/// Layered knapsack table for a single position.
///
/// `value[i][j * buckets + s]` is the best projection using exactly j players
/// drawn from the first i candidates at exactly s salary buckets. Keeping every
/// item layer costs a few megabytes and makes reconstruction exact: a cell
/// differs from the layer below precisely when candidate i-1 was taken, so no
/// player can be recovered twice.
struct Optimizer::PositionTable {
    std::vector<int> candidates;  // pool indices, this position only
    std::vector<std::vector<int>> value;
    int max_take{0};
    int buckets{0};

    int at(int layer, int take, int bucket) const {
        return value[layer][take * buckets + bucket];
    }
};

Optimizer::Optimizer(RosterRules rules, PlayerPool pool)
    : rules_(std::move(rules)), pool_(std::move(pool)) {
    roster_size_ = rules_.slots.size();
    if (roster_size_ == 0) throw std::invalid_argument("roster has no slots");
    if (rules_.salary_cap < 0) throw std::invalid_argument("salary cap is negative");

    for (std::size_t i = 0; i < pool_.size(); ++i) {
        if (pool_[i].salary < 0) throw std::invalid_argument("player salary is negative");
        by_position_[static_cast<std::size_t>(pool_[i].position)].push_back(static_cast<int>(i));
    }

    int step = rules_.salary_cap;
    for (const Player& p : pool_) step = std::gcd(step, p.salary);
    salary_step_ = std::max(1, step);
    cap_buckets_ = rules_.salary_cap / salary_step_;

    // Strongest players first, so Murty's partition tends to branch on the
    // players most likely to matter and the first lineups surface quickly.
    for (auto& list : by_position_) {
        std::sort(list.begin(), list.end(), [this](int a, int b) {
            if (pool_[a].projection_centipoints != pool_[b].projection_centipoints) {
                return pool_[a].projection_centipoints > pool_[b].projection_centipoints;
            }
            return a < b;
        });
    }

    build_feasible_counts();
}

bool Optimizer::counts_can_fill_roster(const std::array<int, kPositionCount>& counts) const {
    int total = 0;
    for (int c : counts) total += c;
    if (total != static_cast<int>(roster_size_)) return false;

    std::array<int, kPositionCount> remaining = counts;
    std::vector<int> assignment(roster_size_, -1);
    for (std::size_t slot = 0; slot < roster_size_; ++slot) {
        std::vector<bool> visited(kPositionCount, false);
        if (!try_match(rules_.slots, slot, remaining, assignment, visited)) return false;
    }
    return true;
}

void Optimizer::build_feasible_counts() {
    std::array<int, kPositionCount> cap{};
    for (int p = 0; p < kPositionCount; ++p) {
        cap[p] = 0;
        for (const Slot& s : rules_.slots) {
            if (s.allows(static_cast<Position>(p))) cap[p]++;
        }
    }

    max_count_.fill(0);
    std::array<int, kPositionCount> counts{};
    while (true) {
        if (counts_can_fill_roster(counts)) {
            feasible_counts_.push_back(counts);
            for (int p = 0; p < kPositionCount; ++p) {
                max_count_[p] = std::max(max_count_[p], counts[p]);
            }
        }
        int p = kPositionCount - 1;
        while (p >= 0) {
            if (++counts[p] <= cap[p]) break;
            counts[p] = 0;
            --p;
        }
        if (p < 0) break;
    }
}

Optimizer::PositionTable Optimizer::build_position_table(Position position, int max_take,
                                                         const Restriction& restriction,
                                                         SolveStats& stats) const {
    PositionTable table;
    table.max_take = max_take;
    table.buckets = cap_buckets_ + 1;

    for (int index : by_position_[static_cast<std::size_t>(position)]) {
        if (!restriction.excluded[static_cast<std::size_t>(index)]) {
            table.candidates.push_back(index);
        }
    }

    const int layers = static_cast<int>(table.candidates.size()) + 1;
    const int row = (max_take + 1) * table.buckets;
    table.value.assign(layers, std::vector<int>(row, kUnreachable));
    table.value[0][0] = 0;  // zero players, zero salary

    for (int i = 1; i < layers; ++i) {
        const Player& player = pool_[table.candidates[i - 1]];
        const int cost = player.salary / salary_step_;
        table.value[i] = table.value[i - 1];

        for (int take = max_take; take >= 1; --take) {
            for (int bucket = cap_buckets_; bucket >= cost; --bucket) {
                const int without = table.value[i - 1][(take - 1) * table.buckets + bucket - cost];
                if (without == kUnreachable) continue;
                const int candidate = without + player.projection_centipoints;
                int& cell = table.value[i][take * table.buckets + bucket];
                if (candidate > cell) {
                    cell = candidate;
                    stats.dp_cell_updates++;
                }
            }
        }
    }
    return table;
}

std::optional<Lineup> Optimizer::solve_restricted(const Restriction& restriction,
                                                  SolveStats& stats) const {
    stats.subproblems_solved++;

    // Forced players are removed from the tables and their cost paid up front,
    // so the DP only ever solves the free remainder.
    std::array<int, kPositionCount> forced_counts{};
    int forced_salary = 0;
    int forced_projection = 0;
    for (int index : restriction.forced_in) {
        const Player& p = pool_[static_cast<std::size_t>(index)];
        forced_counts[static_cast<std::size_t>(p.position)]++;
        forced_salary += p.salary;
        forced_projection += p.projection_centipoints;
    }
    if (forced_salary > rules_.salary_cap) return std::nullopt;

    Restriction effective = restriction;
    for (int index : restriction.forced_in) {
        effective.excluded[static_cast<std::size_t>(index)] = true;
    }

    std::array<int, kPositionCount> need_max{};
    for (int p = 0; p < kPositionCount; ++p) {
        need_max[p] = std::max(0, max_count_[p] - forced_counts[p]);
    }

    std::array<PositionTable, kPositionCount> tables;
    for (int p = 0; p < kPositionCount; ++p) {
        tables[p] = build_position_table(static_cast<Position>(p), need_max[p], effective, stats);
    }

    const int free_budget = (rules_.salary_cap - forced_salary) / salary_step_;

    int best_value = kUnreachable;
    std::array<int, kPositionCount> best_counts{};
    std::array<int, kPositionCount> best_buckets{};

    for (const auto& counts : feasible_counts_) {
        std::array<int, kPositionCount> need{};
        bool viable = true;
        for (int p = 0; p < kPositionCount; ++p) {
            need[p] = counts[p] - forced_counts[p];
            if (need[p] < 0 || need[p] > need_max[p]) { viable = false; break; }
        }
        if (!viable) continue;

        // Convolve the position tables over the salary axis. `running[b]` is the
        // best projection for the positions folded in so far at exactly b buckets.
        std::vector<int> running(free_budget + 1, kUnreachable);
        std::vector<std::array<int, kPositionCount>> split(free_budget + 1);
        running[0] = 0;

        for (int p = 0; p < kPositionCount; ++p) {
            const PositionTable& table = tables[p];
            const int layer = static_cast<int>(table.candidates.size());
            if (need[p] > layer) { running.assign(free_budget + 1, kUnreachable); break; }

            std::vector<int> next(free_budget + 1, kUnreachable);
            std::vector<std::array<int, kPositionCount>> next_split(free_budget + 1);

            for (int used = 0; used <= free_budget; ++used) {
                if (running[used] == kUnreachable) continue;
                for (int mine = 0; mine + used <= free_budget; ++mine) {
                    const int gain = table.at(layer, need[p], mine);
                    if (gain == kUnreachable) continue;
                    const int total = running[used] + gain;
                    if (total > next[used + mine]) {
                        next[used + mine] = total;
                        next_split[used + mine] = split[used];
                        next_split[used + mine][p] = mine;
                    }
                }
            }
            running.swap(next);
            split.swap(next_split);
        }

        for (int b = 0; b <= free_budget; ++b) {
            if (running[b] == kUnreachable) continue;
            if (running[b] > best_value) {
                best_value = running[b];
                best_counts = counts;
                best_buckets = split[b];
            }
        }
    }

    if (best_value == kUnreachable) return std::nullopt;

    Lineup lineup;
    lineup.player_indices = restriction.forced_in;
    lineup.total_salary = forced_salary;
    lineup.total_projection_centipoints = forced_projection + best_value;

    // Walk each position table back down its layers; a cell that differs from
    // the layer below is exactly where that candidate was taken.
    for (int p = 0; p < kPositionCount; ++p) {
        const PositionTable& table = tables[p];
        int take = best_counts[p] - forced_counts[p];
        int bucket = best_buckets[p];
        int layer = static_cast<int>(table.candidates.size());

        while (take > 0 && layer > 0) {
            if (table.at(layer, take, bucket) == table.at(layer - 1, take, bucket)) {
                --layer;
                continue;
            }
            const int index = table.candidates[layer - 1];
            const Player& player = pool_[static_cast<std::size_t>(index)];
            lineup.player_indices.push_back(index);
            lineup.total_salary += player.salary;
            bucket -= player.salary / salary_step_;
            --take;
            --layer;
        }
        if (take > 0) return std::nullopt;  // table and split disagreed
    }

    std::sort(lineup.player_indices.begin(), lineup.player_indices.end());
    return lineup;
}

std::vector<Lineup> Optimizer::solve(int k, SolveStats* stats_out) const {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();

    SolveStats stats;
    std::vector<Lineup> results;
    const int wanted = std::max(1, k);

    struct Node {
        Lineup lineup;
        Restriction restriction;
    };
    struct Worse {
        bool operator()(const Node& a, const Node& b) const {
            return a.lineup.total_projection_centipoints < b.lineup.total_projection_centipoints;
        }
    };

    Restriction root;
    root.excluded.assign(pool_.size(), false);

    std::priority_queue<Node, std::vector<Node>, Worse> frontier;
    if (auto best = solve_restricted(root, stats)) {
        frontier.push(Node{std::move(*best), std::move(root)});
    }

    std::set<std::vector<int>> emitted;

    while (!frontier.empty() && static_cast<int>(results.size()) < wanted) {
        Node node = frontier.top();
        frontier.pop();

        if (!emitted.insert(node.lineup.player_indices).second) continue;
        results.push_back(node.lineup);
        stats.lineups_completed++;

        // Murty's partition. The players of this lineup that were not already
        // forced are split into subproblems: the first i are forced in and the
        // (i+1)-th forced out. Every other roster falls into exactly one part,
        // so nothing is missed and nothing is generated twice.
        std::vector<int> free_players;
        for (int index : node.lineup.player_indices) {
            const bool already_forced =
                std::find(node.restriction.forced_in.begin(),
                          node.restriction.forced_in.end(), index) !=
                node.restriction.forced_in.end();
            if (!already_forced) free_players.push_back(index);
        }

        Restriction branch = node.restriction;
        for (std::size_t i = 0; i < free_players.size(); ++i) {
            Restriction child = branch;
            child.excluded[static_cast<std::size_t>(free_players[i])] = true;

            if (auto candidate = solve_restricted(child, stats)) {
                frontier.push(Node{std::move(*candidate), std::move(child)});
            }
            branch.forced_in.push_back(free_players[i]);
        }
    }

    stats.elapsed_ms =
        std::chrono::duration<double, std::milli>(clock::now() - started).count();
    if (stats_out) *stats_out = stats;
    return results;
}

std::optional<std::vector<int>> Optimizer::assign_to_slots(const Lineup& lineup) const {
    if (lineup.player_indices.size() != roster_size_) return std::nullopt;

    std::vector<int> slot_to_player(roster_size_, kNoPlayer);
    std::vector<bool> player_used(lineup.player_indices.size(), false);

    auto augment = [&](auto&& self, std::size_t slot, std::vector<bool>& seen) -> bool {
        for (std::size_t j = 0; j < lineup.player_indices.size(); ++j) {
            if (seen[j]) continue;
            const Player& p = pool_[static_cast<std::size_t>(lineup.player_indices[j])];
            if (!rules_.slots[slot].allows(p.position)) continue;
            seen[j] = true;

            if (!player_used[j]) {
                player_used[j] = true;
                slot_to_player[slot] = lineup.player_indices[j];
                return true;
            }
            for (std::size_t other = 0; other < roster_size_; ++other) {
                if (slot_to_player[other] != lineup.player_indices[j]) continue;
                if (self(self, other, seen)) {
                    slot_to_player[slot] = lineup.player_indices[j];
                    return true;
                }
            }
        }
        return false;
    };

    for (std::size_t slot = 0; slot < roster_size_; ++slot) {
        std::vector<bool> seen(lineup.player_indices.size(), false);
        if (!augment(augment, slot, seen)) return std::nullopt;
    }
    return slot_to_player;
}

}  // namespace lineup
