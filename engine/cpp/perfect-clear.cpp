// How often can the whole board be cleared?
//
// Players suspect the game hands them a set of pieces that wipes the board
// clean. This driver measures how often that is even on offer. A set counts
// when some order and some choice of placements leaves the board with nothing
// on it, whether that happens on the third piece or on the first with two still
// to play.
//
// Policies simulated over the same piece sequence:
//   baseline - the shipped AI, playing for score. Measures how often a wipe is
//              available to a strong player who is not looking for one.
//   seeking  - plays for wipes: it takes one whenever a set offers it, and
//              otherwise steers the board toward positions a later set can
//              wipe. How hard it steers is set by -w. Since it is a strong
//              player given the run of the board, its wipe rate is an
//              approximate upper bound on what any player could see.
//
// Usage: perfect-clear [sets] [-w weight] [-s seed] [--baseline-only]

#include "solver.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// SFC32 RNG - mirrors the sfc32 implementation in engine/js/blokie.js so piece
// sequences match the other benchmarks.
struct Sfc32 {
	uint32_t a, b, c, d;
	Sfc32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
		: a(a), b(b), c(c), d(d) {}

	double next() {
		uint32_t t = (a + b) + d;
		d = d + 1;
		a = b ^ (b >> 9);
		b = c + (c << 3);
		c = (c << 21) | (c >> 11);
		c = c + t;
		return (double)t / 4294967296.0;
	}
};

// The 27 lines a clear can happen on: 9 rows, 9 columns, 9 cubes.
struct Lines {
	std::vector<BitBoard> line;
	Lines() {
		for (unsigned i = 0; i < 9; ++i) {
			line.push_back(BitBoard::row(i));
		}
		for (unsigned i = 0; i < 9; ++i) {
			line.push_back(BitBoard::column(i));
		}
		for (unsigned r = 0; r < 3; ++r) {
			for (unsigned c = 0; c < 3; ++c) {
				line.push_back(BitBoard::cube(r, c));
			}
		}
	}
};
const Lines LINES;

// Which of the 27 lines pass through each square.
struct LinesThrough {
	uint8_t through[81][3] = {};
	LinesThrough() {
		for (unsigned r = 0; r < 9; ++r) {
			for (unsigned c = 0; c < 9; ++c) {
				const auto square = BitBoard::row(r) & BitBoard::column(c);
				int n = 0;
				for (int i = 0; i < 27; ++i) {
					if (LINES.line[i] & square) {
						through[r * 9 + c][n++] = (uint8_t)i;
					}
				}
			}
		}
	}
};
const LinesThrough LINES_THROUGH;

// Index of the lowest set square, numbered r * 9 + c. The bitboard's two words
// hold rows 0-5 and rows 6-8 in exactly that order, so the bit position is the
// square number.
int lowestSquare(BitBoard bb) {
	if (bb.getA()) {
		return __builtin_ctzll(bb.getA());
	}
	return 54 + __builtin_ctzll(bb.getB());
}

// The fewest squares that have to be placed before this board can be empty.
//
// A block only ever leaves the board as part of a completed row, column or
// cube, so emptying the board means completing some set of lines that covers
// every block, and every empty square inside those lines has to be filled at
// least once. Refilling a square that was already cleared only costs more, so
// the empty squares in the cheapest covering set of lines is a genuine lower
// bound on how many squares of piece the job takes.
//
// Returns cap when the true answer is cap or more, so callers pay only for the
// range they care about.
int coverCostImpl(BitBoard blocks, BitBoard filled, int cost, int cap) {
	const auto uncovered = blocks - filled;
	if (!uncovered) {
		return cost;
	}
	if (cost >= cap) {
		return cap;
	}
	const int square = lowestSquare(uncovered);
	int best = cap;
	for (int i = 0; i < 3; ++i) {
		const auto line = LINES.line[LINES_THROUGH.through[square][i]];
		const auto next = filled | line;
		const int next_cost = (next - blocks).count();
		if (next_cost >= best) {
			continue;
		}
		best = std::min(best, coverCostImpl(blocks, next, next_cost, best));
	}
	return best;
}

