# cdcl-sat

A CDCL SAT solver in dependency-free C++17 that emits DRAT proofs, so its
unsatisfiable answers are checked by somebody else's program.

[![CI](https://github.com/asp53826/cdcl-sat/actions/workflows/ci.yml/badge.svg)](https://github.com/asp53826/cdcl-sat/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square)
![dependencies](https://img.shields.io/badge/runtime_dependencies-0-2ea44f?style=flat-square)

> "Satisfiable" comes with an assignment anyone can check in linear time.
> "Unsatisfiable" comes with nothing at all, unless the solver writes down its
> reasoning. So it writes it down.

## What is actually implemented

- two-watched-literal propagation with blocking literals over an arena-allocated
  clause database;
- first-UIP conflict analysis with recursive clause minimization;
- VSIDS branching on a binary heap, with phase saving;
- Luby restarts;
- LBD-scored clause database reduction that never drops a locked or glue clause;
- root-level simplification;
- DRAT proof emission, including deletion lines;
- a DIMACS reader that treats the header as a hint rather than as truth.

## Measured, not implied

Apple M2 Pro, macOS, Apple Clang 17. Every number below is produced by a script
in this repository.

| Check | Result |
|---|---:|
| Random 3-SAT instances solved, ratio 3.6–5.2 | **300** |
| Unsatisfiable answers **verified by drat-trim** | **180 / 180** |
| Satisfiable answers verified against the source CNF | **120 / 120** |
| Verdicts agreeing with CaDiCaL 3.0.1 | **300 / 300** |
| DRAT proof lines emitted and checked | 108,206 |
| Unit-test assertions | **1,960 passed** |
| Configurations cross-checked for identical answers | 5 × 400 instances |

The verification row is the one that matters. `drat-trim` is Marijn Heule's
checker, fetched at build time rather than vendored so that it is unambiguously
not my code doing the verifying. It replays every learned clause, confirms each
follows from the ones above it by reverse unit propagation, and confirms the
last one is empty. It either prints `s VERIFIED` or it does not.

```
c 133 of 133 clauses in core
c 824 of 859 lemmas in core using 10805 resolution steps
s VERIFIED
```

## Verify it

```bash
make test
```

```bash
make drat-trim && make proof-test
```

```bash
make ablation
```

```bash
CADICAL=/path/to/cadical make benchmark
```

`proof-test` generates instances straddling the phase transition, solves them,
model-checks every SAT answer against the file it came from, and hands every
UNSAT answer to drat-trim. It exits non-zero if anything is rejected.

## What each part of CDCL is worth

A comparison against a state-of-the-art solver tells you that you are slower. It
does not tell you which ideas are carrying the result. So: the same instances,
one component disabled at a time.

<!--ABLATION-->

Conflicts is the search-quality metric; seconds is the throughput metric. They
are reported separately because a configuration can win on one and lose on the
other, and collapsing them into a single "speedup" hides exactly that.

## Distance to a real solver

<!--BENCHMARK-->

## Where it loses

**Pigeonhole, unconditionally.** PHP(n) has no polynomial-length resolution
proof — Haken, 1985 — and CDCL learns clauses by resolution. No amount of
engineering fixes this family; CaDiCaL is faster here and also loses. It is in
the benchmark because a benchmark set the approach always wins on is not
measuring the approach.

**The gap to CaDiCaL widens with instance size**, which says the difference is
not constant factors. It is inprocessing: variable elimination, subsumption,
vivification, probing. None of that is implemented here, and implementing it is
most of what separates a solver you can explain from a solver you can compete
with.

**Proofs are large and unbounded.** 108,206 lines for 180 small refutations.
On hard instances DRAT files reach hundreds of megabytes and checking can cost
more than solving. The proof is written straight through with a 64 KiB buffer
and no attempt at compression; binary DRAT and LRAT both exist and neither is
implemented.

**Emitting proofs is not free.** Every learned clause is written, including the
ones that are deleted twenty conflicts later. Instances where the solver learns
faster than the disk absorbs will be I/O bound, and the benchmark numbers above
are all measured *without* `--proof` for that reason — which is itself worth
stating rather than quietly doing.

**No incremental interface.** No assumptions, no IPASIR, no re-solve. Every call
starts from scratch, which rules out the use case that most applications of SAT
actually have.

**Verified UNSAT, asserted UNKNOWN.** With `--budget` the solver can return
UNKNOWN, and an UNKNOWN carries no evidence of anything. That is honest but it
is not a result.

## How a run works

```mermaid
flowchart LR
  P["DIMACS"] --> S["clause arena"]
  S --> W["two watched literals<br/>+ blocking literal"]
  W --> U{"conflict?"}
  U -->|no| D["VSIDS decision<br/>+ saved phase"]
  D --> W
  U -->|yes| A["1UIP analysis<br/>+ minimization"]
  A --> E["emit to DRAT"]
  A --> B["backjump"]
  B --> W
  A --> R["LBD reduction<br/>emit deletions"]
  U -->|at level 0| X["UNSAT + empty clause"]
  D -->|all assigned| M["SAT + model"]
  X --> T["drat-trim"]
  M --> V["re-read CNF,<br/>check every clause"]
```

## Why position 0 matters

The one invariant in the propagation loop that is easy to break and hard to
notice: when a clause propagates, the implied literal is left at index 0.

`analyze()` walks reason clauses starting from index 1, assuming index 0 holds
the literal that clause implied. Break it and conflict analysis resolves on the
wrong literal, producing learned clauses that do not follow from the formula.
The solver still terminates. It still answers. The answers are wrong.

This is a good advertisement for proof emission: a unit test suite can miss it
for a long time, and drat-trim rejects the very first affected refutation.

## Repository map

```text
include/sat/solver.h      public API, options, statistics
include/sat/proof.h       DRAT emission
include/sat/dimacs.h      reader and independent model checker
src/solver.cpp            arena, watch lists, 1UIP, VSIDS, restarts, reduction
src/proof.cpp             buffered DRAT writer
src/dimacs.cpp            hand-rolled parser, model verification
src/main.cpp              CLI, competition exit codes
tests/test_sat.cpp        unit and differential tests
scripts/gen_cnf.py        random 3-SAT, pigeonhole, parity chains
scripts/check_proofs.py   the verification campaign
bench/ablation.py         component-by-component measurement
bench/benchmark.py        scaling, and the pigeonhole wall
```

## What would come next

- bounded variable elimination and subsumption, which is where most of the
  remaining gap is;
- vivification of learned clauses;
- binary DRAT, and LRAT so the checker does not have to search;
- an incremental interface with assumptions;
- chronological backtracking and target phases.

Each is a specific measured gap rather than a feature list.

## License

MIT. `drat-trim` is fetched from its upstream repository at build time and is
not included here.
