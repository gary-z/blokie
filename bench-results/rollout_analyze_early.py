#!/usr/bin/env python3
"""Analyze an early-game rollout survey.

Extends rollout_analyze.py: for each surveyed board, compute the conditional
hazard in [100, 1500] of the rollout. Then bin boards by absolute game-move
position (move_idx, i.e., "how far into the game the board was sampled")
and check whether hazard decays with move_idx. This tests whether the
"bad-start" regime shows up as elevated hazard in rollouts from early
boards.

Also computes:
  - Per-absolute-move-bin pooled hazard, with Poisson SE.
  - Chi-squared test for "single common hazard across all boards".
  - Short-horizon (rollout [0, 200]) vs long-horizon ([200, 1500]) hazard
    per-move-bin: does the early rollout window show elevated hazard
    (board is risky NOW) or does the elevated hazard emerge only later
    (board transitions into risky territory after mixing)?
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


def bin_bucket_hazard(rows, move_bin_lo, move_bin_hi, win_lo, win_hi):
    """Pooled hazard rate over rows whose move_idx is in [move_bin_lo, move_bin_hi)
    computed in the rollout window [win_lo, win_hi].
    """
    win_lo_idx = THRESHOLDS.index(win_lo) if win_lo in THRESHOLDS else None
    win_hi_idx = THRESHOLDS.index(win_hi) if win_hi in THRESHOLDS else None

    entered = 0
    died = 0
    rollouts_start = 0
    for r in rows:
        if not (move_bin_lo <= r["move_idx"] < move_bin_hi):
            continue
        rollouts_start += r["n"]
        if win_lo == 0:
            entered += r["n"]
        else:
            entered += r["alive"][win_lo_idx]
        if win_hi_idx is not None:
            died += (r["alive"][win_lo_idx] if win_lo else r["n"]) - r["alive"][win_hi_idx]
        else:
            died += (r["alive"][win_lo_idx] if win_lo else r["n"])
    duration = win_hi - win_lo
    if entered == 0:
        return (float("nan"), 0, 0, 0)
    hazard = died / entered / duration
    return (hazard, entered, died, rollouts_start)


def chi2_cdf_upper(chi2, df):
    # Wilson-Hilferty approx of upper tail.
    if df <= 0:
        return float("nan")
    cr = (chi2 / df) ** (1.0 / 3.0)
    z = (cr - (1 - 2 / (9 * df))) / math.sqrt(2 / (9 * df))
    return 0.5 * math.erfc(z / math.sqrt(2))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("corpus", type=Path)
    args = p.parse_args()

    rows = load(args.corpus)
    if not rows:
        sys.exit("empty corpus")
    n_rollouts = rows[0]["n"]

    # Split boards into bins by absolute game-move position.
    move_bins = [(0, 1000), (1000, 2000), (2000, 3000), (3000, 4000),
                 (4000, 5000), (5000, 6000), (6000, 7000), (7000, 8000),
                 (8000, 9000), (9000, 10001)]

    print(f"# early rollout survey  file={args.corpus}")
    print(f"# {len(rows)} boards, {n_rollouts} rollouts each")
    print()

    # Hazard by absolute-move bin, in rollout window [100, 1500] (steady-state
    # rollout window, to stay consistent with mid-game analysis).
    print("# === Pooled per-absolute-move-bin hazard in ROLLOUT [100, 1500] ===")
    print(f"# {'bin':<14} {'#boards':>8} {'entered':>9} {'died':>5} "
          f"{'haz':>11} {'se':>11}  {'1/haz':>8}")
    for lo, hi in move_bins:
        n_boards = sum(1 for r in rows if lo <= r["move_idx"] < hi)
        haz, en, di, _ = bin_bucket_hazard(rows, lo, hi, 100, 1500)
        if en > 0 and di > 0:
            se = haz / math.sqrt(di)
            mean_life = 1 / haz
            print(f"# [{lo:>5},{hi:>5})  {n_boards:>8} {en:>9} {di:>5} "
                  f"{haz:>11.3e} {se:>11.3e}  {mean_life:>8.0f}")
        else:
            print(f"# [{lo:>5},{hi:>5})  {n_boards:>8} {en:>9} {di:>5} "
                  f"{'nan':>11}  {'n/a':>11}  {'n/a':>8}")
    print()

    # Short vs long horizon per bin: does early rollout-window hazard differ
    # from late rollout-window hazard?
    print("# === Early [0,200] vs late [200,1500] rollout-window hazard by bin ===")
    print(f"# {'bin':<14} "
          f"{'haz_early':>11} {'haz_late':>11}  {'ratio early/late':>18}")
    for lo, hi in move_bins:
        haz_e, en_e, di_e, _ = bin_bucket_hazard(rows, lo, hi, 0, 200)
        haz_l, en_l, di_l, _ = bin_bucket_hazard(rows, lo, hi, 200, 1500)
        if di_e and di_l:
            ratio = haz_e / haz_l
            print(f"# [{lo:>5},{hi:>5})  {haz_e:>11.3e} {haz_l:>11.3e}  {ratio:>18.2f}")
        else:
            print(f"# [{lo:>5},{hi:>5})  {haz_e:>11.3e} {haz_l:>11.3e}  "
                  f"{'n/a':>18}")
    print()

    # Chi-squared test: all boards share a single [100, 1500] hazard?
    i100 = THRESHOLDS.index(100)
    i1500 = THRESHOLDS.index(1500)
    per_board = []
    total_died = 0
    total_exposure = 0.0
    for r in rows:
        entered = r["alive"][i100]
        survived = r["alive"][i1500]
        died = entered - survived
        exposure = died * ((1500 - 100) / 2) + survived * (1500 - 100)
        if exposure <= 0:
            continue
        per_board.append((r, died, exposure))
        total_died += died
        total_exposure += exposure
    lam_pool = total_died / total_exposure if total_exposure > 0 else float("nan")
    chi2 = 0.0
    for _r, died, exposure in per_board:
        expected = lam_pool * exposure
        if expected > 0:
            chi2 += (died - expected) ** 2 / expected
    df = len(per_board) - 1
    p_upper = chi2_cdf_upper(chi2, df)
    print(f"# === Chi-squared: single-lambda across ALL early-survey boards ===")
    print(f"# pooled lambda = {lam_pool:.3e}  (1/lambda = {1/lam_pool:.0f} moves)")
    print(f"# chi^2 = {chi2:.2f}  df = {df}  p(>= observed | single lambda) = {p_upper:.3e}")
    if p_upper < 0.01:
        print("# => HYPOTHESIS REFUTED for early boards")
    elif p_upper < 0.05:
        print("# => Marginal evidence against single-lambda")
    else:
        print("# => Cannot refute single-lambda hypothesis")
    print()

    # Test: does pooled hazard decrease monotonically with move bin?
    # Compute Spearman rank correlation between move_idx and per-board
    # conditional hazard.
    move_idx_hazard = []
    for r, died, exposure in per_board:
        lam = died / exposure if exposure > 0 else 0.0
        move_idx_hazard.append((r["move_idx"], lam))
    move_idx_hazard.sort()
    n = len(move_idx_hazard)
    # Simple Spearman: rank x, rank y, Pearson between ranks.
    xs = [x for x, _ in move_idx_hazard]
    ys = [y for _, y in move_idx_hazard]
    def rank(values):
        indexed = sorted(enumerate(values), key=lambda t: t[1])
        r = [0.0] * len(values)
        i = 0
        while i < len(indexed):
            j = i
            while j + 1 < len(indexed) and indexed[j + 1][1] == indexed[i][1]:
                j += 1
            avg_rank = (i + j) / 2.0 + 1
            for k in range(i, j + 1):
                r[indexed[k][0]] = avg_rank
            i = j + 1
        return r
    rx = rank(xs)
    ry = rank(ys)
    mean_rx = sum(rx) / n
    mean_ry = sum(ry) / n
    num = sum((a - mean_rx) * (b - mean_ry) for a, b in zip(rx, ry))
    da = math.sqrt(sum((a - mean_rx) ** 2 for a in rx))
    db = math.sqrt(sum((b - mean_ry) ** 2 for b in ry))
    rho = num / (da * db) if da > 0 and db > 0 else float("nan")
    print(f"# Spearman rho(move_idx, per-board hazard) = {rho:+.3f}  (n={n})")
    print(f"# (Negative rho means hazard decreases as games get later;")
    print(f"#  a few SEs away from 0 would support the bad-start hypothesis.)")


if __name__ == "__main__":
    main()