int coverCost(BitBoard blocks, int cap) {
	if (!blocks) {
		return 0;
	}
	return coverCostImpl(blocks, BitBoard::empty(), 0, cap);
}

int totalSquares(const Piece* list, int n) {
	int total = 0;
	for (int i = 0; i < n; ++i) {
		total += list[i].getBitBoard().count();
	}
	return total;
}

// Is there an order and a set of placements that plays every piece in list and
// leaves the board empty? list must be sorted so equal pieces sit next to each
// other: trying the same piece twice at one level repeats the subtree below it.
bool canEmptyWithAll(GameState game, const Piece* list, int n) {
	if (n == 0) {
		return !game.getBitBoard();
	}
	const int squares = totalSquares(list, n);
	if (coverCost(game.getBitBoard(), squares + 1) > squares) {
		return false;
	}

	for (int i = 0; i < n; ++i) {
		if (i > 0 && !(list[i - 1] < list[i])) {
			continue;
		}
		Piece rest[3];
		int rest_n = 0;
		for (int j = 0; j < n; ++j) {
			if (j != i) {
				rest[rest_n++] = list[j];
			}
		}
		for (const auto next : game.nextStates(list[i])) {
			if (canEmptyWithAll(next, rest, rest_n)) {
				return true;
			}
		}
	}
	return false;
}

// The board can be emptied partway through this set. Which is to say: some
// subset of the pieces, in some order, clears everything, and whatever is left
// over is then played onto the bare board.
struct Wipe {
	bool possible = false;
	// Pieces down the first time the board is empty: 1, 2 or 3.
	int earliest_depth = 0;
};

Wipe findWipe(GameState game, const Piece* pieces) {
	Wipe result;
	// A wipe uses at most every square in the set, so if the board cannot be
	// covered for that price no subset of the pieces can do it either.
	const int squares = totalSquares(pieces, 3);
	if (coverCost(game.getBitBoard(), squares + 1) > squares) {
		return result;
	}

	for (int size = 1; size <= 3; ++size) {
		for (int mask = 1; mask < 8; ++mask) {
			if (__builtin_popcount(mask) != size) {
				continue;
			}
			Piece used[3];
			int used_n = 0;
			for (int i = 0; i < 3; ++i) {
				if (mask & (1 << i)) {
					used[used_n++] = pieces[i];
				}
			}
			std::sort(used, used + used_n);
			if (canEmptyWithAll(game, used, used_n)) {
				result.possible = true;
				result.earliest_depth = size;
				return result;
			}
		}
	}
	return result;
}

// How far this board is from being wipeable, in squares, for the seeking
// policy to steer by. Capped: past the cap every board is equally hopeless and
// the shape of the position stops mattering.
const int COVER_CAP = 28;

struct Move {
	GameState end = GameState(BitBoard::empty());
	bool wiped = false;
	int earliest_depth = 0;
};

// Rank a finished set. A set that emptied the board beats one that did not,
// whatever it left behind; past that the board is judged on how close it sits
// to the next wipe, weighted against staying alive.
struct Score {
	bool wiped = false;
	uint64_t value = UINT64_MAX;

	bool better(const Score& other) const {
		if (wiped != other.wiped) {
			return wiped;
		}
		return value < other.value;
	}
};

// Score a finished set, giving up early once it cannot beat best.
//
// Both halves of the score only ever add, so a board whose cleanliness alone
// already costs more than the standing best cannot win however close to a wipe
// it is, and a board that has to come in under a certain distance from a wipe
// can have that distance measured with a matching cap. Between them the search
// pays the full price of the cover only for boards that are in the running.
Score scoreBoard(EvalWeights weights, GameState state, bool wiped,
	int cover_weight, const Score& best) {
	Score s;
	s.wiped = wiped;
	const bool must_beat_value = wiped == best.wiped;
	const uint64_t ceiling = must_beat_value ? best.value : UINT64_MAX;

	s.value = state.simpleEval(weights, ceiling);
	if (s.value >= ceiling) {
		s.value = UINT64_MAX;
		return s;
	}
	if (cover_weight) {
		int cap = COVER_CAP;
		if (ceiling != UINT64_MAX) {
			const uint64_t room =
				(ceiling - s.value + cover_weight - 1) / (uint64_t)cover_weight;
			cap = (int)std::min<uint64_t>(room, COVER_CAP);
		}
		s.value += (uint64_t)cover_weight *
			(uint64_t)coverCost(state.getBitBoard(), cap);
	}
	return s;
}

