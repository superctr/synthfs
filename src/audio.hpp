// Copyright 2026 Ian Karlsson
// WAV and AIFF files on a file system's volume: told by name, their headers read
#pragma once

#include "internal.hpp"

#include <functional>

namespace syfs {

// a name ending .wav, .aif, .aiff or .aifc
bool audio_name(std::string_view name);

// n bytes at offset of a file; false where any is missing
using FileRead = std::function<bool(uint64_t offset, void *buf, size_t n)>;

// where a WAV's or AIFF's frames are and how a value is kept
struct AudioLayout {
	enum Code { NONE, SIGNED, UNSIGNED, FLOAT };
	Code code = NONE;      // NONE: an encoding not read (compressed)
	uint64_t data = 0;     // the first frame's byte in the file
	uint32_t block = 0;    // bytes a frame
	uint32_t width = 0;    // bytes a value
	bool big = false;      // big-endian
};

// a WAV's or AIFF's rate, channels, bits, frames, original key and first loop, from a file of the bytes given, and
// its layout where layout is not null; false where it is neither or its header cannot be read
bool audio_sample(const FileRead &read, uint64_t bytes, syfs::Sample &out, AudioLayout *layout = nullptr);

// up to frames frames from frame first, decoded to 16 bits, the channels side by side; the count read
size_t audio_wave(const FileRead &read, uint64_t bytes, uint64_t first, int16_t *out, size_t frames);

}
