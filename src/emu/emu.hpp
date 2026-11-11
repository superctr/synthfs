// Copyright 2026 Ian Karlsson
// E-mu: the EIII's, E-IV's and Emax II's disks, told apart
#pragma once

#include "../internal.hpp"

namespace syfs::emu {

class Disk final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
};

}
