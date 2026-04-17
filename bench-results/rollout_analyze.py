#!/usr/bin/env python3
"""Analyze rollout survey CSV to test the "fast mixing" hypothesis.

Hypothesis: once the solver survives ~50-100 moves from any board B, its
subsequent per-move hazard is approximately constant (= baseline steady-state
hazard ~2e-5/move), regardless of B. If true, P(survive next K moves | B) is
a sufficient statistic for per-board "danger" at short K, and we can tune
weights using short rollouts instead of full games.

Refutation: find boards whose CONDITIONAL hazard rate after 100 moves is
significantly different from the population mean conditional hazard.

Diagnostics emitted:
  1. Global per-bucket hazard rate (aggregated over all boards).
  2. Per-board conditional hazard rate in [100, 1500] (steady-state window).
  3. Chi-squared test of per-board variation in the post-100 regime.
"""

import argparse
import csv
import math
import sys
from pathlib import Path

THRESHOLDS = [10, 25, 50, 100, 200, 500, 1000, 1500]


def load(path):
    rows = []
    with open(path) as fp:
        for row in csv.DictReader(fp):
            rows.append({
                "game_idx": int(row["game_idx"]),
                "move_idx": int(row["move_idx"]),
                "n": int(row["n_rollouts"]),
                "alive": [int(row[f"s{t}"]) for t in THRESHOLDS],
            })
    return rows


def bucket_hazard(alive_a, alive_b, thr_a, thr_b):
    """Per-move hazard rate in bucket [thr_a, thr_b].

    Returns (lambda, entered, died).  lambda = died / entered / (thr_b - thr_a).
    """
    entered = alive_a
    died = alive_a - alive_b
    if entered <= 0:
        return float("nan"), 0, 0
    return died / entered / (thr_b - thr_a), entered, died


def main():
    p = argparse.ArgumentParser()
    p.add_argument("corpus", type=Path)
    args = p.parse_args()

    rows = load(args.corpus)
    if not rows:
        sys.exit("no rows in corpus")
    n_boards = len(rows)
    n_rollouts = rows[0]["n"]

    # 1) Global per-bucket hazard (pooled over all boards & rollouts).
    print(f"# {n_boards} boards x {n_rollouts} rollouts each")
    print(f"# === Global per-bucket hazard (pooled) ===")
    print(f"# {'bucket':<14} {'entered':>9} {'died':>6} {'per-move haz':>14}")
    totals_alive = [sum(r["alive"][k] for r in rows) for k in range(len(THRESHOLDS))]
    # s_0 = total rollouts at t=0 (all start alive)
    prev_threshold = 0
    prev_alive = n_rollouts * n_boards
    for k, thr in enumerate(THRESHOLDS):
        ha, en, di = bucket_hazard(prev_alive, totals_alive[k], prev_threshold, thr)
        print(f"# [{prev_threshold:>4},{thr:>4})   {en:>9} {di:>6} {ha:>14.3e}")
        prev_threshold = thr
        prev_alive = totals_alive[k]

    # 2) Per-board conditional hazard in post-mixing regime [100, 1500].
    # We pool the per-board moves 100..1500 (1400 moves of exposure per
    # rollout that survives) and compute lambda_post(B).
    i_100 = THRESHOLDS.index(100)
    i_1500 = THRESHOLDS.index(1500)
    print(f"\n# === Per-board CONDITIONAL hazard in [100, 1500] moves ===")
    print(f"# {'g':>2} {'move':>6}  {'enter':>5} {'died':>4} {'exposure':>10} "
          f"{'lambda':>11} {'se_lambda':>11}  {'mean_life':>10}")
    per_board = []
    for r in rows:
        entered = r["alive"][i_100]
        survived = r["alive"][i_1500]
        died = entered - survived
        # Approximate exposure: died at middle of bucket, survived to full
        # bucket.  Without per-rollout death times, use a middle-of-bucket
        # approximation for died rollouts.
        exposure = died * ((1500 - 100) / 2) + survived * (1500 - 100)
        if entered == 0 or exposure == 0:
            continue
        lam = died / exposure
        # SE(lambda) under exponential ~ lam / sqrt(died); fall back on 1/exp for died=0.
        se_lam = lam / math.sqrt(died) if died > 0 else 1.0 / exposure
        mean_life = 1 / lam if lam > 0 else float("inf")
        print(f"# {r['game_idx']:>2} {r['move_idx']:>6}  "
              f"{entered:>5} {died:>4} {exposure:>10.0f}  "
              f"{lam:>11.3e} {se_lam:>11.3e}  {mean_life:>10.0f}")
        per_board.append((r, lam, se_lam, entered, died, exposure))

    # 3) Is the per-board lambda consistent with a single population lambda?
    # Pooled lambda from all boards' post-100 behavior:
    total_died = sum(b[4] for b in per_board)
    total_exposure = sum(b[5] for b in per_board)
    if total_exposure == 0:
        sys.exit("no exposure data")
    lam_pool = total_died / total_exposure
    print(f"\n# pooled post-100 lambda = {lam_pool:.3e}")
    print(f"# pooled post-100 mean lifetime = {1/lam_pool:.0f} moves")
    print(f"# (compare to 256-game MLE steady-state lambda = 2.07e-05)")

    # Poisson chi-squared test: under null hypothesis (single lambda), each
    # board's died_i ~ Poisson(lam_pool * exposure_i).
    # Chi^2 statistic = sum((died_i - expected_i)^2 / expected_i).
    # With df = n_boards - 1 (we estimated lambda from the data).
    chi2 = 0.0
    for _r, _lam, _se, _entered, died, exposure in per_board:
        expected = lam_pool * exposure
        if expected > 0:
            chi2 += (died - expected) ** 2 / expected
    df = len(per_board) - 1
    print(f"\n# chi^2 = {chi2:.2f}  (df = {df})")
    # Approximate p-value via Wilson-Hilferty for chi-squared w/ large df.
    if df > 0:
        # P(chi^2 > observed) ~= 1 - Phi(z) where
        # z = ((chi2/df)^{1/3} - (1 - 2/(9df))) / sqrt(2/(9df))
        cuberoot_ratio = (chi2 / df) ** (1.0/3.0)
        z = (cuberoot_ratio - (1 - 2/(9*df))) / math.sqrt(2/(9*df))
        # Upper-tail normal CDF via erfc:
        p_upper = 0.5 * math.erfc(z / math.sqrt(2))
        print(f"# approx p(chi^2 >= observed | single-lambda hypothesis) = {p_upper:.3e}")
        if p_upper < 0.01:
            print("# => HYPOTHESIS REFUTED: post-100 hazard varies across boards")
        elif p_upper < 0.05:
            print("# => Marginal evidence against hypothesis")
        else:
            print("# => Cannot refute: post-100 hazard consistent with single lambda")

    # 4) Highlight outlier boards (per-board lambda >> or << pool).
    print(f"\n# === Outlier boards (|lambda / pool - 1| > 1 SE) ===")
    outliers = []
    for r, lam, se_lam, entered, died, exposure in per_board:
        z = (lam - lam_pool) / se_lam if se_lam > 0 else float("nan")
        if abs(z) >= 2:
            outliers.append((z, r, lam, died, entered, exposure))
    outliers.sort(key=lambda x: -abs(x[0]))
    for z, r, lam, died, entered, exposure in outliers[:20]:
        print(f"# z={z:>+5.1f}  g{r['game_idx']} m{r['move_idx']:>6}  "
              f"died={died:>3}/entered={entered:>3}  "
              f"lambda={lam:.3e} ({lam/lam_pool:.2f}x pool)")


if __name__ == "__main__":
    main()