double wipeProbability(BitBoard bb, long long samples, Sfc32* rng);

// How many of the 47 pieces empty this board in a single placement. The chance
// the next set holds one is 1 - (1 - n/47)^3, so this is a direct reading of
// how live the position is, and unlike the cover cost it knows the difference
// between a gap some piece is shaped to fill and one no piece fits.
int onePieceWipes(GameState game) {
	int count = 0;
	for (int i = 0; i < Piece::NUM_PIECES; ++i) {
		const auto piece = Piece::byIndex(i);
		for (const auto next : game.nextStates(piece)) {
			if (!next.getBitBoard()) {
				count++;
				break;
			}
		}
	}
	return count;
}

// A board the search is considering leaving the set on.
struct Candidate {
	GameState end = GameState(BitBoard::empty());
	bool wiped = false;
	int earliest_depth = 0;
	uint64_t value = UINT64_MAX;
};

// The shortlist of endings worth a closer look, cheapest first.
class Shortlist {
public:
	explicit Shortlist(int size) : size_(size) {}

	// The score an ending has to beat to be worth keeping.
	uint64_t ceiling() const {
		return (int)entries_.size() < size_ ? UINT64_MAX : entries_.back().value;
	}

	void offer(const Candidate& c) {
		for (const auto& seen : entries_) {
			if (seen.end.getBitBoard() == c.end.getBitBoard() &&
				seen.wiped == c.wiped) {
				return;
			}
		}
		auto at = entries_.begin();
		while (at != entries_.end() && at->value <= c.value) {
			++at;
		}
		entries_.insert(at, c);
		if ((int)entries_.size() > size_) {
			entries_.pop_back();
		}
	}

	void clear() { entries_.clear(); }

	const std::vector<Candidate>& entries() const { return entries_; }

private:
	int size_;
	std::vector<Candidate> entries_;
};

