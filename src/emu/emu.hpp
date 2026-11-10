// Copyright 2026 Ian Karlsson
// E-mu: the EIII's, E-IV's and Emax II's disks, told apart
#pragma once

#include "../internal.hpp"

namespace sfs::emu {

class Disk final : public Medium {
public:
	using Medium::Medium;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
};

}
