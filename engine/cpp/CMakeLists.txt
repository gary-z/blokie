cmake_minimum_required (VERSION 3.20)

# Native fitness runs are built with GCC 16: with both compilers targeting the
# same host, it measured faster than GCC 12 on the all-core benchmark. Use it
# when it is installed, choosing it before project() initializes the compiler
# and leaving explicit toolchains (notably Emscripten) alone.
#
# Preferred, not required. Which compiler produced a number is what makes two
# fitness runs comparable, and that is an argument about measurements, not
# about correctness: the regression tests below check what the search returns,
# which is the same answer whoever compiled it. Requiring GCC 16 to configure
# at all would mean no stock CI image and no contributor with a distribution
# toolchain could build the tests, to protect timings those builds never take.
# Turn BLOKIE_REQUIRE_GCC16 on for a build whose measurements have to line up
# with the recorded ones, and anything older becomes an error instead.
option(BLOKIE_REQUIRE_GCC16
	"Fail configuration unless the native build uses GCC 16 or newer" OFF)
set(BLOKIE_PROBE_TEST_PAIRS 10000 CACHE STRING
	"Board/triple pairs checked by the routine probe-equivalence ctest")
option(BLOKIE_ENABLE_LONG_TESTS
	"Add long-running statistical and exhaustive validation tests" OFF)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND
	NOT DEFINED CMAKE_CXX_COMPILER AND
	"$ENV{CXX}" STREQUAL "")
	find_program(BLOKIE_GXX16 NAMES g++-16)
	if(BLOKIE_GXX16)
		set(CMAKE_CXX_COMPILER "${BLOKIE_GXX16}" CACHE FILEPATH
			"C++ compiler used by the Blokie native build")
	elseif(BLOKIE_REQUIRE_GCC16)
		message(FATAL_ERROR
			"BLOKIE_REQUIRE_GCC16 is on but g++-16 was not found. Install "
			"g++-16 or configure with CXX=/path/to/g++-16 cmake ...")
	else()
		# Timings from this build are not comparable with the recorded ones, so
		# say which compiler is standing in rather than letting a fitness run
		# quietly attribute its numbers to the wrong one.
		message(STATUS
			"g++-16 not found; building with the default compiler. Fitness "
			"and benchmark timings will not be comparable with GCC 16 runs.")
	endif()
endif()

project ("blokie-solver")