// Play a set for wipes. Walks the same placements the shipped AI walks, but
// scores an ending by whether the board was ever empty along the way and by how
// close the board it leaves sits to the next wipe.
//
// The orderings the shipped search skips are the ones where nothing cleared
// anywhere, and a board that never cleared never emptied either, so skipping
// them cannot hide a wipe.
//
// With a shortlist size the search does not settle on the cheapest ending
// outright. It keeps the best few and then asks the question that actually
// matters of each one: how many pieces would empty the board from there. The
// cover cost cannot tell a gap a piece is shaped to fill from one nothing fits,
// and that difference is most of the game once the board is nearly clear.
Move makeSeekingMove(EvalWeights weights, GameState game, PieceSet piece_set,
	int cover_weight, int shortlist_size, long long lookahead_sets) {
	std::sort(piece_set.pieces, piece_set.pieces + 3);
	const int num_pieces = AI::countPieces(piece_set);
	const auto can_clear_with_2_pieces =
		AI::canClearWith2PiecesOrFewer(game, piece_set);

	Score best;
	Move result;
	result.end = GameState(BitBoard::full());
	Shortlist shortlist(shortlist_size);

	bool is_first_permutation = true;
	do {
		const auto p0 = piece_set.pieces[0];
		const auto p1 = piece_set.pieces[1];
		const auto p2 = piece_set.pieces[2];
		for (const auto after_p0 : game.nextStatesClearsFirst(p0)) {
			const bool empty_at_1 = !after_p0.getBitBoard();
			for (const auto after_p1 : after_p0.nextStatesClearsFirst(p1)) {
				const auto after_p1_max_count = game.getBitBoard().count() +
					p0.getBitBoard().count() + p1.getBitBoard().count();
				if (p1 < p0 &&
					after_p1.getBitBoard().count() == after_p1_max_count) {
					continue;
				}
				const bool empty_at_2 = empty_at_1 || !after_p1.getBitBoard();
				for (const auto after_p2 : after_p1.nextStates(p2)) {
					if (!is_first_permutation &&
						after_p2.getBitBoard().count() ==
							after_p1_max_count + p2.getBitBoard().count()) {
						continue;
					}
					const bool wiped = empty_at_2 || !after_p2.getBitBoard();
					if (best.wiped && !wiped) {
						// A wipe is already in hand and nothing that misses one
						// can beat it, so this board never needs scoring.
						continue;
					}
					if (wiped && !best.wiped) {
						// The first wipe of the search. Everything shortlisted
						// so far missed one and is now out of the running.
						shortlist.clear();
					}
					// While the shortlist has room every ending has to be
					// scored, so the standing best cannot be used to skip work.
					Score bar = best;
					if (shortlist_size > 1) {
						bar.wiped = wiped;
						bar.value = shortlist.ceiling();
					}
					const auto score = scoreBoard(weights, after_p2, wiped,
						cover_weight, bar);
					if (shortlist_size > 1 && score.value != UINT64_MAX) {
						Candidate c;
						c.end = after_p2;
						c.wiped = wiped;
						c.earliest_depth = empty_at_1 ? 1
							: (empty_at_2 ? 2 : (wiped ? 3 : 0));
						c.value = score.value;
						shortlist.offer(c);
					}
					if (score.better(best)) {
						best = score;
						result.end = after_p2;
						result.wiped = wiped;
						result.earliest_depth = empty_at_1 ? 1
							: (empty_at_2 ? 2 : (wiped ? 3 : 0));
					}
				}
			}
		}
		is_first_permutation = false;
	} while (can_clear_with_2_pieces &&
		std::next_permutation(piece_set.pieces, piece_set.pieces + num_pieces));

	// Second look: of the endings that survived, keep the one that leaves the
	// best chance of a wipe next set.
	//
	// With lookahead_sets > 0 that chance is measured the honest way, by
	// dealing the same trial sets to each ending and counting which of them a
	// wipe exists for. Otherwise it is read off the cheaper count of pieces
	// that would empty the board outright.
	if (shortlist_size > 1 && !shortlist.entries().empty()) {
		double best_live = -1;
		for (const auto& c : shortlist.entries()) {
			double live;
			if (lookahead_sets > 0) {
				// Every ending is judged against the same deals, so the winner
				// is not just the one that drew the kinder trial.
				Sfc32 trial(1, 2, 3, 17);
				live = wipeProbability(c.end.getBitBoard(), lookahead_sets,
					&trial);
			} else {
				live = onePieceWipes(c.end);
			}
			if (live > best_live) {
				best_live = live;
				result.end = c.end;
				result.wiped = c.wiped;
				result.earliest_depth = c.earliest_depth;
			}
		}
	}

	return result;
}

// === The ceiling ===
//
// Whatever a player does, each set arrives at some board, and the chance that
// set can wipe it is fixed by that board. So the best board there is caps how
// often anyone can wipe, however well they play and however long they spend
// setting up. This searches for that board.

// The share of piece sets that can wipe this board. Exact when samples is 0:
// every multiset of three pieces, weighted by how many orders it is dealt in.
double wipeProbability(BitBoard bb, long long samples, Sfc32* rng) {
	const GameState game(bb);
	if (samples > 0) {
		long long hits = 0;
		for (long long n = 0; n < samples; ++n) {
			Piece pieces[3];
			for (int j = 0; j < 3; ++j) {
				pieces[j] = Piece::byIndex((int)(rng->next() * Piece::NUM_PIECES));
			}
			hits += findWipe(game, pieces).possible ? 1 : 0;
		}
		return (double)hits / (double)samples;
	}

	double weighted = 0;
	for (int i = 0; i < Piece::NUM_PIECES; ++i) {
		for (int j = i; j < Piece::NUM_PIECES; ++j) {
			for (int k = j; k < Piece::NUM_PIECES; ++k) {
				Piece pieces[3] = {Piece::byIndex(i), Piece::byIndex(j),
					Piece::byIndex(k)};
				if (!findWipe(game, pieces).possible) {
					continue;
				}
				// How many of the 47^3 deals give this multiset.
				const int distinct = (i != j) + (j != k) + (i != k);
				weighted += distinct == 0 ? 1 : (distinct == 2 ? 3 : 6);
			}
		}
	}
	const double total = (double)Piece::NUM_PIECES * Piece::NUM_PIECES *
		Piece::NUM_PIECES;
	return weighted / total;
}

