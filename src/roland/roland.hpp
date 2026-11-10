// Copyright 2026 Ian Karlsson
// Roland: the S-700 series' and the S-550 family's disks and floppies, the wave expansion boards and cards
#pragma once

#include "../internal.hpp"

namespace sfs::roland {

// what the S-700 series' disk and floppy readers share
namespace s700 {
constexpr uint32_t SECTOR = 512, SEGMENT_BYTES = 18 * SECTOR;
// a 48-byte sample record's rate, key, points and loop; frames as stored
void sample_of(const uint8_t *rec, uint64_t frames, samplerfs_sample &out);
// the category code a name starts with, else ""
std::string_view category(const uint8_t *name);
std::string name_of(const uint8_t *p);
// sector 0's line at +0x20: the system a disk was made with, a floppy's kind
std::string system_line(const uint8_t *s0);
}

// an S-760's or S-770's disk: five lists of directory entries and records
class S700Disk final : public Medium {
public:
	using Medium::Medium;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;

private:
	// an entry's index by its list and number
	std::vector<size_t> numbered_[5];
};

// an S-760's, S-770's or DJ-70's floppy: one volume
class S700Floppy final : public Medium {
public:
	using Medium::Medium;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;

private:
	size_t volume_ = SAMPLERFS_NONE;
	std::vector<size_t> numbered_[4];
};

// the S-550 family's areas of patches and tones, on a disk or a floppy
class S550 : public Medium {
public:
	using Medium::Medium;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;

protected:
	// an area's patches and tones, from its parameters at start
	void list_area(size_t ai, uint64_t start);
};

// the S-550's and W-30's SCSI format: a directory of areas
class S550Disk final : public S550 {
public:
	using S550::S550;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	void list() override;
};

// an S-550's, S-330's or W-30's floppy: one area (an S-50's told apart only)
class S550Floppy final : public S550 {
public:
	using S550::S550;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	void list() override;
};

class Socket;

// an SR-JV80 board or SO-PCM1 card, or an SRX board, as dumped
class WaveRom final : public Medium {
public:
	using Medium::Medium;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	// a card or a board, by its size and name
	static samplerfs_medium medium_of(const Image &im);
	void list() override;

private:
	void list_srjv80(const Socket &s);
	void list_srx(const Socket &s);
	size_t bank(std::string_view shape);
	uint32_t banks_ = 0;
};

}