if(BLOKIE_REQUIRE_GCC16 AND NOT EMSCRIPTEN AND
	(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
	 CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16))
	message(FATAL_ERROR
		"BLOKIE_REQUIRE_GCC16 is on, which needs GCC 16 or newer; found "
		"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings and optimization, written twice because the two command line
# dialects share no spelling: everything below is GCC's, which Clang and
# Emscripten's emcc also accept, and MSVC accepts none of it. Handing MSVC
# -Wall -Wextra -Werror does not configure a stricter build, it configures a
# build that cannot start, so the compiler that speaks the other dialect is
# asked for the same things in its own words.
#
# Both branches APPEND to the per-configuration flags rather than assigning
# them. A plain set() here reads as "these are the release flags", but what it
# does is silently discard whatever the caller passed in
# -DCMAKE_CXX_FLAGS_RELEASE=..., because the ordinary variable it creates
# shadows the cache entry that the command line wrote. The build still
# configures, still compiles and still passes its tests -- with the caller's
# flags absent. That is the worst shape a build bug can have: an experiment
# gated on -DSOME_MACRO=1 quietly measures the unmodified engine, and the
# result looks like evidence about the macro. Appending keeps the caller's
# flags and still lets these win, since later flags take precedence.
if(MSVC)
	# /permissive- turns off the Microsoft extensions that would let
	# nonstandard code through here and fail everywhere else.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /permissive-")
	# /GL is whole-program optimization, MSVC's -flto, and it needs /LTCG at
	# link time to finish the job.
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /GL /DNDEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE
		"${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
	set(CMAKE_STATIC_LINKER_FLAGS_RELEASE
		"${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -flto -DNDEBUG")
endif()

# Release builds are -flto, which leaves every object file holding the
# compiler's own intermediate form instead of machine code. Plain ar cannot
# read that. It still writes an archive, and the archive still has a symbol
# index -- an empty one -- so libsolver.a links as though the solver were not
# in it and the executables come out undefined on every symbol it defines.
#
# Whether that happens is a fact about the machine rather than the project:
# binutils can read the intermediate form through a plugin, so it works
# wherever the toolchain dropped one in the directory binutils looks in, and
# fails wherever it did not. Building on a machine with the plugin says
# nothing about the next machine.
#
# Every compiler with LTO ships an archiver that reads its own bitcode with no
# plugin involved -- llvm-ar, gcc-ar -- and CMake has already located it. Use
# it in preference to whichever ar was first on PATH. Emscripten is left alone:
# its toolchain file sets these to emar and emranlib itself, and it knows
# better than this file does.
if(NOT EMSCRIPTEN AND NOT MSVC)
	if(CMAKE_CXX_COMPILER_AR)
		set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
	endif()
	if(CMAKE_CXX_COMPILER_RANLIB)
		set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")
	endif()
endif()

include(CheckCXXCompilerFlag)

# Tune native-only tools for the machine that runs them. These executables are
# used locally for long fitness runs rather than shipped, so letting the
# compiler use the build host's ISA is both safe and useful. BMI1/BMI2 buy a
# little beyond x86-64-v2; AVX2 itself does not measurably help the branchy,
# one-board-at-a-time evaluation loop.
#
# If the compiler has no native tuning flag, at least give the solver a
# hardware popcount on the compilers that need telling.
#
# BitBoard::count() runs tens of times per candidate board and the search
# evaluates thousands of boards per move, so how it is compiled decides how
# fast the search is. Baseline x86-64 predates POPCNT and so has no instruction
# to lower it to: GCC emits a call into libgcc's software popcount, and Clang
# open-codes a wider SWAR sequence. Turning the instruction on replaces either
# with a single instruction in the hottest function the search has.
#
# Nothing else needs the flag. aarch64 lowers the same code to an inline NEON
# popcount, and WASM has i64.popcnt in its core instruction set -- which is why
# this asks the compiler what it accepts rather than hard coding an ISA. A
# target that is already fine, or that has never heard of these flags, matches
# nothing and is left exactly as it was.
function(target_enable_native_isa target)
	if(MSVC)
		# MSVC has no -march=native, and none of the names below mean anything
		# to it. It needs telling for the same reason though: its std::popcount
		# only compiles to POPCNT when the target is known to have one, and
		# otherwise dispatches on a runtime ISA check. /arch:AVX2 implies
		# POPCNT and is the coarsest grain MSVC offers to say so.
		set(CMAKE_REQUIRED_FLAGS "/WX")
		set(candidates "/arch:AVX2")
	else()
		set(CMAKE_REQUIRED_FLAGS "-Werror")
		# -march=native first, then x86-64-v2, the 2009 baseline and the first
		# level to include POPCNT, and -mpopcnt for compilers predating the
		# level names.
		set(candidates "-march=native" "-march=x86-64-v2" "-mpopcnt")
	endif()

	foreach(flag IN LISTS candidates)
		string(MAKE_C_IDENTIFIER "CXX_ACCEPTS_${flag}" result_var)
		# Without the warnings-are-errors flag above, a compiler that merely
		# warns that the flag is unused would look as though it had accepted it.
		check_cxx_compiler_flag("${flag}" ${result_var})
		if(${result_var})
			target_compile_options(${target} PRIVATE "${flag}")
			return()
		endif()
	endforeach()
endfunction()

# Core solver library - no threading, no RNG (WASM-compatible)
add_library(solver STATIC bitboard.cpp eval.cpp game.cpp solver.cpp)
# Keep the split files compiling as a single translation unit for the solver
# itself. Separate files give the intended module boundaries, but compiling
# them together lets the optimizer inline the hot BitBoard and evaluation
# routines into the search without relying on cross-TU LTO heuristics that
# measured ~3% slower. The library is still four files on disk.
set_target_properties(solver PROPERTIES UNITY_BUILD ON)

if(EMSCRIPTEN)
	# WASM build: solver + Emscripten bindings -> blokie-solver.js + blokie-solver.wasm
	add_executable(blokie-solver bindings.cpp)
	target_link_libraries(blokie-solver PRIVATE solver)

	set_target_properties(blokie-solver PROPERTIES
		SUFFIX ".js"
	)

	target_link_options(blokie-solver PRIVATE
		"SHELL:--bind"
		"SHELL:-s MODULARIZE=1"
		"SHELL:-s EXPORT_NAME='createBlokieSolver'"
		"SHELL:-s EXPORT_ES6=1"
		"SHELL:-s ENVIRONMENT='web,worker,node'"
		"SHELL:-s ALLOW_MEMORY_GROWTH=1"
		"SHELL:-s NO_EXIT_RUNTIME=1"
	)
else()
	# Native build: solver + benchmark/fitness drivers and regression tests.
	find_package(Threads REQUIRED)
	include(CTest)

	add_executable(benchmark benchmark.cpp)
	target_link_libraries(benchmark PRIVATE solver)

	add_executable(fitness fitness.cpp)
	target_link_libraries(fitness PRIVATE solver Threads::Threads)

	add_executable(search-stats search-stats.cpp)
	target_link_libraries(search-stats PRIVATE solver)

	if(BUILD_TESTING)
		add_executable(eval-test tests/eval-test.cpp)
		target_link_libraries(eval-test PRIVATE solver)
		add_test(NAME evaluation COMMAND eval-test)

		add_executable(bitboard-test tests/bitboard-test.cpp)
		target_link_libraries(bitboard-test PRIVATE solver)
		add_test(NAME bitboard COMMAND bitboard-test)

		add_executable(placement-test tests/placement-test.cpp)
		target_link_libraries(placement-test PRIVATE solver)
		add_test(NAME placement COMMAND placement-test)

		add_executable(search-test tests/search-test.cpp)
		target_link_libraries(search-test PRIVATE solver)
		add_test(NAME search COMMAND search-test)

		add_executable(probe-test tests/probe-test.cpp)
		target_link_libraries(probe-test PRIVATE solver Threads::Threads)
		add_test(NAME probe-equivalence COMMAND probe-test
			${BLOKIE_PROBE_TEST_PAIRS})
		if(BLOKIE_ENABLE_LONG_TESTS)
			add_test(NAME probe-equivalence-10m COMMAND probe-test 10000000)
			set_tests_properties(probe-equivalence-10m PROPERTIES LABELS long)
		endif()

		add_test(NAME fitness-probe-rng COMMAND ${CMAKE_COMMAND}
			-DFITNESS=$<TARGET_FILE:fitness>
			-P ${CMAKE_CURRENT_SOURCE_DIR}/fitness-probe-test.cmake)
	endif()

	# Release builds are -flto, so codegen happens at link and every target
	# that contributes to it needs the flag, not just the one holding the
	# native solver code. The WASM build is deliberately left out: it does not
	# need this, and changing how it compiles would change the committed binary.
	target_enable_native_isa(solver)
	target_enable_native_isa(benchmark)
	target_enable_native_isa(fitness)
	target_enable_native_isa(search-stats)
	if(BUILD_TESTING)
		target_enable_native_isa(eval-test)
		target_enable_native_isa(bitboard-test)
		target_enable_native_isa(placement-test)
		target_enable_native_isa(search-test)
		target_enable_native_isa(probe-test)
	endif()
endif()
