cmake_minimum_required (VERSION 3.14)
project ("blokie-solver")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -DNDEBUG")

include(CheckCXXCompilerFlag)

# Give the solver a hardware popcount, on the compilers that need telling.
#
# BitBoard::count() runs tens of times per candidate board and the search
# evaluates thousands of boards per move, so how it is compiled decides how
# fast the search is. Baseline x86-64 predates POPCNT and so has no instruction
# to lower it to: GCC emits a call into libgcc's software popcount, and Clang
# open-codes a wider SWAR sequence. Measured on the benchmark, turning the
# instruction on is worth 2.3x under GCC and 1.6x under Clang.
#
# Nothing else needs the flag. aarch64 lowers the same code to an inline NEON
# popcount, and WASM has i64.popcnt in its core instruction set -- which is why
# this asks the compiler what it accepts rather than hard coding an ISA. A
# target that is already fine, or that has never heard of these flags, matches
# nothing and is left exactly as it was.
function(target_enable_hardware_popcount target)
	# x86-64-v2 is the 2009 baseline and the first level to include POPCNT.
	# -mpopcnt is the fallback for compilers predating the level names.
	foreach(flag "-march=x86-64-v2" "-mpopcnt")
		string(MAKE_C_IDENTIFIER "CXX_ACCEPTS_${flag}" result_var)
		# Without -Werror, a compiler that merely warns that the flag is unused
		# would look as though it had accepted it.
		set(CMAKE_REQUIRED_FLAGS "-Werror")
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
	# Native build: solver + benchmark/fitness drivers.
	find_package(Threads REQUIRED)

	add_executable(benchmark benchmark.cpp)
	target_link_libraries(benchmark PRIVATE solver)

	add_executable(fitness fitness.cpp)
	target_link_libraries(fitness PRIVATE solver Threads::Threads)

	# Release builds are -flto, so codegen happens at link and every target
	# that contributes to it needs the flag, not just the one holding the
	# popcounts. The WASM build is deliberately left out: it does not need
	# this, and changing how it compiles would change the committed binary.
	target_enable_hardware_popcount(solver)
	target_enable_hardware_popcount(benchmark)
	target_enable_hardware_popcount(fitness)
endif()