// A board the game could actually be sat on: no line is already complete,
// because the game clears one the instant it is made.
bool isLegalBoard(BitBoard bb) {
	for (int i = 0; i < 27; ++i) {
		if ((bb & LINES.line[i]) == LINES.line[i]) {
			return false;
		}
	}
	return true;
}

struct Stats {
	long long sets = 0;
	long long wipe_possible = 0;
	long long wiped = 0;
	long long depth[4] = {0, 0, 0, 0};
	long long ended_empty = 0;
	long long games = 0;
	long long occupancy_sum = 0;
	long long cover_sum = 0;
	// Sets dealt onto a board that was already empty. Wiping one of those is
	// still a set that clears away everything it puts down, but the board was
	// not being rescued from anything, so it is worth splitting out.
	long long started_empty = 0;
	long long started_empty_wipes = 0;
	long long by_occupancy_sets[82] = {0};
	long long by_occupancy_wipes[82] = {0};
};

void rate(const char* label, long long hits, long long total) {
	if (!total) {
		return;
	}
	const double pct = 100.0 * (double)hits / (double)total;
	if (hits) {
		std::printf("%-28s %10lld  %8.4f%%   1 in %.0f\n", label, hits, pct,
			(double)total / (double)hits);
	} else {
		std::printf("%-28s %10lld  %8.4f%%   none seen\n", label, hits, pct);
	}
}

void report(const char* name, const Stats& s) {
	std::printf("\n=== %s ===\n", name);
	std::printf("piece sets played:           %lld\n", s.sets);
	std::printf("games ended:                 %lld", s.games);
	if (s.games) {
		std::printf("  (mean %.0f sets per game)",
			(double)s.sets / (double)s.games);
	}
	std::printf("\n");
	std::printf("mean blocks on board:        %.2f\n",
		(double)s.occupancy_sum / (double)s.sets);
	std::printf("mean squares from a wipe:    %.2f  (capped at %d)\n",
		(double)s.cover_sum / (double)s.sets, COVER_CAP);
	std::printf("sets dealt onto empty board: %lld (%.2f%%)\n", s.started_empty,
		100.0 * (double)s.started_empty / (double)s.sets);
	std::printf("\n%-28s %10s  %9s   %s\n", "", "count", "of sets", "frequency");
	rate("wipe available", s.wipe_possible, s.sets);
	rate("  board was not empty", s.wipe_possible - s.started_empty_wipes,
		s.sets - s.started_empty);
	rate("wipe taken", s.wiped, s.sets);
	rate("  empty after 1st piece", s.depth[1], s.sets);
	rate("  empty after 2nd piece", s.depth[2], s.sets);
	rate("  empty after 3rd piece", s.depth[3], s.sets);
	rate("set ended on empty board", s.ended_empty, s.sets);
}

void reportOccupancy(const char* name, const Stats& s) {
	std::printf("\n=== %s: wipe availability by blocks on board ===\n", name);
	std::printf("blocks       sets    share   wipe available\n");
	for (int i = 0; i < 82; ++i) {
		if (!s.by_occupancy_sets[i]) {
			continue;
		}
		std::printf("%5d   %10lld   %5.1f%%   %8.3f%%\n", i,
			s.by_occupancy_sets[i],
			100.0 * (double)s.by_occupancy_sets[i] / (double)s.sets,
			100.0 * (double)s.by_occupancy_wipes[i] /
				(double)s.by_occupancy_sets[i]);
	}
}

