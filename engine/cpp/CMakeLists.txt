cmake_minimum_required (VERSION 3.14)
project ("blokie-solver")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -DNDEBUG")

# Core solver library - no threading, no RNG (WASM-compatible)
add_library(solver STATIC solver.cpp)

if(EMSCRIPTEN)
	# Enable WASM SIMD
	target_compile_options(solver PRIVATE -msimd128 -msse2 -msse4.1)

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
	target_compile_options(blokie-solver PRIVATE -msimd128 -msse2 -msse4.1)
	target_link_options(blokie-solver PRIVATE -msimd128)
else()
	# Native build: enable SSE4.1 (includes SSE2)
	target_compile_options(solver PRIVATE -msse4.1)

	# Native benchmark
	add_executable(benchmark benchmark.cpp)
	target_link_libraries(benchmark PRIVATE solver)
	target_compile_options(benchmark PRIVATE -msse4.1)
endif()
