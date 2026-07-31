# Design

## Why DRAT is the whole point

A SAT solver has an asymmetric verification problem. When it answers
satisfiable it hands back an assignment, and checking that assignment against
the formula is linear and completely convincing. When it answers unsatisfiable
it hands back nothing: the claim is that no assignment exists anywhere in a
space of size 2^n, and the only evidence is that the solver looked.

That asymmetry is why the SAT competition made proof emission mandatory for the
UNSAT track in 2013, and it is why solvers that had been considered correct for
years were found to be wrong once they had to write down their reasoning.

DRAT closes it. Every clause the solver learns is emitted to a file, in the
order it was learned. A checker replays the file and confirms that each clause
follows from the ones above it by reverse unit propagation, and that the last
one is empty. The checker is a different program, written by different people,
that knows nothing about how the solver reached its conclusion.

So the correctness claim in this repository is not "my tests pass". It is
"drat-trim verified 180 of 180 refutations", and drat-trim is not my code.

## Literal encoding

Variable `v` becomes literal `2v` positive and `2v+1` negative. Negation is
`l ^ 1`, and watch lists index directly by literal with no branch. DIMACS is
1-based and signed; that conversion happens in `dimacs.cpp` and `proof.cpp` and
nowhere else, because a solver with two literal conventions in flight is a
solver with a sign bug waiting.

## Clause storage

All clauses live in one contiguous arena of 32-bit words, and a clause reference
is a word offset rather than a pointer. Two reasons:

- watch list entries stay at 8 bytes, so more of the watch list fits in cache,
  and the watch loop is where the time goes;
- the arena can grow without invalidating every reference in the solver, which
  a `vector<Clause*>` cannot.

## The watched literal invariant

Each clause watches two literals. The invariant: a clause is only visited when
one of its watched literals becomes false, and if the clause is not satisfied
and not unit, a new non-false literal can always be found to watch.

Two details that matter more than they look.

**The blocker.** Each watch list entry caches a second literal from the clause.
If that literal is already true, the clause cannot propagate and the arena is
never touched at all. Most visits end here, and this is the single biggest win
in the propagation loop.

**Position 0.** When a clause propagates, the implied literal is left at index 0.
`analyze()` walks reason clauses starting from index 1 on the assumption that
index 0 is the literal that was implied. Break that invariant and conflict
analysis silently resolves on the wrong literal, producing learned clauses that
do not follow - which drat-trim then rejects, which is a good illustration of
why the proof is worth having.

## Conflict analysis

First-UIP: walk the trail backwards from the conflict, resolving each reason
clause into the conflict clause, and stop as soon as exactly one literal from
the current decision level remains. That literal, negated, becomes the asserting
literal, and the clause is guaranteed to propagate immediately after backjumping.

Then minimization. A literal in the learned clause can be dropped if it is
implied by the others already there; the check is a depth-first walk over
reasons, filtered by an abstraction of the decision levels present in the clause
so that most candidates are rejected without touching the arena. Shorter clauses
propagate more often and cost less to keep, and the effect is measured in
`bench/ablation.py` rather than assumed.

## Heuristics, and what they are worth

Every one of these is allowed to change the search and forbidden to change the
answer, which is exactly what `test_all_configurations_agree` checks.

**VSIDS.** Each variable carries an activity score, bumped when it appears in a
conflict and decayed globally by dividing the increment rather than scaling
every score. Variables sit in a binary max-heap. This is the component with the
clearest measured effect.

**Phase saving.** When a variable is unassigned during backtracking, its value
is remembered and reused when it is next decided. The effect is to keep the
solver in the region it was working in rather than restarting the same subproblem
from scratch after every restart.

**Luby restarts.** Restart intervals follow 1,1,2,1,1,2,4,... scaled by a base.
The Luby sequence is optimal for restart strategies against an unknown runtime
distribution, and unlike a fixed interval it is not tuned to one instance family.

**LBD-based reduction.** Learned clauses are scored by literal block distance -
the number of distinct decision levels in the clause - and the worst half are
deleted periodically. Clauses with LBD 2 are never deleted; this is the one
heuristic every solver since Glucose agrees on. Clauses currently serving as a
reason for an assignment on the trail are also never deleted, because the trail
would lose the justification for an assignment it still holds.

## Deletions in the proof

Every clause deletion is written to the proof as a `d` line. This is not
required for soundness - a checker that ignored deletions would still verify a
correct proof - but backward checking gets dramatically faster when it knows a
clause was gone, because it does not have to consider it as a candidate for
propagation. On the pigeonhole instances the difference is the difference
between verifying and not.

Deletion itself is always sound in DRAT: removing a clause can only weaken the
formula, so no justification is needed.

## Root-level simplification

At decision level 0 any clause containing a literal already true is permanently
satisfied and can be dropped. It is detached, marked free, and a deletion line
goes into the proof.

Clauses are not *strengthened* by removing literals already false at root, even
though that is a legal and useful simplification, because the strengthened
clause would need emitting as an addition and the original as a deletion, and
getting the order wrong produces a proof that fails to check. The optimization
is worth less than the risk.

## What this is not

**No inprocessing.** No variable elimination, no subsumption, no vivification,
no probing, no symmetry breaking. These are most of the distance between this
solver and CaDiCaL, and their absence is why the gap widens with instance size
rather than staying a constant factor.

**No chronological backtracking**, no rephasing, no target phases, no stable/
unstable mode switching. This is roughly MiniSat 2.2 plus LBD, which is a
deliberate choice: it is the architecture whose components can each be explained
and measured.

**Not incremental.** No assumptions, no `solve(under assumptions)`, no IPASIR
interface. Every solve is from scratch.

**No parallelism.** One thread.

## Where the approach loses

Pigeonhole. PHP(n) - n+1 pigeons into n holes - is unsatisfiable, and Haken
proved in 1985 that every resolution refutation of it is exponentially long. CDCL
learns clauses by resolution, so *no* amount of engineering makes this family
tractable; CaDiCaL is faster on it than this solver, and also loses.

This is included in the benchmark on purpose. A benchmark set on which the
approach always wins is not measuring the approach, it is advertising it. The
useful number is where the wall is, and the useful observation is that it is a
property of resolution rather than of the implementation.
