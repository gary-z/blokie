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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -DNDEBUG")

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
	set(CMAKE_REQUIRED_FLAGS "-Werror")
	check_cxx_compiler_flag("-march=native" CXX_ACCEPTS_MARCH_NATIVE)
	if(CXX_ACCEPTS_MARCH_NATIVE)
		target_compile_options(${target} PRIVATE "-march=native")
		return()
	endif()

	# x86-64-v2 is the 2009 baseline and the first level to include POPCNT.
	# -mpopcnt is the fallback for compilers predating the level names.
	foreach(flag "-march=x86-64-v2" "-mpopcnt")
		string(MAKE_C_IDENTIFIER "CXX_ACCEPTS_${flag}" result_var)
		# Without -Werror, a compiler that merely warns that the flag is unused
		# would look as though it had accepted it.
		check_cxx_compiler_flag("${flag}" ${result_var})
		if(${result_var})
			target_compile_options(${target} PRIVATE "${flag}")
			return()
		endif()
	endforeach()
endfunction()

# Core solver library - no threading, no RNG (WASM-compatible)
add_library(solver STATIC solver.cpp)

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

	if(BUILD_TESTING)
		add_executable(eval-test eval-test.cpp)
		target_link_libraries(eval-test PRIVATE solver)
		add_test(NAME evaluation COMMAND eval-test)

		add_executable(search-test search-test.cpp)
		target_link_libraries(search-test PRIVATE solver)
		add_test(NAME search COMMAND search-test)
	endif()

	# Release builds are -flto, so codegen happens at link and every target
	# that contributes to it needs the flag, not just the one holding the
	# native solver code. The WASM build is deliberately left out: it does not
	# need this, and changing how it compiles would change the committed binary.
	target_enable_native_isa(solver)
	target_enable_native_isa(benchmark)
	target_enable_native_isa(fitness)
	if(BUILD_TESTING)
		target_enable_native_isa(eval-test)
		target_enable_native_isa(search-test)
	endif()
endif()
