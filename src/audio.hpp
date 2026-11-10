// Copyright 2026 Ian Karlsson
// WAV and AIFF files on a file system's volume: told by name, their headers read
#pragma once

#include "internal.hpp"

#include <functional>

namespace sfs {

// a name ending .wav, .aif, .aiff or .aifc
bool audio_name(std::string_view name);

// n bytes at offset of a file; false where any is missing
using FileRead = std::function<bool(uint64_t offset, void *buf, size_t n)>;

// a WAV's or AIFF's rate, frames, original key and first loop, from a file of the bytes given; false where it is
// neither or its header cannot be read
bool audio_sample(const FileRead &read, uint64_t bytes, samplerfs_sample &out);

}
