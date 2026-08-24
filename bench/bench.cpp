#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "lineup/optimizer.hpp"
#include "lineup/roster.hpp"

using namespace lineup;

namespace {

/// Deterministic pool so numbers are comparable between runs and machines.
/// Salary correlates with projection plus noise, which mirrors a real slate:
/// if salary were independent, pruning would look far better than it is.
PlayerPool synthetic_pool(int per_position_scale, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 1.6);

    const std::vector<std::pair<Position, int>> shape = {
        {Position::QB, 2}, {Position::RB, 4}, {Position::WR, 6},
        {Position::TE, 2}, {Position::DST, 1},
    };

    PlayerPool pool;
    int id = 0;
    for (const auto& [pos, weight] : shape) {
        const int count = weight * per_position_scale;
        for (int i = 0; i < count; ++i) {
            const double tier = 1.0 - static_cast<double>(i) / std::max(1, count);
            const double points = 4.0 + tier * 20.0 + noise(rng);
            const double salary = 2500.0 + tier * 6500.0 + noise(rng) * 250.0;

            Player p;
            p.name = std::string(to_string(pos)) + std::to_string(id++);
            p.position = pos;
            p.salary = static_cast<int>(salary / 100.0) * 100;
            p.projection_centipoints = static_cast<int>(points * 100.0);
            if (p.projection_centipoints < 0) p.projection_centipoints = 0;
            pool.push_back(std::move(p));
        }
    }
    return pool;
}

void run(int scale, int k) {
    const auto pool = synthetic_pool(scale, 424242u);
    Optimizer opt(draftkings_nfl_classic(), pool);

    SolveStats stats;
    const auto results = opt.solve(k, &stats);

    std::printf("%7zu %6d %11.2f %14llu %13llu %10.1f\n", pool.size(), k,
                results.empty() ? 0.0 : results.front().projection(),
                static_cast<unsigned long long>(stats.dp_cell_updates),
                static_cast<unsigned long long>(stats.subproblems_solved), stats.elapsed_ms);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    int max_scale = (argc > 1) ? std::atoi(argv[1]) : 24;

    std::printf("%7s %6s %11s %14s %13s %10s\n", "players", "K", "best pts", "DP updates",
                "subproblems", "ms");
    std::printf("--------------------------------------------------------------------\n");

    for (int scale : {2, 4, 8, 12, 16, 20, 24, 32, 40}) {
        if (scale > max_scale) break;
        run(scale, 1);
    }

    std::printf("\nTop-K at 300 players:\n");
    std::printf("%7s %6s %11s %14s %13s %10s\n", "players", "K", "best pts", "DP updates",
                "subproblems", "ms");
    std::printf("--------------------------------------------------------------------\n");
    for (int k : {1, 10, 50, 150}) {
        run(20, k);
    }
    return 0;
}
