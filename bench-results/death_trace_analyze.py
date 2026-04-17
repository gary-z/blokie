#!/usr/bin/env python3
"""Analyze death-trace CSV to see what game death looks like.

Specifically testing: "we are at steady state, then suddenly 2-3 piece sets
can't be cleared, eval spikes, and we never recover."

Produces:
  1. Per-game summary (moves, died vs capped, final eval).
  2. For games that DIED naturally, eval trajectory across the last 500
     moves. Does eval spike abruptly or drift up?
  3. Clearing pattern across the last 500 moves: how many moves before
     death did clearing stop?
  4. Distribution of "run of consecutive no-clear moves" across death
     games and capped games.
"""

import argparse
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path


def load(path):
    # Organize rows by game_idx, ordered by move_idx.
    games = defaultdict(list)
    meta = {}
    with open(path) as fp:
        for row in csv.DictReader(fp):
            gi = int(row["game_idx"])
            games[gi].append({
                "move_idx": int(row["move_idx"]),
                "pieces_placed": int(row["pieces_placed"]),
                "squares_cleared": int(row["squares_cleared"]),
                "eval_after": int(row["eval_after"]),
                "board_count_after": int(row["board_count_after"]),
            })
            meta[gi] = {
                "seed": int(row["seed"]),
                "died": int(row["died"]) == 1,
                "total_moves": int(row["total_moves"]),
            }
    for gi in games:
        games[gi].sort(key=lambda r: r["move_idx"])
    return games, meta


