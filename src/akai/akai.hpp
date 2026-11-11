// Copyright 2026 Ian Karlsson
// AKAI: the S1000's and S3000's partitions and floppies, the S900's and S950's floppies
#pragma once

#include "../internal.hpp"

namespace syfs::akai {

// a partition header's 98 halfwords after its size: k times HEADER_STEP
constexpr uint32_t HEADER_WORDS = 98;
constexpr uint16_t HEADER_STEP = 0x0D05;

// S1000 and S3000 volumes of programs, samples and multis, wherever their blocks are
class S1000 : public Medium {
public:
	using Medium::Medium;
	std::vector<size_t> refs(size_t i) const override;
	size_t sample_at(size_t i, int key) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;

protected:
	// where a volume's blocks are: a partition's, or a floppy's; its FAT, a halfword a block
	struct Store {
		uint64_t base, block;
		uint32_t blocks;
		std::vector<uint8_t> fat;
	};
	std::vector<Store> stores_;
	std::vector<std::pair<size_t, size_t>> volume_store_;  // a volume's entry, its store

	std::vector<uint8_t> read_file(size_t i, uint64_t max) const;
	// n bytes of file i from offset along its volume's FAT; the count read, short where its chain ends
	size_t read_at(size_t i, uint64_t offset, uint8_t *buf, size_t n) const;
	size_t in_volume(size_t vol, syfs_key kind, const std::string &name) const;
};

// a disk: partitions of volumes
class S1000Disk final : public S1000 {
public:
	using S1000::S1000;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
};

// a floppy: one volume
class S1000Floppy final : public S1000 {
public:
	using S1000::S1000;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
};

// an S900's or S950's floppy
class S900 final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	size_t sample_at(size_t i, int key) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	std::vector<uint8_t> read_file(size_t i, uint64_t max) const;
	size_t named(syfs_key kind, const std::string &name) const;
};

}