// Climb from a starting board to the one nearby that the most piece sets can
// wipe, by turning single squares on and off. Neighbours are judged on a
// sample, and the winner is measured exactly at the end.
BitBoard climb(BitBoard start, long long samples, Sfc32* rng, bool trace) {
	auto best = start;
	// Every neighbour of a given board is judged against the same piece sets,
	// so the comparison is not decided by which board drew the kinder sample.
	const uint32_t sample_seed = 11;
	Sfc32 fixed(1, 2, 3, sample_seed);
	double best_score = wipeProbability(best, samples, &fixed);

	for (int round = 0; round < 40; ++round) {
		auto round_best = best;
		double round_score = best_score;
		for (unsigned r = 0; r < 9; ++r) {
			for (unsigned c = 0; c < 9; ++c) {
				const auto square = BitBoard::row(r) & BitBoard::column(c);
				const auto candidate = (best & square) ? (best - square)
					: (best | square);
				if (!isLegalBoard(candidate)) {
					continue;
				}
				Sfc32 same(1, 2, 3, sample_seed);
				const double score = wipeProbability(candidate, samples, &same);
				if (score > round_score) {
					round_score = score;
					round_best = candidate;
				}
			}
		}
		if (!(round_score > best_score)) {
			break;
		}
		best = round_best;
		best_score = round_score;
		if (trace) {
			std::printf("  round %2d: %d blocks, sampled %.2f%%\n", round,
				best.count(), 100.0 * best_score);
			std::fflush(stdout);
		}
	}
	(void)rng;
	return best;
}

// Every board that fits inside one line, measured exactly.
//
// A block outside the line the wipe is built on has to be taken by a second
// completed line, and that second line costs at least eight more squares on top
// of the first. Nothing with three or four pieces to spend can pay for both, so
// the best boards to be sat on are the ones that fit inside a single line, and
// there are only 511 of those per line. This walks all of them.
void runLineScan(int family) {
	struct Family {
		const char* name;
		BitBoard line;
	};
	const Family families[] = {
		{"row 0 (edge)", BitBoard::row(0)},
		{"row 4 (middle)", BitBoard::row(4)},
		{"column 4 (middle)", BitBoard::column(4)},
		{"cube (0,0) (corner)", BitBoard::cube(0, 0)},
		{"cube (1,1) (centre)", BitBoard::cube(1, 1)},
	};
	const int num_families = (int)(sizeof(families) / sizeof(families[0]));
	if (family < 0 || family >= num_families) {
		std::fprintf(stderr, "family must be 0..%d\n", num_families - 1);
		return;
	}

	const auto& f = families[family];
	std::printf("=== every board inside %s, exactly ===\n", f.name);
	std::printf("511 boards x all 47^3 deals each\n\n");

	// The squares of the line, low to high, so a subset mask picks them out.
	std::vector<BitBoard> squares;
	auto rest = f.line;
	while (rest) {
		const auto square = rest.leastSignificantBit();
		squares.push_back(square);
		rest = rest - square;
	}

	std::vector<std::pair<double, BitBoard>> results;
	for (int mask = 0; mask < 511; ++mask) {
		auto bb = BitBoard::empty();
		for (int i = 0; i < 9; ++i) {
			if (mask & (1 << i)) {
				bb = bb | squares[i];
			}
		}
		results.push_back({wipeProbability(bb, 0, nullptr), bb});
		if ((mask & 63) == 63) {
			std::printf("  ... %d/511 done\n", mask + 1);
			std::fflush(stdout);
		}
	}

	std::sort(results.begin(), results.end(),
		[](const std::pair<double, BitBoard>& a,
		   const std::pair<double, BitBoard>& b) { return a.first > b.first; });

	std::printf("\ntop 8 boards inside %s:\n", f.name);
	for (int i = 0; i < 8 && i < (int)results.size(); ++i) {
		std::printf("%d. %d blocks, %.4f%% of deals (1 in %.0f)\n%s\n", i + 1,
			results[i].second.count(), 100.0 * results[i].first,
			results[i].first > 0 ? 1.0 / results[i].first : 0.0,
			results[i].second.str().c_str());
	}
	std::printf("BEST %s %.6f\n", f.name, results[0].first);
}

