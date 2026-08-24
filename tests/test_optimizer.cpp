#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include "lineup/optimizer.hpp"
#include "lineup/roster.hpp"

using namespace lineup;

namespace {

Player make(std::string name, Position pos, int salary, int centipoints) {
    Player p;
    p.name = std::move(name);
    p.position = pos;
    p.salary = salary;
    p.projection_centipoints = centipoints;
    return p;
}

/// Smallest pool that can fill the DraftKings roster exactly once:
/// 1 QB, 3 RB, 4 WR, 2 TE, 1 DST. Any nine-player subset that is slot-legal
/// must therefore use the QB, the DST, and seven of the RB/WR/TE group.
PlayerPool minimal_pool() {
    return {
        make("QB1", Position::QB, 7000, 2000),
        make("RB1", Position::RB, 8000, 1800),
        make("RB2", Position::RB, 6000, 1400),
        make("RB3", Position::RB, 4000, 1000),
        make("WR1", Position::WR, 7500, 1700),
        make("WR2", Position::WR, 6500, 1500),
        make("WR3", Position::WR, 5000, 1200),
        make("WR4", Position::WR, 3500, 900),
        make("TE1", Position::TE, 5500, 1300),
        make("TE2", Position::TE, 3000, 700),
        make("DST1", Position::DST, 2500, 600),
    };
}

/// Reference solver: every 9-subset, filtered by cap and slot legality.
/// Exponential and only usable on tiny pools, which is exactly what makes it a
/// trustworthy oracle for the branch-and-bound search.
std::vector<Lineup> brute_force(const RosterRules& rules, const PlayerPool& pool, int k) {
    const std::size_t n = pool.size();
    const std::size_t r = rules.slots.size();
    std::vector<Lineup> found;

    std::vector<int> combo(r);
    std::function<void(std::size_t, std::size_t)> recurse = [&](std::size_t start,
                                                                std::size_t depth) {
        if (depth == r) {
            int salary = 0;
            int projection = 0;
            for (int idx : combo) {
                salary += pool[idx].salary;
                projection += pool[idx].projection_centipoints;
            }
            if (salary > rules.salary_cap) return;

            Lineup candidate;
            candidate.player_indices = combo;
            candidate.total_salary = salary;
            candidate.total_projection_centipoints = projection;

            Optimizer probe(rules, pool);
            if (!probe.assign_to_slots(candidate).has_value()) return;
            found.push_back(std::move(candidate));
            return;
        }
        for (std::size_t i = start; i < n; ++i) {
            combo[depth] = static_cast<int>(i);
            recurse(i + 1, depth + 1);
        }
    };
    recurse(0, 0);

    std::stable_sort(found.begin(), found.end(), [](const Lineup& a, const Lineup& b) {
        return a.total_projection_centipoints > b.total_projection_centipoints;
    });
    if (static_cast<int>(found.size()) > k) found.resize(k);
    return found;
}

}  // namespace

TEST(Optimizer, FindsALineupFromAMinimalPool) {
    Optimizer opt(draftkings_nfl_classic(), minimal_pool());
    const auto results = opt.solve(1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].player_indices.size(), 9u);
    EXPECT_LE(results[0].total_salary, 50000);
}

TEST(Optimizer, RespectsTheSalaryCap) {
    auto rules = draftkings_nfl_classic();
    rules.salary_cap = 45000;
    Optimizer opt(rules, minimal_pool());

    for (const auto& l : opt.solve(20)) {
        EXPECT_LE(l.total_salary, 45000);
    }
}

TEST(Optimizer, ReturnsNothingWhenCapIsUnreachable) {
    auto rules = draftkings_nfl_classic();
    rules.salary_cap = 1000;  // far below the cheapest legal roster
    Optimizer opt(rules, minimal_pool());
    EXPECT_TRUE(opt.solve(5).empty());
}

TEST(Optimizer, ReturnsNothingWhenAPositionIsMissing) {
    PlayerPool pool = minimal_pool();
    pool.erase(std::remove_if(pool.begin(), pool.end(),
                              [](const Player& p) { return p.position == Position::DST; }),
               pool.end());

    Optimizer opt(draftkings_nfl_classic(), pool);
    EXPECT_TRUE(opt.solve(1).empty());
}

