# Copyright 2026 Ian Karlsson
build_dir := "build"

# configure (once) and build the library, the synthfs tool and the tests
build:
	cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build {{build_dir}}

test: build
	ctest --test-dir {{build_dir}} --output-on-failure

# every image in a folder, one line each
scan dir: build
	find {{dir}} -type f \( -iname '*.iso' -o -iname '*.bin' -o -iname '*.cdr' -o -iname '*.img' \) -print0 | sort -z | xargs -0 {{build_dir}}/synthfs scan

# include/synthfs/syfs_keys.h made again from src/keys.def
keys:
	python3 tools/keys.py

# src/maps.cpp made again from maps/*.json
maps:
	python3 tools/maps.py
