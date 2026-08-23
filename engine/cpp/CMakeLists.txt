cmake_minimum_required (VERSION 3.20)

# Prefer GCC 16 for native performance tools when it is available.
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
		message(STATUS
			"g++-16 not found; building with the default compiler. Use the "
			"same compiler for comparable fitness and benchmark timings.")
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

# Strict warnings and optimized release flags.
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /permissive-")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /GL /DNDEBUG")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE
		"${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
	set(CMAKE_STATIC_LINKER_FLAGS_RELEASE
		"${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
	string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -flto -DNDEBUG")
endif()

# Use the compiler's archiver for LTO objects.
if(NOT EMSCRIPTEN AND NOT MSVC)
	if(CMAKE_CXX_COMPILER_AR)
		set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}")
	endif()
	if(CMAKE_CXX_COMPILER_RANLIB)
		set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}")
	endif()
endif()

include(CheckCXXCompilerFlag)

# Enable the best available host ISA for native-only tools.
function(target_enable_native_isa target)
	if(MSVC)
		set(CMAKE_REQUIRED_FLAGS "/WX")
		set(candidates "/arch:AVX2")
	else()
		set(CMAKE_REQUIRED_FLAGS "-Werror")
		set(candidates "-march=native" "-march=x86-64-v2" "-mpopcnt")
	endif()

	foreach(flag IN LISTS candidates)
		string(MAKE_C_IDENTIFIER "CXX_ACCEPTS_${flag}" result_var)
		check_cxx_compiler_flag("${flag}" ${result_var})
		if(${result_var})
			target_compile_options(${target} PRIVATE "${flag}")
			return()
		endif()
	endforeach()
endfunction()

# Core solver library.
add_library(solver STATIC bitboard.cpp eval.cpp game.cpp solver.cpp)
# Keep hot solver paths in one optimization unit.
set_target_properties(solver PROPERTIES UNITY_BUILD ON)

if(EMSCRIPTEN)
	# WebAssembly solver and bindings.
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
	# Native tools and tests.
	find_package(Threads REQUIRED)
	include(CTest)

	add_executable(benchmark benchmark.cpp)
	target_link_libraries(benchmark PRIVATE solver)

	add_executable(fitness fitness.cpp)
	target_link_libraries(fitness PRIVATE solver Threads::Threads)

	add_executable(weight-screen weight-screen.cpp)
	target_link_libraries(weight-screen PRIVATE solver Threads::Threads)

	add_executable(search-stats search-stats.cpp)
	target_link_libraries(search-stats PRIVATE solver)

	add_executable(golden ../golden/golden.cpp)
	target_link_libraries(golden PRIVATE solver)
	target_include_directories(golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../golden ${CMAKE_CURRENT_SOURCE_DIR})

	add_executable(golden_measure ../golden/golden_measure.cpp)
	target_link_libraries(golden_measure PRIVATE solver Threads::Threads)
	target_include_directories(golden_measure PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../golden ${CMAKE_CURRENT_SOURCE_DIR})

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

		add_test(NAME golden COMMAND golden --file ${CMAKE_CURRENT_SOURCE_DIR}/../golden/golden.json)
	endif()

	# LTO code generation requires the ISA flag on every native target.
	target_enable_native_isa(solver)
	target_enable_native_isa(benchmark)
	target_enable_native_isa(fitness)
	target_enable_native_isa(search-stats)
	target_enable_native_isa(golden)
	target_enable_native_isa(golden_measure)
	if(BUILD_TESTING)
		target_enable_native_isa(eval-test)
		target_enable_native_isa(bitboard-test)
		target_enable_native_isa(placement-test)
		target_enable_native_isa(search-test)
		target_enable_native_isa(probe-test)
	endif()
endif()
