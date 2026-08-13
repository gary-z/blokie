function(run_fitness probe_args output_var)
	execute_process(
		COMMAND "${FITNESS}" 8 --threads 2 --seed-base 20260812
			--max-moves 100 --burn-in 25 ${probe_args}
		RESULT_VARIABLE status
		OUTPUT_VARIABLE output
		ERROR_QUIET)
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "fitness exited with status ${status}")
	endif()
	set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

function(trajectories output result_var)
	string(REPLACE "\n" ";" lines "${output}")
	set(result "")
	foreach(line IN LISTS lines)
		if(line MATCHES "^([0-9]+) ([01]) ([0-9]+) ")
			string(APPEND result
				"${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3}\n")
		endif()
	endforeach()
	set(${result_var} "${result}" PARENT_SCOPE)
endfunction()

run_fitness("" without_probes)
run_fitness("--probe;10" with_probes)
run_fitness("--probe-occupancy;2;10;29" with_occupancy_probes)
run_fitness("--probe-occupancy-bands;2;4;10;23;36" with_banded_probes)
trajectories("${without_probes}" trajectory_without_probes)
trajectories("${with_probes}" trajectory_with_probes)
trajectories("${with_occupancy_probes}" trajectory_with_occupancy_probes)
trajectories("${with_banded_probes}" trajectory_with_banded_probes)

if(trajectory_without_probes STREQUAL "")
	message(FATAL_ERROR "fitness produced no trajectory rows")
endif()
if(NOT trajectory_without_probes STREQUAL trajectory_with_probes)
	message(FATAL_ERROR
		"probing changed the dealt-piece trajectory\n"
		"without probes:\n${trajectory_without_probes}"
		"with probes:\n${trajectory_with_probes}")
endif()
if(NOT trajectory_without_probes STREQUAL trajectory_with_occupancy_probes)
	message(FATAL_ERROR "occupancy probing changed the dealt-piece trajectory")
endif()
if(NOT trajectory_without_probes STREQUAL trajectory_with_banded_probes)
	message(FATAL_ERROR "banded probing changed the dealt-piece trajectory")
endif()
if(NOT with_probes MATCHES "# probe M=10 burn_in=25")
	message(FATAL_ERROR "fitness did not report probe metadata")
endif()
if(NOT with_occupancy_probes MATCHES
		"# probe adaptive low=2 middle=0 high=10 .*occupancy=29")
	message(FATAL_ERROR "fitness did not report occupancy probe metadata")
endif()
if(NOT with_banded_probes MATCHES
		"# probe adaptive low=2 middle=4 high=10 .*occupancy=23 occupancy_high=36")
	message(FATAL_ERROR "fitness did not report banded probe metadata")
endif()
