# lineup-optimizer

Exact salary-cap lineup optimizer for daily fantasy football, in C++17.

Given a pool of players with salaries and projected points, it returns the
highest-projecting roster that fits the cap — and the next K best after it,
ranked. The answers are optimal, not heuristic: no simulated annealing, no
genetic algorithm, no "good enough" greedy pass.

```
$ lineup -n 2 -v data/players_sample.csv

Pool: 178 players   Cap: $50000

Lineup 1  -  154.10 pts  |  $49900
----------------------------------------------------------
  QB    QB_CHI_21               QB     $2900     7.20
  RB1   RB_KC_47                RB     $2000     7.40
  RB2   RB_BUF_35               RB     $3900    11.40
  WR1   WR_CHI_37               WR     $5100    15.20
  WR2   WR_CHI_21               WR     $6600    22.00
  WR3   WR_IND_13               WR     $7000    21.80
  TE    TE_JAX_14               TE     $4800    15.40
  FLEX  WR_CAR_04               WR     $8700    26.70
  DST   DST_ATL_01              DST    $8900    27.00

Solve: 19206 DP updates, 18 subproblems, 2 lineups, 5.0 ms
```

## Build

Requires CMake 3.16+ and a C++17 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/lineup_tests          # 34 tests
./build/lineup data/players_sample.csv
```

## How it works

The obvious formulation — search over subsets of players — is exponential, and
that is not a theoretical concern. The first version of this solver did exactly
that, with branch and bound and two admissible pruning bounds. On a 300-player
pool it explored **16.8 billion nodes and took just over three minutes** to
return one lineup.

The problem is that no cheap bound is tight enough. Players are sorted by
projection, so the optimistic completion estimate happily assumes a roster of
nine wide receivers, and prunes almost nothing. Making the bound position-aware
cut the node count by only 16%, and cost more in per-node work than it saved.

The fix is to stop searching subsets. **Positions partition the pool**, so once
you fix *how many* players come from each position, the problem separates:

1. **Per-position knapsack.** For each position independently, compute the best
   projection achievable by taking exactly `j` players for exactly `s` salary.
   This is a layered 0/1 knapsack, `O(n_p · j_max · S)`.

2. **Convolve over salary.** Combine the five position tables along the salary
   axis to find the best split of the cap between them, `O(P · S²)`.

3. **Enumerate roster shapes.** Which count vectors are legal — is `(1 QB, 3 RB,
   3 WR, 1 TE, 1 DST)` fillable? — is decided once at construction by bipartite
   matching between slots and positions. This is what makes FLEX fall out for
   free: a running back in RB2 versus FLEX is the same count vector, so it is
   solved once rather than twice, and no deduplication pass is needed anywhere.

The salary axis is quantised by the GCD of every salary and the cap. Real slates
price in round hundreds, so this collapses 50,000 salary values into ~500
buckets exactly, with no approximation.

Ranked lineups after the first come from **Murty's partition**: given the best
roster, split the remaining solution space into subproblems that each force one
of its players out and the earlier ones in. Every other roster falls into
exactly one part, so the enumeration is complete and duplicate-free.

### What that bought

Same machine, same pools, same answers:

| Pool | Branch & bound | Dynamic programming | Speedup |
|-----:|---------------:|--------------------:|--------:|
| 30   | 0.3 ms         | 0.8 ms              | 0.4× |
| 60   | 23 ms          | 1.4 ms              | 16× |
| 120  | 1,036 ms       | 2.1 ms              | 493× |
| 180  | 13,802 ms      | 2.9 ms              | 4,759× |
| 240  | 23,340 ms      | 3.2 ms              | 7,294× |
| 300  | 182,666 ms     | 3.8 ms              | **48,070×** |

Branch and bound wins at 30 players, where setting up DP tables costs more than
just searching. Everywhere else it loses, and the gap widens without bound.

Scaling past where the old solver could go at all:

| Pool | Time | Pool | Time |
|-----:|-----:|-----:|-----:|
| 360  | 4.2 ms | 480 | 5.1 ms |
| 600  | 6.0 ms |     |        |

Top-K on a 300-player pool:

| K | Subproblems | Time |
|--:|------------:|-----:|
| 1   | 10  | 3.8 ms |
| 10  | 51  | 13.1 ms |
| 50  | 235 | 51.4 ms |
| 150 | 671 | 135.6 ms |

Reproduce with `./build/lineup_bench`.

## Correctness

Optimality claims are worth exactly as much as their evidence, so the top-K
results are checked against a brute-force oracle that enumerates every legal
9-player subset. The oracle is exponential and only usable on tiny pools, which
is precisely why it is trustworthy:

- `MatchesBruteForceOnTheMinimalPool` — top 10 lineups, exact projections
- `MatchesBruteForceOnRandomPools` — 8 randomised pools, top 5 each
- `RequestingMoreLineupsThanExistReturnsAllOfThem` — enumeration is complete

The DP and the old branch-and-bound implementation also independently agree to
the cent on every benchmark pool.

Plus the properties that should hold for any result: cap respected, no player in
two slots, every slot's position rules satisfied, lineups distinct and ordered,
and empty results rather than garbage when the pool cannot fill the roster.

Projections are parsed to fixed-point centipoints rather than `double`. Pruning
and DP comparisons run millions of times, and in binary floating point two
mathematically equal rosters can compare unequal depending on the order their
players were summed — which would make results depend on input ordering.

## Input format

CSV with a header. `name`, `position`, `salary` and `projection` are required;
`team` is optional. Column order does not matter and header names are
case-insensitive.

```csv
name,position,team,salary,projection
"Smith, Jr.",WR,KC,7800,18.4
```

Positions are `QB`, `RB`, `WR`, `TE`, `DST` (`DEF` is accepted for `DST`).
Quoted fields are handled, so a comma in a player's name does not shift every
later column. Parse errors name the 1-based line and what was wrong with it —
one malformed row in six hundred is the common case, and "parse error" alone is
useless.

## Usage

```
lineup <players.csv> [options]

  -n, --lineups <k>   number of lineups to return (default 1)
  -c, --cap <salary>  salary cap (default 50000)
  -v, --verbose       print solver statistics
  -h, --help          show usage
```

## Roster rules

The default is the DraftKings NFL classic roster: QB, RB, RB, WR, WR, WR, TE,
FLEX (RB/WR/TE), DST under $50,000. A slot is just a label plus the set of
positions it accepts, so other formats need no new solver logic:

```cpp
RosterRules rules;
rules.salary_cap = 60000;
rules.slots = {
    make_slot("QB",    {Position::QB}),
    make_slot("FLEX1", {Position::RB, Position::WR, Position::TE}),
    make_slot("FLEX2", {Position::RB, Position::WR, Position::TE}),
};
```

The feasible-count table is rebuilt by matching, so multi-flex and
superflex rosters work without touching the DP.

## Licence

MIT.