def main():
    p = argparse.ArgumentParser()
    p.add_argument("corpus", type=Path)
    p.add_argument("--lookback", type=int, default=500,
                   help="moves before death to include in per-death trajectory")
    args = p.parse_args()

    games, meta = load(args.corpus)
    n = len(games)
    died = [gi for gi, m in meta.items() if m["died"]]
    capped = [gi for gi, m in meta.items() if not m["died"]]

    print(f"# death-trace analysis  file={args.corpus}")
    print(f"# {n} games: {len(died)} died, {len(capped)} capped")
    print(f"# mean(total_moves) = {sum(m['total_moves'] for m in meta.values()) / n:.0f}")

    # 1) Per-game summary
    print("\n# === Per-game summary (died vs capped, total moves, final eval) ===")
    print(f"# {'g':>3} {'fate':>7} {'moves':>7} {'final_eval':>12} "
          f"{'eval@-50':>9} {'eval@-200':>10} {'cleared_last_50':>16}")
    for gi in sorted(games):
        rows = games[gi]
        total = len(rows)
        last = rows[-1]
        # Row near -50 and -200 from end. eval_after is UINT64_MAX at death; use
        # the previous finite eval.
        last_finite = None
        for r in reversed(rows):
            if r["eval_after"] < (1 << 60):
                last_finite = r
                break
        e_m50 = rows[-51]["eval_after"] if total >= 51 else rows[0]["eval_after"]
        e_m200 = rows[-201]["eval_after"] if total >= 201 else rows[0]["eval_after"]
        cleared_50 = sum(r["squares_cleared"] for r in rows[-50:] if r["squares_cleared"] > 0)
        fate = "DIED" if meta[gi]["died"] else "capped"
        final = last_finite["eval_after"] if last_finite else 0
        print(f"# {gi:>3} {fate:>7} {total:>7} {final:>12} {e_m50:>9} "
              f"{e_m200:>10} {cleared_50:>16}")

    # 2) Eval trajectory over the last `lookback` moves of death games.
    # Align by "moves until death" (0 = death move). Compute per-offset
    # mean + std of eval across all death games.
    print(f"\n# === Eval trajectory in last {args.lookback} moves before death ===")
    print(f"# (offset 0 = move immediately before the death move; larger = further from death)")
    # Collect arrays aligned such that index 0 = last finite-eval move,
    # index 1 = one move earlier, etc.
    trajectories = []
    for gi in died:
        rows = games[gi]
        trail = []
        # Stop at UINT64_MAX eval (the death move itself) and reverse.
        for r in reversed(rows):
            if r["eval_after"] >= (1 << 60):
                continue  # skip the death move itself
            trail.append(r["eval_after"])
            if len(trail) >= args.lookback:
                break
        if len(trail) >= 50:
            trajectories.append(trail)

    if not trajectories:
        print("# (no death games)")
    else:
        # Buckets at a handful of offsets for summary.
        checkpoints = [0, 1, 2, 3, 5, 10, 20, 50, 100, 200, 500]
        print(f"# {'offset':>6}  {'mean_eval':>11}  {'median':>11}  {'stddev':>11}  "
              f"{'n':>4}")
        for ck in checkpoints:
            vals = [t[ck] for t in trajectories if ck < len(t)]
            if len(vals) < 3:
                continue
            m = sum(vals) / len(vals)
            med = sorted(vals)[len(vals) // 2]
            sd = math.sqrt(sum((v - m) ** 2 for v in vals) / len(vals))
            print(f"# {ck:>6}  {m:>11.0f}  {med:>11.0f}  {sd:>11.0f}  {len(vals):>4}")

        # Interpretation: if eval is flat until the last few offsets and then
        # spikes, user's "sudden spike" hypothesis is supported. If eval
        # drifts monotonically up over hundreds of moves, it's a slow drift.

    # 3) "No clear" streak right before death.
    print(f"\n# === Consecutive no-clear streaks just before death ===")
    print(f"# For each death game: count number of final moves that had 0 cleared squares.")
    print(f"# {'g':>3} {'no_clear_run':>14} {'mean_clear_last100':>20}")
    runs = []
    for gi in died:
        rows = games[gi]
        run = 0
        for r in reversed(rows):
            # The death move itself has eval=UINT64_MAX but squares_cleared is
            # still recorded. Skip it too so we count streak ENDING with the
            # move right before death.
            if r["eval_after"] >= (1 << 60):
                continue
            if r["squares_cleared"] == 0:
                run += 1
            else:
                break
        mean_c100 = sum(r["squares_cleared"] for r in rows[-100:]) / min(100, len(rows))
        runs.append(run)
        print(f"# {gi:>3} {run:>14} {mean_c100:>20.2f}")
    if runs:
        mean_run = sum(runs) / len(runs)
        med_run = sorted(runs)[len(runs) // 2]
        print(f"\n# no-clear streak just before death: mean={mean_run:.1f} median={med_run}")

    # 4) Same for capped games (comparison).
    print(f"\n# === Control: no-clear streaks at cap for games that didn't die ===")
    control_runs = []
    for gi in capped:
        rows = games[gi]
        run = 0
        for r in reversed(rows):
            if r["eval_after"] >= (1 << 60):
                continue
            if r["squares_cleared"] == 0:
                run += 1
            else:
                break
        control_runs.append(run)
    if control_runs:
        mean_run = sum(control_runs) / len(control_runs)
        med_run = sorted(control_runs)[len(control_runs) // 2]
        print(f"# capped games:  mean no-clear streak at cap = {mean_run:.1f} median={med_run}")

    # 5) Net-fill trajectory: per offset, mean of pieces_placed - squares_cleared,
    # and mean board_count_after. If the cascade starts at offset ~20, we
    # should see net_fill transition from ~0 (steady) to >0 (growing).
    def net_fill(r):
        return r["pieces_placed"] - r["squares_cleared"]

    checkpoints = list(range(0, 10)) + [12, 15, 20, 25, 30, 40, 50, 100, 200, 500]
    print(f"\n# === Net-fill and board-count trajectory before death ===")
    print(f"# (offset 0 = move just before the death move)")
    print(f"# {'off':>4}  {'mean_net_fill':>15}  {'mean_board_count':>18}  "
          f"{'mean_cleared':>14}  {'mean_pieces':>13}  {'n':>3}")
    # Build a list of (offset -> rows_at_that_offset) across all died games.
    # offset = total_moves - move_idx (offset 0 = final finite-eval move).
    off_net = defaultdict(list)
    off_bc = defaultdict(list)
    off_cl = defaultdict(list)
    off_pp = defaultdict(list)
    for gi in died:
        rows = games[gi]
        # Determine last finite-eval index.
        last_ok = None
        for idx in range(len(rows) - 1, -1, -1):
            if rows[idx]["eval_after"] < (1 << 60):
                last_ok = idx
                break
        if last_ok is None:
            continue
        for off in range(last_ok + 1):
            r = rows[last_ok - off]
            off_net[off].append(net_fill(r))
            off_bc[off].append(r["board_count_after"])
            off_cl[off].append(r["squares_cleared"])
            off_pp[off].append(r["pieces_placed"])

    for ck in checkpoints:
        if ck not in off_net or len(off_net[ck]) < 3:
            continue
        m_nf = sum(off_net[ck]) / len(off_net[ck])
        m_bc = sum(off_bc[ck]) / len(off_bc[ck])
        m_cl = sum(off_cl[ck]) / len(off_cl[ck])
        m_pp = sum(off_pp[ck]) / len(off_pp[ck])
        print(f"# {ck:>4}  {m_nf:>15.2f}  {m_bc:>18.1f}  {m_cl:>14.2f}  "
              f"{m_pp:>13.2f}  {len(off_net[ck]):>3}")

    # 6) For each death game, scan backwards and find the move where net_fill
    # first became "persistently positive" -- a streak of >= 3 moves with
    # net_fill > 0. Call this the "cascade start."  Report statistics.
    print(f"\n# === Cascade onset per game (first streak of 3+ net-fill-positive moves ending at death) ===")
    print(f"# {'g':>3} {'total':>6} {'onset_off':>10} {'last_clear_of_9+':>17}")
    cascade_onsets = []
    for gi in died:
        rows = games[gi]
        last_ok = None
        for idx in range(len(rows) - 1, -1, -1):
            if rows[idx]["eval_after"] < (1 << 60):
                last_ok = idx
                break
        if last_ok is None:
            continue
        # Walk backwards, find the first non-cascade move.
        onset = 0
        for off in range(last_ok + 1):
            r = rows[last_ok - off]
            if net_fill(r) <= 0:
                # Found a non-cascade move. Check it has at least 2 preceding
                # non-cascade moves (to avoid treating single negative-fill
                # moves as breaks).
                break
            onset = off + 1
        # Also find last big-clear (>= 9 squares cleared, ie row/col/cube clear).
        last_big = None
        for off in range(last_ok + 1):
            r = rows[last_ok - off]
            if r["squares_cleared"] >= 9:
                last_big = off
                break
        cascade_onsets.append(onset)
        print(f"# {gi:>3} {meta[gi]['total_moves']:>6} {onset:>10} "
              f"{str(last_big) if last_big is not None else 'n/a':>17}")

    if cascade_onsets:
        mean_on = sum(cascade_onsets) / len(cascade_onsets)
        sorted_on = sorted(cascade_onsets)
        med_on = sorted_on[len(sorted_on) // 2]
        print(f"\n# cascade-onset offset (moves before death): mean={mean_on:.1f} "
              f"median={med_on}  min={min(cascade_onsets)} max={max(cascade_onsets)}")

    # 7) Visualize eval and net_fill of ONE representative game (median-length
    # death), over the last 50 moves, so we can eyeball the trigger.
    if died:
        # Pick the game closest to median total moves.
        died_sorted = sorted(died, key=lambda gi: meta[gi]["total_moves"])
        rep = died_sorted[len(died_sorted) // 2]
        rows = games[rep]
        last_ok = None
        for idx in range(len(rows) - 1, -1, -1):
            if rows[idx]["eval_after"] < (1 << 60):
                last_ok = idx
                break
        print(f"\n# === Per-move log for representative game {rep} "
              f"(moves={meta[rep]['total_moves']}, last 40 moves shown) ===")
        print(f"# {'off':>4} {'m':>6} {'pieces':>6} {'cleared':>7} {'net':>4} "
              f"{'bcount':>6} {'eval':>9}")
        for off in range(min(40, last_ok + 1)):
            r = rows[last_ok - off]
            print(f"# {off:>4} {r['move_idx']:>6} {r['pieces_placed']:>6} "
                  f"{r['squares_cleared']:>7} {net_fill(r):>+4} "
                  f"{r['board_count_after']:>6} {r['eval_after']:>9}")


if __name__ == "__main__":
    main()
