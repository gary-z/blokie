#pragma once
#include <cstdint>

// SplitMix64's finalizer, which is what the native tools seed their
// mt19937_64 streams through. A seed derived by hand -- a thread index, a
// trial number, a board's position in a run -- differs from its neighbours in
// a handful of low bits, and mt19937_64 seeded with two such values starts on
// two states that stay visibly related. This spreads those bits over the whole
// word first, so streams that were meant to be independent are.
inline constexpr uint64_t splitMix64(uint64_t value) {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}
