// Copyright 2026 Ian Karlsson
//! \file iso9660.hpp
//! A plain CD-ROM: its WAV and AIFF files.
#pragma once

#include "internal.hpp"

namespace syfs {

class Iso9660 final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
	bool sample(size_t i, syfs::Sample &out) const override;
	uint64_t raw_size(size_t i) const override { return entries[i].kind == SYFS_TYPE_FILE ? entries[i].bytes : 0; }
	bool raw(size_t i, uint64_t offset, void *buf, size_t n) const override
	{
		return offset <= entries[i].bytes && n <= entries[i].bytes - offset && image.read(where[i] + offset, buf, n);
	}
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	bool walk(uint32_t extent, uint32_t bytes, size_t parent, int depth);
};

}