void runCeiling(long long samples, uint32_t seed) {
	std::printf("=== the ceiling: the board the most piece sets can wipe ===\n");
	std::printf("neighbours judged on %lld sampled sets, winners measured "
		"exactly over all 47^3 = %d deals\n\n",
		samples, Piece::NUM_PIECES * Piece::NUM_PIECES * Piece::NUM_PIECES);

	Sfc32 rng(1, 2, 3, seed);
	std::vector<std::pair<const char*, BitBoard>> seeds;

	seeds.push_back({"empty board", BitBoard::empty()});
	// A row short of a few squares, the shape a wipe hunter would aim for.
	for (int gap = 1; gap <= 5; ++gap) {
		auto bb = BitBoard::row(4);
		for (int i = 0; i < gap; ++i) {
			bb = bb - (BitBoard::row(4) & BitBoard::column(4 + i));
		}
		static char names[6][32];
		std::snprintf(names[gap], sizeof(names[gap]), "row less %d in a row", gap);
		seeds.push_back({names[gap], bb});
	}
	// A cube short of a few squares.
	{
		auto bb = BitBoard::cube(1, 1);
		bb = bb - (BitBoard::row(4) & BitBoard::column(4));
		seeds.push_back({"cube less its middle", bb});
	}
	// Random sparse boards, to give the climb somewhere else to start from.
	for (int i = 0; i < 4; ++i) {
		auto bb = BitBoard::empty();
		const int blocks = 4 + (int)(rng.next() * 12);
		while (bb.count() < blocks) {
			const auto square = BitBoard::row((unsigned)(rng.next() * 9)) &
				BitBoard::column((unsigned)(rng.next() * 9));
			if (isLegalBoard(bb | square)) {
				bb = bb | square;
			}
		}
		static char names[4][32];
		std::snprintf(names[i], sizeof(names[i]), "random start %d", i + 1);
		seeds.push_back({names[i], bb});
	}

	BitBoard overall = BitBoard::empty();
	double overall_p = -1;
	for (const auto& s : seeds) {
		std::printf("from %s (%d blocks):\n", s.first, s.second.count());
		const auto found = climb(s.second, samples, &rng, true);
		const double exact = wipeProbability(found, 0, nullptr);
		std::printf("  settled on %d blocks, exactly %.4f%% of deals "
			"(1 in %.0f)\n%s\n", found.count(), 100.0 * exact,
			exact > 0 ? 1.0 / exact : 0.0, found.str().c_str());
		std::fflush(stdout);
		if (exact > overall_p) {
			overall_p = exact;
			overall = found;
		}
	}

	std::printf("\nbest board found: %.4f%% of piece sets can wipe it "
		"(1 in %.0f)\n%s\n", 100.0 * overall_p,
		overall_p > 0 ? 1.0 / overall_p : 0.0, overall.str().c_str());
	std::printf("This is a hill climb from a handful of starts, so it is the\n"
		"best board found and not a proven best. --line-scan walks every\n"
		"board inside a single line without missing any.\n");
}

}  // namespace

