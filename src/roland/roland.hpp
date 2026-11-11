// Copyright 2026 Ian Karlsson
// Roland: the S-700 series' and the S-550 family's disks and floppies, the wave expansion boards and cards
#pragma once

#include "../internal.hpp"

#include <map>

namespace sfs::roland {

// what the S-700 series' disk and floppy readers share
namespace s700 {
constexpr uint32_t SECTOR = 512, SEGMENT_BYTES = 18 * SECTOR, SEGMENT_WORDS = SEGMENT_BYTES / 2;
// a 48-byte sample record's rate, key, points and loop; frames as stored
void sample_of(const uint8_t *rec, uint64_t frames, samplerfs_sample &out);
// the category code a name starts with, else ""
std::string_view category(const uint8_t *name);
std::string name_of(const uint8_t *p);
// sector 0's line at +0x20: the system a disk was made with, a floppy's kind
std::string system_line(const uint8_t *s0);
// an empty volume written as the machine's own format leaves it: an S-760's or S-770's hard disk, an S-760's or
// S-770's floppy; false with why where the image's size cannot be had
bool format_s760_disk(const samplerfs_write_io &io, std::string &why);
bool format_s770_disk(const samplerfs_write_io &io, std::string &why);
bool format_s760_floppy(const samplerfs_write_io &io, std::string &why);
bool format_s770_floppy(const samplerfs_write_io &io, std::string &why);
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
	size_t wave(size_t i, const samplerfs_sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	// an entry's index by its list and number
	std::vector<size_t> numbered_[5];
	// the segment table, read when a wave is first
	mutable std::vector<uint8_t> segments_;
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
	size_t wave(size_t i, const samplerfs_sample &s, uint64_t first, int16_t *out, size_t frames) const override;

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
	size_t wave(size_t i, const samplerfs_sample &s, uint64_t first, int16_t *out, size_t frames) const override;

protected:
	// an area's patches and tones, from its parameters at start
	void list_area(size_t ai, uint64_t start);
	// the waves three bytes to two values (a floppy's), else a 16-bit word a value
	virtual bool packed() const { return false; }
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

protected:
	bool packed() const override { return true; }
};

class Socket;

// an SR-JV80 board or SO-PCM1 card, or an SRX board, as dumped
class WaveRom final : public Medium {
public:
	WaveRom(const samplerfs_io &io, samplerfs_format format);
	~WaveRom() override;
	static samplerfs_format probe(const Image &im);
	static std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format);
	// a card or a board, by its size and name
	static samplerfs_medium medium_of(const Image &im);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;
	size_t wave(size_t i, const samplerfs_sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	void list_srjv80();
	void list_srx();
	size_t bank(std::string_view shape);
	// a wave's split as a sample entry, under the wave, its keys from the split before's top up to top; a split of
	// no record sounds nothing and has no entry
	void split(size_t wave, uint32_t number, uint8_t below, uint8_t top, uint64_t record);
	// the sample entry of the split that sounds nearest key
	size_t nearest(size_t wave, int key) const;
	uint32_t banks_ = 0;
	std::unique_ptr<Socket> socket_;
	// each wave's splits: their top keys and sample entries (SAMPLERFS_NONE where silent)
	std::map<size_t, std::vector<std::pair<uint8_t, size_t>>> splits_;
	// the last sample decoded, whole
	mutable size_t decoded_ = SAMPLERFS_NONE;
	mutable std::vector<int16_t> frames_;
};

}