TEST(Optimizer, EveryLineupSatisfiesSlotRules) {
    Optimizer opt(draftkings_nfl_classic(), minimal_pool());

    for (const auto& l : opt.solve(50)) {
        const auto slots = opt.assign_to_slots(l);
        ASSERT_TRUE(slots.has_value());
        ASSERT_EQ(slots->size(), opt.rules().size());

        std::set<int> used;
        for (std::size_t i = 0; i < slots->size(); ++i) {
            const int player_index = (*slots)[i];
            EXPECT_TRUE(opt.rules().slots[i].allows(opt.pool()[player_index].position))
                << "slot " << opt.rules().slots[i].label << " rejected "
                << to_string(opt.pool()[player_index].position);
            EXPECT_TRUE(used.insert(player_index).second) << "player used in two slots";
        }
    }
}

TEST(Optimizer, LineupsAreDistinctAndDescending) {
    Optimizer opt(draftkings_nfl_classic(), minimal_pool());
    const auto results = opt.solve(40);
    ASSERT_GT(results.size(), 1u);

    std::set<std::vector<int>> seen;
    for (std::size_t i = 0; i < results.size(); ++i) {
        EXPECT_TRUE(std::is_sorted(results[i].player_indices.begin(),
                                   results[i].player_indices.end()));
        EXPECT_TRUE(seen.insert(results[i].player_indices).second)
            << "duplicate roster at rank " << i;
        if (i > 0) {
            EXPECT_LE(results[i].total_projection_centipoints,
                      results[i - 1].total_projection_centipoints);
        }
    }
}

TEST(Optimizer, MatchesBruteForceOnTheMinimalPool) {
    const auto rules = draftkings_nfl_classic();
    const auto pool = minimal_pool();

    const auto expected = brute_force(rules, pool, 10);
    const auto actual = Optimizer(rules, pool).solve(10);

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].total_projection_centipoints,
                  expected[i].total_projection_centipoints)
            << "rank " << i << " projection differs from brute force";
    }
}

TEST(Optimizer, MatchesBruteForceOnRandomPools) {
    std::mt19937 rng(20260824);
    std::uniform_int_distribution<int> salary(2500, 9500);
    std::uniform_int_distribution<int> points(300, 2600);

    const auto rules = draftkings_nfl_classic();

    for (int trial = 0; trial < 8; ++trial) {
        PlayerPool pool;
        // Two spare bodies at each position keeps the search non-trivial while
        // leaving brute force tractable.
        const std::vector<std::pair<Position, int>> shape = {
            {Position::QB, 3}, {Position::RB, 4}, {Position::WR, 5},
            {Position::TE, 3}, {Position::DST, 2},
        };
        int id = 0;
        for (const auto& [pos, count] : shape) {
            for (int i = 0; i < count; ++i) {
                pool.push_back(make(std::string(to_string(pos)) + std::to_string(id++), pos,
                                    salary(rng), points(rng)));
            }
        }

        const auto expected = brute_force(rules, pool, 5);
        const auto actual = Optimizer(rules, pool).solve(5);

        ASSERT_EQ(actual.size(), expected.size()) << "trial " << trial;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(actual[i].total_projection_centipoints,
                      expected[i].total_projection_centipoints)
                << "trial " << trial << ", rank " << i;
        }
    }
}

TEST(Optimizer, RequestingMoreLineupsThanExistReturnsAllOfThem) {
    const auto rules = draftkings_nfl_classic();
    const auto pool = minimal_pool();

    const auto every = brute_force(rules, pool, 100000);
    const auto actual = Optimizer(rules, pool).solve(100000);
    EXPECT_EQ(actual.size(), every.size());
}

TEST(Optimizer, PruningDoesNotChangeTheAnswer) {
    // Solving for one lineup prunes far more aggressively than solving for many,
    // because the threshold is the best found rather than the K-th best. Both
    // must still agree on the top lineup.
    const auto rules = draftkings_nfl_classic();
    const auto pool = minimal_pool();
    Optimizer opt(rules, pool);

    const auto one = opt.solve(1);
    const auto many = opt.solve(25);

    ASSERT_FALSE(one.empty());
    ASSERT_FALSE(many.empty());
    EXPECT_EQ(one[0].total_projection_centipoints, many[0].total_projection_centipoints);
}

TEST(Optimizer, ReportsSearchStatistics) {
    Optimizer opt(draftkings_nfl_classic(), minimal_pool());
    SolveStats stats;
    opt.solve(3, &stats);

    EXPECT_GT(stats.dp_cell_updates, 0u);
    EXPECT_GT(stats.lineups_completed, 0u);
    EXPECT_GT(stats.subproblems_solved, 0u);
    EXPECT_GE(stats.elapsed_ms, 0.0);
}

TEST(Optimizer, HandlesAnEmptyPool) {
    Optimizer opt(draftkings_nfl_classic(), PlayerPool{});
    EXPECT_TRUE(opt.solve(1).empty());
}
