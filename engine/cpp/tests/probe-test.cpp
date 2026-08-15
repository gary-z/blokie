#include "../solver.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

namespace {

struct Counts {
	uint64_t pairs = 0;
	uint64_t deaths = 0;
	uint64_t deep_pairs = 0;
	uint64_t near_death_pairs = 0;
};

Piece randomPiece(std::mt19937_64 &rng) {
	std::uniform_int_distribution<int> pieces(0, Piece::NUM_PIECES - 1);
	return Piece::byIndex(pieces(rng));
}

Counts checkPairs(uint64_t pairs, uint64_t seed, std::atomic<bool> &failed) {
	std::mt19937_64 rng(seed);
	GameState game(BitBoard::empty());
	uint64_t depth = 0;
	Counts counts;
	while (counts.pairs < pairs && !failed.load(std::memory_order_relaxed)) {
		const Piece p0 = randomPiece(rng);
		const Piece p1 = randomPiece(rng);
		const Piece p2 = randomPiece(rng);
		const PieceSet pieces(p0, p1, p2);
		const bool fits = AI::tripleFits(game, pieces);
		const auto move = AI::makeMoveSimpleDefault(game, pieces);
		const bool search_fits = move.evaluation != UINT64_MAX;
		if (fits != search_fits) {
			std::fprintf(stderr,
				"mismatch seed=%llu depth=%llu board=(%llx,%llx) fits=%d search=%d\n",
				(unsigned long long)seed, (unsigned long long)depth,
				(unsigned long long)game.getBitBoard().getA(),
				(unsigned long long)game.getBitBoard().getB(), fits, search_fits);
			failed.store(true, std::memory_order_relaxed);
			break;
		}

		++counts.pairs;
		if (depth >= 1000) ++counts.deep_pairs;
		if (!search_fits) {
			++counts.deaths;
			++counts.near_death_pairs;
			game = GameState(BitBoard::empty());
			depth = 0;
		} else {
			game = move.state;
			++depth;
		}
	}
	return counts;
}

} // namespace

int main(int argc, char **argv) {
	uint64_t total_pairs = 10000;
	if (argc >= 2) total_pairs = std::strtoull(argv[1], nullptr, 10);
	if (total_pairs == 0) {
		std::fprintf(stderr, "pair count must be positive\n");
		return 2;
	}

	unsigned threads = std::thread::hardware_concurrency();
	if (threads == 0) threads = 1;
	if (argc >= 3) threads = (unsigned)std::strtoul(argv[2], nullptr, 10);
	threads = std::max(1U, (unsigned)std::min<uint64_t>(threads, total_pairs));

	std::atomic<bool> failed{false};
	std::vector<Counts> counts(threads);
	std::vector<std::thread> workers;
	workers.reserve(threads);
	for (unsigned id = 0; id < threads; ++id) {
		const uint64_t begin = total_pairs * id / threads;
		const uint64_t end = total_pairs * (id + 1) / threads;
		workers.emplace_back([&, id, begin, end]() {
			counts[id] = checkPairs(end - begin,
				0x5242455354494d41ULL + id, failed);
		});
	}
	for (auto &worker : workers) worker.join();

	Counts total;
	for (const auto &count : counts) {
		total.pairs += count.pairs;
		total.deaths += count.deaths;
		total.deep_pairs += count.deep_pairs;
		total.near_death_pairs += count.near_death_pairs;
	}
	if (failed.load(std::memory_order_relaxed)) return 1;
	if (total.pairs != total_pairs) {
		std::fprintf(stderr, "checked only %llu of %llu requested pairs\n",
			(unsigned long long)total.pairs, (unsigned long long)total_pairs);
		return 1;
	}

	std::printf("checked %llu real-play board/triple pairs: all equivalent\n",
		(unsigned long long)total.pairs);
	std::printf("deep pairs (depth >= 1000): %llu; death/near-death pairs: %llu\n",
		(unsigned long long)total.deep_pairs,
		(unsigned long long)total.near_death_pairs);
	return 0;
}
