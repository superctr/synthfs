# Copyright 2026 Ian Karlsson
# synthfs: synthesizer and sampler images read (the library), synthfs-ls and synthfs-wav (the tools) and the tests
cmake_minimum_required(VERSION 3.23)
project(synthfs VERSION 0.1.0 LANGUAGES CXX)

option(SYNTHFS_TOOLS "Build synthfs-ls and synthfs-wav, the command line tools" ${PROJECT_IS_TOP_LEVEL})
option(SYNTHFS_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
	set(SYNTHFS_WARNINGS /W4)
else()
	set(SYNTHFS_WARNINGS -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_library(synthfs STATIC src/synthfs.cpp src/image.cpp src/store.cpp src/vhd.cpp src/smartmedia_format.cpp src/iso9660.cpp src/cue.cpp src/audio.cpp src/fat.cpp src/fat_write.cpp src/fat_format.cpp src/maps.cpp src/map.cpp
	src/roland/s700.cpp src/roland/s700_floppy.cpp src/roland/s700_format.cpp src/roland/s550.cpp src/roland/wave.cpp src/roland/syx.cpp
	src/akai/s1000.cpp src/akai/s900.cpp
	src/emu/emu.cpp)
target_include_directories(synthfs PUBLIC include)
target_compile_options(synthfs PRIVATE ${SYNTHFS_WARNINGS})

if(SYNTHFS_TOOLS)
	add_executable(synthfs-ls tools/synthfs_ls.cpp)
	target_link_libraries(synthfs-ls PRIVATE synthfs)
	target_compile_options(synthfs-ls PRIVATE ${SYNTHFS_WARNINGS})
	add_executable(synthfs-wav tools/synthfs_wav.cpp)
	target_link_libraries(synthfs-wav PRIVATE synthfs)
	target_compile_options(synthfs-wav PRIVATE ${SYNTHFS_WARNINGS})
	add_executable(synthfs-edit tools/synthfs_edit.cpp)
	target_link_libraries(synthfs-edit PRIVATE synthfs)
	target_compile_options(synthfs-edit PRIVATE ${SYNTHFS_WARNINGS})
	add_executable(synthfs-convert tools/synthfs_convert.cpp)
	target_link_libraries(synthfs-convert PRIVATE synthfs)
	target_compile_options(synthfs-convert PRIVATE ${SYNTHFS_WARNINGS})
endif()

if(SYNTHFS_TESTS)
	enable_testing()
	add_executable(synthfs_test test/synthfs_test.cpp)
	target_link_libraries(synthfs_test PRIVATE synthfs)
	target_compile_options(synthfs_test PRIVATE ${SYNTHFS_WARNINGS})
	add_test(NAME synthfs COMMAND synthfs_test)
endif()