int main(int argc, char** argv) {
	long long num_sets = 100000;
	int cover_weight = 40000;
	uint32_t seed = 4;
	bool baseline_only = false;
	bool seeking_only = false;
	bool verify = false;
	bool ceiling = false;
	int line_scan = -1;
	int shortlist_size = 1;
	long long lookahead_sets = 0;

	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "-w") && i + 1 < argc) {
			cover_weight = std::atoi(argv[++i]);
		} else if (!std::strcmp(argv[i], "-q") && i + 1 < argc) {
			lookahead_sets = std::atoll(argv[++i]);
		} else if (!std::strcmp(argv[i], "-k") && i + 1 < argc) {
			shortlist_size = std::atoi(argv[++i]);
		} else if (!std::strcmp(argv[i], "-s") && i + 1 < argc) {
			seed = (uint32_t)std::atoll(argv[++i]);
		} else if (!std::strcmp(argv[i], "--baseline-only")) {
			baseline_only = true;
		} else if (!std::strcmp(argv[i], "--seeking-only")) {
			seeking_only = true;
		} else if (!std::strcmp(argv[i], "--verify")) {
			verify = true;
		} else if (!std::strcmp(argv[i], "--ceiling")) {
			ceiling = true;
		} else if (!std::strcmp(argv[i], "--line-scan") && i + 1 < argc) {
			line_scan = std::atoi(argv[++i]);
		} else {
			num_sets = std::atoll(argv[i]);
			if (num_sets <= 0) {
				std::fprintf(stderr, "sets must be positive\n");
				return 1;
			}
		}
	}

	if (line_scan >= 0) {
		runLineScan(line_scan);
		return 0;
	}
	if (ceiling) {
		runCeiling(num_sets, seed);
		return 0;
	}

	const auto weights = EvalWeights::getDefault();
	std::printf("sets=%lld seed=%u cover weight=%d shortlist=%d lookahead=%lld\n",
		num_sets, seed, cover_weight, shortlist_size, lookahead_sets);

	// Both policies see the same piece sequence for as long as they both
	// survive, so a difference between them is the policy and not the deal.
	Sfc32 rng(1, 2, 3, seed);

	Stats baseline, seeking;
	GameState baseline_game(BitBoard::empty());
	GameState seeking_game(BitBoard::empty());

	const auto start = std::chrono::steady_clock::now();
	for (long long i = 0; i < num_sets; ++i) {
		Piece pieces[3];
		for (int j = 0; j < 3; ++j) {
			pieces[j] = Piece::byIndex((int)(rng.next() * Piece::NUM_PIECES));
		}
		const PieceSet piece_set(pieces[0], pieces[1], pieces[2]);

		for (int policy = 0; policy < 2; ++policy) {
			const bool seek = policy == 1;
			if (seek ? baseline_only : seeking_only) {
				continue;
			}
			Stats& stats = seek ? seeking : baseline;
			GameState& game = seek ? seeking_game : baseline_game;

			const int blocks = game.getBitBoard().count();
			stats.sets++;
			stats.occupancy_sum += blocks;
			stats.cover_sum += coverCost(game.getBitBoard(), COVER_CAP);
			stats.by_occupancy_sets[blocks]++;
			if (!blocks) {
				stats.started_empty++;
			}

			// The seeking search prefers a wipe over every board that misses
			// one, so it finds a wipe exactly when one exists and there is no
			// need to run the dedicated search as well. --verify checks that
			// the two agree.
			bool possible = false;
			if (seek) {
				const auto move = makeSeekingMove(weights, game, piece_set,
					cover_weight, shortlist_size, lookahead_sets);
				possible = move.wiped;
				if (verify && possible != findWipe(game, pieces).possible) {
					std::fprintf(stderr, "wipe disagreement on set %lld\n%s\n",
						i, game.getBitBoard().str().c_str());
					return 1;
				}
				if (move.wiped) {
					stats.wiped++;
					stats.depth[move.earliest_depth]++;
				}
				game = move.end;
			} else {
				possible = findWipe(game, pieces).possible;
				game = AI::makeMoveSimple(weights, game, piece_set);
				// The shipped AI only ever hands back the board it finished
				// the set on, so a board it emptied on the way through and
				// covered up again is not visible here. Availability above is
				// the honest number for this policy.
				if (!game.getBitBoard()) {
					stats.wiped++;
					stats.depth[3]++;
				}
			}

			if (possible) {
				stats.wipe_possible++;
				stats.by_occupancy_wipes[blocks]++;
				if (!blocks) {
					stats.started_empty_wipes++;
				}
			}

			if (!game.getBitBoard()) {
				stats.ended_empty++;
			}
			if (game.isOver()) {
				stats.games++;
				game = GameState(BitBoard::empty());
			}
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const double seconds = std::chrono::duration<double>(end - start).count();

	if (!seeking_only) {
		report("baseline: shipped AI, playing for score", baseline);
	}
	if (!baseline_only) {
		report("seeking: playing for wipes", seeking);
	}
	if (!seeking_only) {
		reportOccupancy("baseline", baseline);
	}
	if (!baseline_only) {
		reportOccupancy("seeking", seeking);
	}
	std::printf("\n%lld sets in %.1f seconds (%.0f sets/sec)\n", num_sets,
		seconds, (double)num_sets / seconds);
	return 0;
}
