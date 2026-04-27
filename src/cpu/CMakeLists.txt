
include_directories(. ../.. ../includes ${SDL2_INCLUDE_DIR}) 

# Unfortunately we've got to specify the rules for the generated files twice,
# once for cross compiling (with calling the host cc directly) and once
# for native compiling so that the rules also work for non-Unix environments...
if(CMAKE_CROSSCOMPILING)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/build68k
		COMMAND cc ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c
		           -o ${CMAKE_CURRENT_BINARY_DIR}/build68k
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c)

	add_custom_command(OUTPUT cpudefs.c
		COMMAND ./build68k < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/table68k
			${CMAKE_CURRENT_BINARY_DIR}/build68k)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		COMMAND cc -I${CMAKE_CURRENT_SOURCE_DIR}
			   cpudefs.c ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			   ${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c
			   -o ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c cpudefs.c)

	add_custom_command(OUTPUT cpustbl.c 
				cpuemu_31.c cpuemu_32.c
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gencpu)

else()	# Rules for normal build follow

	add_executable(build68k build68k.c)

	add_custom_command(OUTPUT cpudefs.c
		COMMAND $<TARGET_FILE:build68k> < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
		DEPENDS table68k build68k)

	add_executable(gencpu gencpu.c readcpu.c cpudefs.c)

	add_custom_command(OUTPUT cpustbl.c 
				cpuemu_31.c cpuemu_32.c
		COMMAND $<TARGET_FILE:gencpu>  DEPENDS gencpu)

endif(CMAKE_CROSSCOMPILING)


# Generated cpuemu.c contains a lot of warnings we don't really care about...
if(CMAKE_COMPILER_IS_GNUCC)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-compare -Wno-shadow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-variable -Wno-unused-label -Wno-unused-but-set-variable")
endif(CMAKE_COMPILER_IS_GNUCC)

set(UAECPU_SOURCES
#cpuemu_0.c cpuemu_11.c cpuemu_12.c cpuemu_20.c cpuemu_21.c 
	cpuemu_31.c cpuemu_32.c
	cpudefs.c cpummu.c cpummu030.c cpustbl.c cpuemu_common.c
    	hatari-glue.c memory.c newcpu.c readcpu.c fpp.c
)

if(ENABLE_EXPERIMENTAL_UAE2026_JIT)
	list(APPEND UAECPU_SOURCES
		uae2026_jit_bridge.cpp
		uae2026_compiler_prefs_shim.cpp
		uae2026_compiler_unit.cpp
		uae2026_vm_alloc_unit.cpp
		uae2026_linker_stubs.cpp
	)
endif()

add_library(UaeCpu ${UAECPU_SOURCES})

if(ENABLE_EXPERIMENTAL_UAE2026_JIT)
	# Extra include paths needed by the vendored compiler unity build.
	# Must come after add_library.
	target_include_directories(UaeCpu PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/uae_cpu_2026
		${CMAKE_CURRENT_SOURCE_DIR}/uae_cpu_2026/compiler
		/workspace/projects/macemu/BasiliskII/src/CrossPlatform
	)
	# uae2026_compiler_unit.cpp includes vendored headers that conflict with
	# Previous's own headers of the same name (m68k.h, newcpu.h, readcpu.h).
	# Use the INCLUDE_DIRECTORIES source property (CMake ≥3.11) which places
	# these paths BEFORE any target-level or global include_directories() paths.
	# This ensures bare #include "m68k.h" etc. find the vendored versions first
	# instead of src/cpu/m68k.h (which has no include guard).
	set_source_files_properties(uae2026_compiler_unit.cpp uae2026_linker_stubs.cpp PROPERTIES
		INCLUDE_DIRECTORIES
			"${CMAKE_CURRENT_SOURCE_DIR}/uae_cpu_2026;${CMAKE_CURRENT_SOURCE_DIR}/uae_cpu_2026/compiler;/workspace/projects/macemu/BasiliskII/src/CrossPlatform"
	)
endif()
