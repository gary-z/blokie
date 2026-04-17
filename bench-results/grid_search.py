#!/usr/bin/env python3
"""OAT sensitivity grid around EvalWeights::getDefault().

For each of the 12 weights, runs the fitness binary at the baseline, then
at baseline * (1 +/- perturbation). Uses deterministic --seed-base so
all configs face the same game trajectories (common random numbers =
variance reduction for the comparison). Any candidate that looks like an
improvement should be re-confirmed with fresh random seeds before we
trust it, to avoid overfitting to the particular seed set.

Stdout: one line per config with deaths, mean_hat, and ratio to baseline.
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

BINARY = Path(__file__).resolve().parent.parent / "engine/cpp/build-native/fitness"

# Mirror EvalWeights::getDefault() from engine/cpp/solver.cpp.
BASE = [1358, 524, 6540, 4450, 18185, 2665, 204, 908, 1776, 3386, 1607, 3067]
NAMES = [
    "OccupiedSideCube",       #  0
    "SquashedEmpty",          #  1
    "CorneredEmpty",          #  2
    "Transition",             #  3
    "DeadlyPiece",            #  4
    "ThreeBar",               #  5
    "OccupiedCenterCube",     #  6
    "OccupiedCornerCube",     #  7
    "TransitionAligned",      #  8
    "SquashedEmptyAtEdge",    #  9
    "OccupiedCenterSquare",   # 10
    "OccupiedCornerSquare",   # 11
]

SUMMARY_RE = re.compile(
    r"deaths=(\d+)\s+censored=(\d+)\s+exposure=(\d+).*?"
    r"lambda_hat=([\deE.+-]+)\s+mean_hat=([\d.]+)\s+SE\(mean_hat\)=([\d.]+)",
    re.DOTALL,
)


def run(weights, num_games, cap, seed_base):
    cmd = [
        str(BINARY),
        str(num_games),
        "--cap", str(cap),
        "--seed-base", str(seed_base),
        "--weights", ",".join(str(w) for w in weights),
    ]
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    elapsed = time.time() - t0
    m = SUMMARY_RE.search(proc.stderr)
    if not m:
        sys.stderr.write(f"!! could not parse fitness stderr:\n{proc.stderr}\n")
        sys.exit(1)
    return {
        "deaths": int(m.group(1)),
        "censored": int(m.group(2)),
        "exposure": int(m.group(3)),
        "lambda_hat": float(m.group(4)),
        "mean_hat": float(m.group(5)),
        "se_mean_hat": float(m.group(6)),
        "elapsed": elapsed,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--games", type=int, default=128)
    p.add_argument("--cap", type=int, default=25_000)
    p.add_argument("--seed-base", type=int, default=10_000_000)
    p.add_argument("--perturbation", type=float, default=0.30,
                   help="fractional +/- swing per weight")
    args = p.parse_args()

    print(f"# fitness grid: games={args.games} cap={args.cap} "
          f"seed_base={args.seed_base} +/-{args.perturbation:.0%}")
    print(f"# baseline weights: {BASE}")

    b = run(BASE, args.games, args.cap, args.seed_base)
    print(f"{'config':<35} {'D':>4} {'exposure':>10} "
          f"{'mean_hat':>10} {'SE':>7} {'ratio':>7} {'t(s)':>6}")
    print(f"{'baseline':<35} {b['deaths']:>4} {b['exposure']:>10} "
          f"{b['mean_hat']:>10.1f} {b['se_mean_hat']:>7.1f} "
          f"{1.0:>7.4f} {b['elapsed']:>6.1f}")
    sys.stdout.flush()

    for i in range(len(BASE)):
        for sign, mult in (("-", 1 - args.perturbation), ("+", 1 + args.perturbation)):
            w = list(BASE)
            w[i] = max(0, int(round(BASE[i] * mult)))
            r = run(w, args.games, args.cap, args.seed_base)
            ratio = r["mean_hat"] / b["mean_hat"]
            # Rough unpaired log-ratio SE; CRN reduces this in practice.
            se_logratio = (1.0 / r["deaths"] + 1.0 / b["deaths"]) ** 0.5
            flag = " <--" if ratio > 1 + se_logratio else ""
            tag = f"w[{i:>2}]{sign}  {NAMES[i]}"
            print(f"{tag:<35} {r['deaths']:>4} {r['exposure']:>10} "
                  f"{r['mean_hat']:>10.1f} {r['se_mean_hat']:>7.1f} "
                  f"{ratio:>7.4f} {r['elapsed']:>6.1f}{flag}")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
