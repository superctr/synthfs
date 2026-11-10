# Copyright 2026 Ian Karlsson
# samplerfs: sampler disk images read (the library), samplerfs-ls (the tool) and the tests
cmake_minimum_required(VERSION 3.23)
project(samplerfs VERSION 0.1.0 LANGUAGES CXX)

option(SAMPLERFS_TOOLS "Build samplerfs-ls, the command line tool" ${PROJECT_IS_TOP_LEVEL})
option(SAMPLERFS_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
	set(SAMPLERFS_WARNINGS /W4)
else()
	set(SAMPLERFS_WARNINGS -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_library(samplerfs STATIC src/samplerfs.cpp src/image.cpp src/iso9660.cpp src/audio.cpp src/fat.cpp src/fat_format.cpp
	src/roland/s700.cpp src/roland/s700_floppy.cpp src/roland/s550.cpp src/roland/wave.cpp
	src/akai/s1000.cpp src/akai/s900.cpp
	src/emu/emu.cpp)
target_include_directories(samplerfs PUBLIC include)
target_compile_options(samplerfs PRIVATE ${SAMPLERFS_WARNINGS})

if(SAMPLERFS_TOOLS)
	add_executable(samplerfs-ls tools/samplerfs_ls.cpp)
	target_link_libraries(samplerfs-ls PRIVATE samplerfs)
	target_compile_options(samplerfs-ls PRIVATE ${SAMPLERFS_WARNINGS})
endif()

if(SAMPLERFS_TESTS)
	enable_testing()
	add_executable(samplerfs_test test/samplerfs_test.cpp)
	target_link_libraries(samplerfs_test PRIVATE samplerfs)
	target_compile_options(samplerfs_test PRIVATE ${SAMPLERFS_WARNINGS})
	add_test(NAME samplerfs COMMAND samplerfs_test)
endif()
