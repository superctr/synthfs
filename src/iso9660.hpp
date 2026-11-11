// Copyright 2026 Ian Karlsson
// A plain CD-ROM: its WAV and AIFF files
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
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	bool walk(uint32_t extent, uint32_t bytes, size_t parent, int depth);
};

}
