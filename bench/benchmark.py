"""Scaling, and the distance to a real solver.

Two comparisons, and they answer different questions.

Against CaDiCaL: how far off is a from-scratch MiniSat-architecture solver from
one that has had twenty years of work put into it? The answer is not close, and
the ratio growing with instance size is the interesting part - it says the gap
is inprocessing and heuristics, not constant factors.

Pigeonhole: where the solver loses outright. PHP(n) has no polynomial-length
resolution proof, so every CDCL solver hits a wall; the only question is where.
Including a family the approach cannot solve is the difference between a
benchmark and an advertisement.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(os.path.dirname(HERE), "scripts", "gen_cnf.py")


def generate(path, family, **kw):
    cmd = [sys.executable, GEN, family, "-o", path]
    for k, v in kw.items():
        cmd += [f"--{k}", str(v)]
    subprocess.run(cmd, check=True)


def conflicts_in(out):
    for line in out.splitlines():
        if line.startswith("c conflicts"):
            return int(float(line.split()[-1]))
    return None


def time_run(cmd, timeout):
    """One run, timed, with the statistics read out of the same run.

    An earlier version solved each instance twice - once for the wall clock and
    once to read the conflict count off stdout - which doubled the cost of the
    whole benchmark and, worse, reported timings from a run whose statistics
    came from a different process.
    """
    started = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, timeout, ""
    return r.returncode, time.time() - started, r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default="./build/satsolve")
    ap.add_argument("--cadical", default=os.environ.get("CADICAL", ""))
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--reps", type=int, default=8)
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    cadical = args.cadical if args.cadical and os.path.exists(args.cadical) else None
    sizes = [100, 150, 200] if args.quick else [100, 150, 200, 250, 300]
    holes = [6, 7, 8] if args.quick else [6, 7, 8, 9, 10]
    reps = 3 if args.quick else args.reps

    with tempfile.TemporaryDirectory() as tmp:
        print(f"random 3-SAT at ratio 4.26, {reps} instances per size, "
              f"{args.timeout:.0f}s timeout\n")
        header = (f"{'vars':>6} {'solved':>8} {'mine (s)':>10} "
                  f"{'conflicts':>12} {'cadical (s)':>12} {'ratio':>8}")
        print(header)
        print("-" * len(header))

        for n in sizes:
            mine = cad = 0.0
            solved = 0
            conflicts = 0
            for i in range(reps):
                cnf = os.path.join(tmp, f"r{n}_{i}.cnf")
                generate(cnf, "random", vars=n, ratio=4.26, seed=1000 + i)

                code, t, out = time_run([args.solver, cnf, "--no-verify"],
                                        args.timeout)
                mine += t
                if code in (10, 20):
                    solved += 1
                    c = conflicts_in(out)
                    if c:
                        conflicts += c
                if cadical:
                    _, tc, _ = time_run([cadical, "-q", cnf], args.timeout)
                    cad += tc

            ratio = f"{mine / cad:.1f}x" if cadical and cad > 0 else "-"
            cad_txt = f"{cad:>12.2f}" if cadical else f"{'-':>12}"
            print(f"{n:>6} {solved:>4}/{reps:<3} {mine:>10.2f} "
                  f"{conflicts:>12,}{cad_txt} {ratio:>8}")

        print(f"\npigeonhole PHP(n): n+1 pigeons into n holes, unsatisfiable, "
              f"no short resolution proof exists\n")
        header = (f"{'holes':>6} {'vars':>7} {'clauses':>9} {'mine (s)':>10} "
                  f"{'conflicts':>12} {'cadical (s)':>12}")
        print(header)
        print("-" * len(header))

        for h in holes:
            cnf = os.path.join(tmp, f"php{h}.cnf")
            generate(cnf, "php", holes=h)
            with open(cnf) as fh:
                for line in fh:
                    if line.startswith("p cnf"):
                        _, _, nv, nc = line.split()
                        break

            code, t, out = time_run([args.solver, cnf], args.timeout)
            c = conflicts_in(out) if code in (10, 20) else None
            mine_txt = f"{t:>10.2f}" if code in (10, 20) else f"{'timeout':>10}"
            conf_txt = f"{c:>12,}" if c else f"{'-':>12}"

            if cadical:
                _, tc, _ = time_run([cadical, "-q", cnf], args.timeout)
                cad_txt = f"{tc:>12.2f}"
            else:
                cad_txt = f"{'-':>12}"
            print(f"{h:>6} {nv:>7} {nc:>9} {mine_txt} {conf_txt}{cad_txt}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
