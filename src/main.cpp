#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "lineup/csv.hpp"
#include "lineup/optimizer.hpp"
#include "lineup/roster.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " <players.csv> [options]\n\n"
        << "Builds the highest-projecting salary-cap-legal lineups from a player pool.\n\n"
        << "Options:\n"
        << "  -n, --lineups <k>   number of lineups to return (default 1)\n"
        << "  -c, --cap <salary>  salary cap (default 50000)\n"
        << "  -v, --verbose       print search statistics\n"
        << "  -h, --help          show this message\n";
}

void print_lineup(const lineup::Optimizer& opt, const lineup::Lineup& l, int rank) {
    const auto slots = opt.assign_to_slots(l);
    std::cout << "\nLineup " << rank << "  -  " << std::fixed << std::setprecision(2)
              << l.projection() << " pts  |  $" << l.total_salary << "\n";
    std::cout << std::string(58, '-') << "\n";

    if (!slots) {
        std::cout << "  (could not assign slots)\n";
        return;
    }
    for (std::size_t i = 0; i < slots->size(); ++i) {
        const auto& player = opt.pool()[(*slots)[i]];
        std::cout << "  " << std::left << std::setw(6) << opt.rules().slots[i].label
                  << std::setw(24) << player.name
                  << std::setw(5) << lineup::to_string(player.position)
                  << std::right << std::setw(7) << ("$" + std::to_string(player.salary))
                  << std::setw(9) << std::fixed << std::setprecision(2) << player.projection()
                  << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    int lineups = 1;
    int cap = 50000;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-n" || arg == "--lineups") && i + 1 < argc) {
            lineups = std::atoi(argv[++i]);
        } else if ((arg == "-c" || arg == "--cap") && i + 1 < argc) {
            cap = std::atoi(argv[++i]);
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (!arg.empty() && arg[0] != '-') {
            path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }

    if (path.empty()) {
        print_usage(argv[0]);
        return 2;
    }
    if (lineups < 1) {
        std::cerr << "Number of lineups must be at least 1.\n";
        return 2;
    }

    std::ifstream file(path);
    if (!file) {
        std::cerr << "Cannot open '" << path << "'.\n";
        return 1;
    }

    auto parsed = lineup::read_players(file);
    if (!parsed.ok) {
        std::cerr << path << ": " << parsed.error << "\n";
        return 1;
    }

    auto rules = lineup::draftkings_nfl_classic();
    rules.salary_cap = cap;

    lineup::Optimizer optimizer(std::move(rules), std::move(parsed.players));
    lineup::SolveStats stats;
    const auto results = optimizer.solve(lineups, &stats);

    if (results.empty()) {
        std::cerr << "No cap-legal lineup exists for this pool.\n";
        return 1;
    }

    std::cout << "Pool: " << optimizer.pool().size() << " players   Cap: $" << cap << "\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        print_lineup(optimizer, results[i], static_cast<int>(i) + 1);
    }

    if (verbose) {
        std::cout << "\nSolve: " << stats.dp_cell_updates << " DP updates, "
                  << stats.subproblems_solved << " subproblems, " << stats.lineups_completed
                  << " lineups, " << std::fixed << std::setprecision(1) << stats.elapsed_ms
                  << " ms\n";
    }
    return 0;
}
