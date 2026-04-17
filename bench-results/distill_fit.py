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


def solve_ridge(A, b, ridge_lambda=0.0, prior=None):
    """Solve argmin ||A w - b||^2 + ridge_lambda * ||w - prior||^2.

    Equivalent to (A^T A + lambda I) w = A^T b + lambda * prior.
    prior defaults to zero if not given.
    """
    n = N_TUNABLE
    prior = prior if prior is not None else [0.0] * n
    ATA = [[0.0] * n for _ in range(n)]
    ATb = [0.0] * n
    for row, y in zip(A, b):
        for i in range(n):
            bi = row[i]
            for j in range(n):
                ATA[i][j] += bi * row[j]
            ATb[i] += bi * y
    # Ridge penalty.
    if ridge_lambda > 0.0:
        for i in range(n):
            ATA[i][i] += ridge_lambda
            ATb[i] += ridge_lambda * prior[i]
    # Gauss-Jordan on augmented matrix [ATA | ATb].
    M = [ATA[i] + [ATb[i]] for i in range(n)]
    for col in range(n):
        pivot_row = max(range(col, n), key=lambda r: abs(M[r][col]))
        if abs(M[pivot_row][col]) < 1e-12:
            raise ValueError(f"singular at column {col}")
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
    p.add_argument("--lambdas", default="0,1,10,100,1000,10000",
                   help="comma-separated ridge lambdas. Ridge penalty is "
                        "lambda * ||w - BASE_WEIGHTS||^2, pulling the fit "
                        "toward the current hand-tuned weights.")
    args = p.parse_args()

    feats, f_ss, teacher, current = load_corpus(args.corpus)
    n = len(teacher)
    if n < N_TUNABLE * 2:
        sys.exit(f"corpus too small: {n} rows")

    # Target: teacher minus the fixed SideSquare contribution.
    adj_target = [t - SIDE_SQUARE_COEF * fss for t, fss in zip(teacher, f_ss)]

    def rmse(preds):
        s = sum((p - t) ** 2 for p, t in zip(preds, teacher)) / len(teacher)
        return math.sqrt(s)

    import statistics as st
    def corr(a, b):
        ma, mb = st.mean(a), st.mean(b)
        num = sum((ai - ma) * (bi - mb) for ai, bi in zip(a, b))
        da = math.sqrt(sum((ai - ma) ** 2 for ai in a))
        db = math.sqrt(sum((bi - mb) ** 2 for bi in b))
        return num / (da * db) if da > 0 and db > 0 else float("nan")

    # Baseline: predict teacher using current weights as-is.
    r2_baseline = r_squared(current, teacher)
    rmse_baseline = rmse(current)
    corr_baseline = corr(current, teacher)
    print(f"# distillation fit  rows={n}  file={args.corpus}")
    print(f"# baseline:  R^2={r2_baseline:.4f}  RMSE={rmse_baseline:.1f}  "
          f"corr={corr_baseline:.4f}")
    print()

    lambdas = [float(x) for x in args.lambdas.split(",")]
    fitted = {}
    for lam in lambdas:
        prior = [float(bw) for bw in BASE_WEIGHTS] if lam > 0 else None
        try:
            w = solve_ridge(feats, adj_target, ridge_lambda=lam, prior=prior)
        except ValueError as e:
            print(f"# lambda={lam}: {e}")
            continue
        preds = [
            sum(wk * fk for wk, fk in zip(w, row)) + SIDE_SQUARE_COEF * fss
            for row, fss in zip(feats, f_ss)
        ]
        fitted[lam] = (w, rmse(preds), corr(preds, teacher), r_squared(preds, teacher))

    # Summary table of the regularization path.
    print(f"# {'lambda':>10}  {'R^2':>6}  {'RMSE':>10}  {'corr':>6}  "
          f"{'#neg':>4}  {'#<50%':>6}")
    for lam, (w, rm, co, r2) in fitted.items():
        negs = sum(1 for wi in w if wi < 0)
        low = sum(1 for wi, b_ in zip(w, BASE_WEIGHTS)
                  if b_ > 0 and wi / b_ < 0.5)
        print(f"# {lam:>10.3g}  {r2:.4f}  {rm:>10.1f}  {co:.4f}  "
              f"{negs:>4}  {low:>6}")
    print()

    # Weight path.
    print("# weight path (delta% from baseline)")
    header = "# {:>2} {:<25} {:>10}".format("k", "name", "base")
    for lam in fitted:
        header += "  {:>7}".format(f"l={lam:g}")
    print(header)
    for k in range(N_TUNABLE):
        row = "# {:>2} {:<25} {:>10.0f}".format(k, NAMES[k], BASE_WEIGHTS[k])
        for lam, (w, *_rest) in fitted.items():
            b_ = BASE_WEIGHTS[k]
            if b_:
                pct = 100.0 * (w[k] - b_) / b_
                row += "  {:>+6.1f}%".format(pct)
            else:
                row += "  {:>7}".format("n/a")
        print(row)

    # Emit candidate --weights strings for each lambda.
    print()
    for lam, (w, *_rest) in fitted.items():
        rounded = [max(0, int(round(wk))) for wk in w]
        print("# lambda={:g}  --weights '{}'".format(
            lam, ",".join(str(v) for v in rounded)))


if __name__ == "__main__":
    main()


if __name__ == "__main__":
    main()
