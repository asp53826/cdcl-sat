"""What each part of CDCL is actually worth.

A comparison against a state-of-the-art solver tells you that you are slower. It
does not tell you which of the ideas in the literature are carrying the result,
and that is the more interesting question for something built from scratch.

So: the same instances, the same seed, with one component switched off at a
time. Everything here is measured in *conflicts* as well as seconds, because
seconds confound "made better decisions" with "ran the inner loop faster", and
the two components below have opposite signs on those two metrics.
"""

from __future__ import annotations

import argparse
import os
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(os.path.dirname(HERE), "scripts", "gen_cnf.py")

CONFIGS = [
    ("full", []),
    ("no clause minimization", ["--no-minimize"]),
    ("no phase saving", ["--no-phase-saving"]),
    ("restart every 20 conflicts", ["--restart-base", "20"]),
    ("restart every 50000 conflicts", ["--restart-base", "50000"]),
    ("no VSIDS decay (var-decay 1.0)", ["--var-decay", "1.0"]),
]


def generate(path, seed, n_vars, ratio):
    subprocess.run([sys.executable, GEN, "random", "-o", path,
                    "--vars", str(n_vars), "--ratio", str(ratio),
                    "--seed", str(seed)], check=True)


def parse_stats(out):
    stats = {}
    for line in out.splitlines():
        if line.startswith("c "):
            parts = line[2:].rsplit(None, 1)
            if len(parts) == 2:
                try:
                    stats[parts[0].strip()] = float(parts[1])
                except ValueError:
                    pass
    return stats


def run(solver, cnf, extra, timeout):
    started = time.time()
    try:
        r = subprocess.run([solver, cnf, *extra], capture_output=True,
                           text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, timeout
    elapsed = time.time() - started
    if r.returncode not in (10, 20):
        return None, elapsed
    return parse_stats(r.stdout), elapsed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default="./build/satsolve")
    ap.add_argument("--instances", type=int, default=40)
    ap.add_argument("--vars", type=int, default=150)
    ap.add_argument("--ratio", type=float, default=4.26)
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for i in range(args.instances):
            p = os.path.join(tmp, f"a{i}.cnf")
            generate(p, i, args.vars, args.ratio)
            files.append(p)

        print(f"random 3-SAT, {args.vars} vars, ratio {args.ratio}, "
              f"{args.instances} instances, {args.timeout:.0f}s timeout\n")
        header = (f"{'configuration':<32} {'solved':>7} {'conflicts':>12} "
                  f"{'seconds':>9} {'vs full':>8}")
        print(header)
        print("-" * len(header))

        baseline = None
        for name, extra in CONFIGS:
            conflicts, seconds, solved = [], [], 0
            for cnf in files:
                stats, elapsed = run(args.solver, cnf, extra, args.timeout)
                seconds.append(elapsed)
                if stats is not None:
                    solved += 1
                    conflicts.append(stats.get("conflicts", 0))

            total_time = sum(seconds)
            total_conf = sum(conflicts)
            if baseline is None:
                baseline = total_time
                ratio_txt = "1.00x"
            else:
                ratio_txt = f"{total_time / baseline:.2f}x"
            print(f"{name:<32} {solved:>3}/{len(files):<3} {total_conf:>12,} "
                  f"{total_time:>9.2f} {ratio_txt:>8}")

    print("\nconflicts is the search-quality metric and seconds is the "
          "throughput metric.\nA configuration can win on one and lose on the "
          "other; that is the point of\nreporting both.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
