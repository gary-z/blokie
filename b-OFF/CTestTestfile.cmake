# CMake generated Testfile for 
# Source directory: /tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp
# Build directory: /tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[evaluation]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/eval-test")
set_tests_properties([=[evaluation]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;235;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[bitboard]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/bitboard-test")
set_tests_properties([=[bitboard]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;239;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[placement]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/placement-test")
set_tests_properties([=[placement]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;243;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[search]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/search-test")
set_tests_properties([=[search]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;247;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[probe-equivalence]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/probe-test" "10000")
set_tests_properties([=[probe-equivalence]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;251;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[fitness-probe-rng]=] "/usr/bin/cmake" "-DFITNESS=/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/fitness" "-P" "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/fitness-probe-test.cmake")
set_tests_properties([=[fitness-probe-rng]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;258;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
add_test([=[golden]=] "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/b-OFF/golden" "--file" "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/../golden/golden.json")
set_tests_properties([=[golden]=] PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;262;add_test;/tmp/claude-1000/-workspaces-blockudoku/91612a82-a0b0-4c7e-973b-25ae2a18aa38/scratchpad/wt/engine/cpp/CMakeLists.txt;0;")
