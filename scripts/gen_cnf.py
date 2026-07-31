"""CNF generators, so the benchmark set is reproducible from a seed rather than
a download.

Three families, chosen because they fail in different ways:

  random 3-SAT   the standard scaling benchmark. At ratio 4.26 half the
                 instances are satisfiable and both answers are hard, which is
                 what makes it a benchmark rather than a demo.
  pigeonhole     provably requires exponentially many resolution steps, so any
                 CDCL solver dies on small n. This is where the solver loses,
                 and the point of including it is to show by how much.
  parity / xor   long chains of xor constraints. Unit propagation learns almost
                 nothing from them, so they are hard out of proportion to size.
"""

from __future__ import annotations

import argparse
import itertools
import random


def header(clauses, n_vars):
    return [f"p cnf {n_vars} {len(clauses)}"]


def render(clauses, n_vars, comment=""):
    out = []
    if comment:
        out.append(f"c {comment}")
    out += header(clauses, n_vars)
    out += [" ".join(str(l) for l in c) + " 0" for c in clauses]
    return "\n".join(out) + "\n"


def random_ksat(n_vars, ratio, k=3, seed=0):
    rng = random.Random(seed)
    m = int(round(n_vars * ratio))
    clauses = []
    for _ in range(m):
        vs = rng.sample(range(1, n_vars + 1), k)
        clauses.append([v if rng.random() < 0.5 else -v for v in vs])
    return clauses, n_vars


def pigeonhole(holes, seed=0):
    """holes+1 pigeons into holes holes. Unsatisfiable, and famously so."""
    pigeons = holes + 1

    def x(p, h):
        return p * holes + h + 1

    clauses = []
    for p in range(pigeons):                       # every pigeon gets a hole
        clauses.append([x(p, h) for h in range(holes)])
    for h in range(holes):                         # no hole takes two pigeons
        for p, q in itertools.combinations(range(pigeons), 2):
            clauses.append([-x(p, h), -x(q, h)])
    return clauses, pigeons * holes


def parity_chain(n_vars, seed=0):
    """A chain of 3-variable xor constraints with an inconsistent parity.

    Each xor expands to four clauses. The result is unsatisfiable and unit
    propagation cannot see it, because no single clause is ever unit until
    almost everything is assigned.
    """
    rng = random.Random(seed)
    clauses = []
    for i in range(1, n_vars - 1):
        a, b, c = i, i + 1, i + 2
        parity = 1 if rng.random() < 0.5 else 0
        for signs in itertools.product([1, -1], repeat=3):
            negatives = sum(1 for s in signs if s < 0)
            if (negatives % 2) == parity:
                clauses.append([signs[0] * a, signs[1] * b, signs[2] * c])
    # force a contradiction on the ends
    clauses.append([1])
    clauses.append([-1])
    return clauses, n_vars


FAMILIES = {
    "random": lambda a: random_ksat(a.vars, a.ratio, a.k, a.seed),
    "php": lambda a: pigeonhole(a.holes, a.seed),
    "parity": lambda a: parity_chain(a.vars, a.seed),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("family", choices=sorted(FAMILIES))
    ap.add_argument("--vars", type=int, default=100)
    ap.add_argument("--ratio", type=float, default=4.26)
    ap.add_argument("--k", type=int, default=3)
    ap.add_argument("--holes", type=int, default=8)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("-o", "--out", default="")
    args = ap.parse_args()

    clauses, n = FAMILIES[args.family](args)
    text = render(clauses, n, f"{args.family} seed={args.seed}")
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
