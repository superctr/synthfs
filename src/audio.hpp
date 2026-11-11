// Copyright 2026 Ian Karlsson
//! \file audio.hpp
//! WAV and AIFF files on a file system's volume: told by name, their headers read.
#pragma once

#include "internal.hpp"

#include <functional>

namespace syfs {

//! A name ending .wav, .aif, .aiff or .aifc.
bool audio_name(std::string_view name);

//! Reads n bytes at offset of a file; false where any is missing.
using FileRead = std::function<bool(uint64_t offset, void *buf, size_t n)>;

//! Where a WAV's or AIFF's frames are and how a value is kept.
struct AudioLayout {
	enum Code { NONE, SIGNED, UNSIGNED, FLOAT };
	Code code = NONE;      //!< NONE: an encoding not read (compressed).
	uint64_t data = 0;     //!< The first frame's byte in the file.
	uint32_t block = 0;    //!< Bytes a frame.
	uint32_t width = 0;    //!< Bytes a value.
	bool big = false;      //!< Big-endian.
};

//! A WAV's or AIFF's rate, channels, bits, frames, original key and first loop, from a file of the bytes given.
/*!
 * Also its layout where layout is not null; false where it is neither or its header cannot be read.
 */
bool audio_sample(const FileRead &read, uint64_t bytes, syfs::Sample &out, AudioLayout *layout = nullptr);

//! A 16-bit WAV file's bytes around its frames, for a sample's header: what goes before them and what after.
/*!
 * Before: the RIFF header, the format and the data chunk's head.  After: a pad byte where the frames are an odd
 * count of bytes, then a smpl chunk with the original key and the loop, where the sample has either.
 * \return false where the frames are more than a WAV file holds.
 */
bool wav_around(const syfs::Sample &s, std::vector<uint8_t> &before, std::vector<uint8_t> &after);

//! Up to frames frames from frame first, decoded to 16 bits, the channels side by side; the count read.
size_t audio_wave(const FileRead &read, uint64_t bytes, uint64_t first, int16_t *out, size_t frames);

}
