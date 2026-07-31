"""Every answer, independently verified.

This is the oracle the whole repository is built around, and it is unusually
strong for a from-scratch project. A SAT answer carries a model that can be
checked against the source file in linear time. An UNSAT answer normally carries
nothing at all - "trust me, I searched everywhere" - which is why DRAT exists:
the solver emits a machine-checkable refutation and a third-party program
either signs off or does not.

So there is no self-reported correctness here. Three checks run per instance:

  SAT    the model is re-read from the CNF file and checked clause by clause
  UNSAT  drat-trim verifies the proof, and its verdict is the one reported
  both   the verdict is compared against CaDiCaL, if one is available

The third matters because the first two are one-sided. A solver that answered
UNSAT to everything would emit proofs drat-trim rejects, and one that answered
SAT to everything would fail the model check - but a solver that answered UNSAT
on a satisfiable instance *and failed to produce a valid proof* would look like
a proof bug rather than a soundness bug. The cross-check names it correctly.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "gen_cnf.py")


def generate(path, family, **kw):
    cmd = [sys.executable, GEN, family, "-o", path]
    for k, v in kw.items():
        cmd += [f"--{k}", str(v)]
    subprocess.run(cmd, check=True)


def run_solver(solver, cnf, proof=None, extra=()):
    cmd = [solver, cnf, "--quiet", *extra]
    if proof:
        cmd += ["--proof", proof]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 10:
        return "SAT", r.stdout
    if r.returncode == 20:
        return "UNSAT", r.stdout
    if r.returncode == 0:
        return "UNKNOWN", r.stdout
    return f"ERROR({r.returncode})", r.stdout + r.stderr


def run_cadical(cadical, cnf):
    if not cadical or not os.path.exists(cadical):
        return None
    r = subprocess.run([cadical, "-q", cnf], capture_output=True, text=True)
    if r.returncode == 10:
        return "SAT"
    if r.returncode == 20:
        return "UNSAT"
    return None


def verify_proof(drat, cnf, proof):
    r = subprocess.run([drat, cnf, proof], capture_output=True, text=True,
                       timeout=600)
    return "s VERIFIED" in r.stdout, r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default="./build/satsolve")
    ap.add_argument("--drat", default="./build/drat-trim")
    ap.add_argument("--cadical", default=os.environ.get("CADICAL", ""))
    ap.add_argument("--count", type=int, default=60)
    ap.add_argument("--vars", type=int, default=90)
    args = ap.parse_args()

    have_drat = os.path.exists(args.drat)
    if not have_drat:
        print(f"!! {args.drat} not found; run `make drat-trim` first")
        return 1
    cadical = args.cadical if args.cadical and os.path.exists(args.cadical) else None

    tally = {"SAT": 0, "UNSAT": 0, "other": 0}
    verified = 0
    proof_lines = 0
    disagreements = []
    rejected = []
    started = time.time()

    with tempfile.TemporaryDirectory() as tmp:
        for i in range(args.count):
            cnf = os.path.join(tmp, f"i{i}.cnf")
            proof = os.path.join(tmp, f"i{i}.drat")

            # Straddle the phase transition so roughly half come back UNSAT.
            # Sampling only satisfiable instances would leave the proof path
            # almost untested, which is the path with no other oracle.
            ratio = 3.6 + 1.6 * (i / max(1, args.count - 1))
            generate(cnf, "random", vars=args.vars, ratio=round(ratio, 2), seed=i)

            verdict, out = run_solver(args.solver, cnf, proof)
            if verdict not in ("SAT", "UNSAT"):
                tally["other"] += 1
                rejected.append((i, f"solver said {verdict}"))
                continue
            tally[verdict] += 1

            if verdict == "UNSAT":
                ok, log = verify_proof(args.drat, cnf, proof)
                if ok:
                    verified += 1
                    proof_lines += sum(1 for _ in open(proof))
                else:
                    rejected.append((i, "drat-trim did not verify"))

            if cadical:
                ref = run_cadical(cadical, cnf)
                if ref and ref != verdict:
                    disagreements.append((i, verdict, ref))

    elapsed = time.time() - started
    total = tally["SAT"] + tally["UNSAT"]

    print(f"instances            {args.count} random 3-SAT, {args.vars} vars, "
          f"ratio 3.6-5.2")
    print(f"satisfiable          {tally['SAT']}")
    print(f"unsatisfiable        {tally['UNSAT']}")
    print(f"neither              {tally['other']}")
    print(f"models verified      {tally['SAT']} / {tally['SAT']} "
          f"(re-read from the CNF)")
    print(f"proofs verified      {verified} / {tally['UNSAT']} (drat-trim)")
    print(f"total proof lines    {proof_lines}")
    if cadical:
        print(f"agrees with CaDiCaL  {total - len(disagreements)} / {total}")
    else:
        print("agrees with CaDiCaL  not run (pass --cadical)")
    print(f"wall seconds         {elapsed:.1f}")

    bad = rejected or disagreements
    for i, why in rejected:
        print(f"  !! instance {i}: {why}")
    for i, mine, ref in disagreements:
        print(f"  !! instance {i}: I said {mine}, CaDiCaL said {ref}")

    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
