// Copyright 2026 Ian Karlsson
// E-mu disks: told apart, not listed
#include "emu.hpp"

namespace sfs::emu {

samplerfs_format Disk::probe(const Image &im)
{
	uint8_t s0[4];
	if (!im.read(0, s0, sizeof s0))
		return SAMPLERFS_UNKNOWN;
	if (!memcmp(s0, "EMU3", 4))
		return SAMPLERFS_EMU3;
	if (!memcmp(s0, "EMX2", 4))
		return SAMPLERFS_EMAX2;
	return SAMPLERFS_UNKNOWN;
}

std::unique_ptr<Medium> Disk::open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<Disk>(io, format);
}

}
