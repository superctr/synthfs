# Copyright 2026 Ian Karlsson
# samplecd: sampler CD-ROM images read (the library), samplecd-ls (the tool) and the tests
cmake_minimum_required(VERSION 3.23)
project(samplecd VERSION 0.1.0 LANGUAGES CXX)

option(SAMPLECD_TOOLS "Build samplecd-ls, the command line tool" ${PROJECT_IS_TOP_LEVEL})
option(SAMPLECD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
	set(SAMPLECD_WARNINGS /W4)
else()
	set(SAMPLECD_WARNINGS -Wall -Wextra -Wno-missing-field-initializers)
endif()

add_library(samplecd STATIC src/samplecd.cpp src/image.cpp src/s7.cpp src/s550.cpp src/akai.cpp src/iso9660.cpp)
target_include_directories(samplecd PUBLIC include)
target_compile_options(samplecd PRIVATE ${SAMPLECD_WARNINGS})

if(SAMPLECD_TOOLS)
	add_executable(samplecd-ls tools/samplecd_ls.cpp)
	target_link_libraries(samplecd-ls PRIVATE samplecd)
	target_compile_options(samplecd-ls PRIVATE ${SAMPLECD_WARNINGS})
endif()

if(SAMPLECD_TESTS)
	enable_testing()
	add_executable(samplecd_test test/samplecd_test.cpp)
	target_link_libraries(samplecd_test PRIVATE samplecd)
	target_compile_options(samplecd_test PRIVATE ${SAMPLECD_WARNINGS})
	add_test(NAME samplecd COMMAND samplecd_test)
endif()
