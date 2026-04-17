#!/usr/bin/env python3
"""Linear regression on a distillation corpus.

Input: a CSV produced by `fitness --distill-out`, each row:
    f0,f1,...,f11,f_sideSquare,teacher,current_eval,game_idx,move_idx

The current eval is linear in the 12 tunable weights plus one hardcoded
coefficient (SIDE_SQUARE_COEF = 2000 applied to f_sideSquare). We fit:

    teacher_i  ~=  sum_{k=0..11} f_ki * w_k  +  SIDE_SQUARE_COEF * f_sideSquare_i

by least squares, reporting the fitted w vector plus basic fit diagnostics
(R^2 on teacher, baseline R^2 of current eval, pre/post residual stddev).

No numpy: this is 12-dim normal equations solved via Gaussian elimination.
"""

import argparse
import csv
import math
import sys
from pathlib import Path

SIDE_SQUARE_COEF = 2000
N_TUNABLE = 12
BASE_WEIGHTS = [1358, 524, 6540, 4450, 18185, 2665, 204, 908, 1776, 3386, 1607, 3067]
NAMES = [
    "OccupiedSideCube", "SquashedEmpty", "CorneredEmpty", "Transition",
    "DeadlyPiece", "ThreeBar", "OccupiedCenterCube", "OccupiedCornerCube",
    "TransitionAligned", "SquashedEmptyAtEdge", "OccupiedCenterSquare",
    "OccupiedCornerSquare",
]


def solve_normal_equations(A, b):
    """Solve (A^T A) w = A^T b for w.  A is rows x N_TUNABLE; b is rows.
    Returns the N_TUNABLE-vector w.
    """
    n = N_TUNABLE
    # Build ATA (n x n) and ATb (n).
    ATA = [[0.0] * n for _ in range(n)]
    ATb = [0.0] * n
    for row, y in zip(A, b):
        for i in range(n):
            bi = row[i]
            for j in range(n):
                ATA[i][j] += bi * row[j]
            ATb[i] += bi * y
    # Gaussian elimination with partial pivoting on augmented matrix.
    M = [ATA[i] + [ATb[i]] for i in range(n)]
    for col in range(n):
        # Partial pivot.
        pivot_row = max(range(col, n), key=lambda r: abs(M[r][col]))
        if abs(M[pivot_row][col]) < 1e-12:
            raise ValueError(f"singular at column {col} (features may be collinear)")
        M[col], M[pivot_row] = M[pivot_row], M[col]
        inv = 1.0 / M[col][col]
        for j in range(col, n + 1):
            M[col][j] *= inv
        for r in range(n):
            if r == col:
                continue
            factor = M[r][col]
            if factor == 0.0:
                continue
            for j in range(col, n + 1):
                M[r][j] -= factor * M[col][j]
    return [M[i][n] for i in range(n)]


def r_squared(preds, targets):
    n = len(targets)
    mean = sum(targets) / n
    ss_tot = sum((t - mean) ** 2 for t in targets)
    ss_res = sum((t - p) ** 2 for t, p in zip(targets, preds))
    return 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")


def load_corpus(path):
    feats, f_ss, teacher, current = [], [], [], []
    with open(path) as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            feats.append([float(row[f"f{k}"]) for k in range(N_TUNABLE)])
            f_ss.append(float(row["f_sideSquare"]))
            teacher.append(float(row["teacher"]))
            current.append(float(row["current_eval"]))
    return feats, f_ss, teacher, current


def main():
    p = argparse.ArgumentParser()
    p.add_argument("corpus", type=Path)
    args = p.parse_args()

    feats, f_ss, teacher, current = load_corpus(args.corpus)
    n = len(teacher)
    if n < N_TUNABLE * 2:
        sys.exit(f"corpus too small: {n} rows")

    # Target: teacher minus the fixed SideSquare contribution.
    adj_target = [t - SIDE_SQUARE_COEF * fss for t, fss in zip(teacher, f_ss)]

    # Fit.
    w = solve_normal_equations(feats, adj_target)

    # Diagnostics.
    preds_distilled_full = [
        sum(wk * fk for wk, fk in zip(w, row)) + SIDE_SQUARE_COEF * fss
        for row, fss in zip(feats, f_ss)
    ]
    r2_distilled = r_squared(preds_distilled_full, teacher)
    r2_baseline = r_squared(current, teacher)

    # Residual stddev relative to teacher.
    def rmse(preds):
        s = sum((p - t) ** 2 for p, t in zip(preds, teacher)) / len(teacher)
        return math.sqrt(s)

    print(f"# distillation fit  rows={n}  file={args.corpus}")
    print(f"# R^2 vs teacher   baseline(current eval) = {r2_baseline:.4f}")
    print(f"# R^2 vs teacher   distilled              = {r2_distilled:.4f}")
    print(f"# RMSE vs teacher  baseline               = {rmse(current):.1f}")
    print(f"# RMSE vs teacher  distilled              = {rmse(preds_distilled_full):.1f}")

    # Also report a correlation-based diagnostic: does the distilled eval
    # RANK boards the same way the teacher does?  (Ranking is what the game
    # policy actually cares about.)
    import statistics as st
    def corr(a, b):
        ma, mb = st.mean(a), st.mean(b)
        num = sum((ai - ma) * (bi - mb) for ai, bi in zip(a, b))
        da = math.sqrt(sum((ai - ma) ** 2 for ai in a))
        db = math.sqrt(sum((bi - mb) ** 2 for bi in b))
        return num / (da * db) if da > 0 and db > 0 else float("nan")
    print(f"# corr(current, teacher)   = {corr(current, teacher):.4f}")
    print(f"# corr(distilled, teacher) = {corr(preds_distilled_full, teacher):.4f}")

    print()
    print("# weight-by-weight comparison (baseline -> distilled):")
    print(f"# {'k':>2} {'name':<25} {'baseline':>10} {'distilled':>10} {'delta%':>8}")
    for k in range(N_TUNABLE):
        d = w[k]
        b_ = BASE_WEIGHTS[k]
        pct = 100.0 * (d - b_) / b_ if b_ != 0 else float("nan")
        print(f"# {k:>2} {NAMES[k]:<25} {b_:>10.0f} {d:>10.1f} {pct:>+7.1f}%")

    # Emit new weights as comma-separated ints for --weights.
    rounded = [max(0, int(round(wk))) for wk in w]
    print()
    print("# pass to fitness as:  --weights '{}'".format(",".join(str(v) for v in rounded)))


if __name__ == "__main__":
    main()
