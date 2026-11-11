// Copyright 2026 Ian Karlsson
// E-mu disks: told apart, not listed
#include "emu.hpp"

namespace syfs::emu {

syfs_key Disk::probe(const Image &im)
{
	uint8_t s0[4];
	if (!im.read(0, s0, sizeof s0))
		return SYFS_NONE;
	if (!memcmp(s0, "EMU3", 4))
		return SYFS_FORMAT_EMU3;
	if (!memcmp(s0, "EMX2", 4))
		return SYFS_FORMAT_EMAX2;
	return SYFS_NONE;
}

std::unique_ptr<Medium> Disk::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Disk>(io, format);
}

}
