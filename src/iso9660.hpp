// Copyright 2026 Ian Karlsson
// A plain CD-ROM: its WAV and AIFF files
#pragma once

#include "internal.hpp"

namespace sfs {

class Iso9660 final : public Medium {
public:
	using Medium::Medium;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	void list() override;

private:
	bool walk(uint32_t extent, uint32_t bytes, size_t parent, int depth);
};

}
