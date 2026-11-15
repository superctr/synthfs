// Copyright 2026 Ian Karlsson
//! \file roland.hpp
//! Roland: the S-700 series' and the S-550 family's disks and floppies, the wave expansion boards and cards.
#pragma once

#include "../internal.hpp"

#include <map>

namespace syfs::roland {

//! What the S-700 series' disk and floppy readers share.
namespace s700 {
constexpr uint32_t SECTOR = 512, SEGMENT_BYTES = 18 * SECTOR, SEGMENT_WORDS = SEGMENT_BYTES / 2;
//! A 48-byte sample record's rate, key, points and loop; frames as stored.
void sample_of(const uint8_t *rec, uint64_t frames, syfs::Sample &out);
//! The category code a name starts with, else "".
std::string_view category(const uint8_t *name);
std::string name_of(const uint8_t *p);
//! Sector 0's line at +0x20: the system a disk was made with, a floppy's kind.
std::string system_line(const uint8_t *s0);
//! An empty volume written as the machine's own format leaves it.
/*!
 * An S-760's or S-770's hard disk, an S-760's or S-770's floppy; false with why where the image's size cannot be had.
 */
bool format_s760_disk(const syfs_io &io, const char *label, std::string &why);
bool format_s770_disk(const syfs_io &io, const char *label, std::string &why);
bool format_s760_floppy(const syfs_io &io, std::string &why);
bool format_s770_floppy(const syfs_io &io, std::string &why);
}

//! An S-760's or S-770's disk: five lists of directory entries and records.
class S700Disk final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	size_t sample_at(size_t i, int key) const override;
	//! The disk's name, apart from its label.
	void identify() override;
	//! Written by taking another S-700 disk's entries in, each with what it uses (docs/formats.md).
	bool writes() const override { return true; }
	bool names() const override { return false; }
	size_t adopt(const Medium &from, size_t i, size_t parent, std::string &why, bool &taken) override;

private:
	//! An entry's index by its list and number.
	std::vector<size_t> numbered_[5];
	//! The segment table, read when a wave is first.
	mutable std::vector<uint8_t> segments_;
};

//! An S-760's, S-770's or DJ-70's floppy: one volume.
class S700Floppy final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	size_t sample_at(size_t i, int key) const override;

private:
	size_t volume_ = syfs::NONE;
	std::vector<size_t> numbered_[4];
};

//! The S-550 family's areas of patches and tones, on a disk or a floppy.
class S550 : public Medium {
public:
	using Medium::Medium;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	size_t sample_at(size_t i, int key) const override;

protected:
	//! An area's patches and tones, from its parameters at start.
	void list_area(size_t ai, uint64_t start);
	//! The waves three bytes to two values (a floppy's), else a 16-bit word a value.
	virtual bool packed() const { return false; }
};

//! The S-550's and W-30's SCSI format: a directory of areas.
class S550Disk final : public S550 {
public:
	using S550::S550;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;
};

//! An S-550's, S-330's or W-30's floppy: one area (an S-50's told apart only).
class S550Floppy final : public S550 {
public:
	using S550::S550;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	void list() override;

protected:
	bool packed() const override { return true; }
};

//! A JV's DATA card, a RAM card of 32 or 64 KB: a JV-80's or JV-880's, a JV-1080's.
/*!
 * Its performances, patches and rhythm sets by name, and a JV-1080's card's own name (docs/formats.md, "Roland DATA
 * cards").
 */
class DataCard final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	//! The card's name, a JV-1080's.
	void identify() override;
	void list() override;
};

class Socket;

//! An SR-JV80 board or SO-PCM1 card, or an SRX board, as dumped.
class WaveRom final : public Medium {
public:
	WaveRom(const syfs_io &io, syfs_key format);
	~WaveRom() override;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	//! A card or a board, by its size and name.
	static syfs_key medium_of(const Image &im);
	//! The board's name, and what it was sold as where it is one of Roland's.
	void identify() override;
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, syfs::Sample &out) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	size_t sample_at(size_t i, int key) const override;
	//! An SRX bank's bank select (MSB * 128 + LSB), an SRX patch's category.
	bool number(size_t i, syfs_key key, int64_t &value) const override;

private:
	void list_srjv80();
	void list_srx();
	size_t bank(std::string_view shape, std::string_view name);
	//! A wave's split as a sample entry, under the wave, its keys from the split before's top up to top.
	/*!
	 * A split of no record sounds nothing and has no entry.
	 */
	void split(size_t wave, uint32_t number, uint8_t below, uint8_t top, uint64_t record);
	//! The sample entry of the split that sounds nearest key.
	size_t nearest(size_t wave, int key) const;
	//! What a wave reads as: its sample at key 60, else its split's that sounds nearest.
	size_t at_middle_c(size_t i) const;
	uint32_t banks_ = 0;
	//! Each entry's property of the board's own, where it has one.
	std::map<size_t, std::pair<syfs_key, int64_t>> own_;
	std::unique_ptr<Socket> socket_;
	//! Each wave's splits: their top keys and sample entries (syfs::NONE where silent).
	std::map<size_t, std::vector<std::pair<uint8_t, size_t>>> splits_;
	//! The last sample decoded, whole.
	mutable size_t decoded_ = syfs::NONE;
	mutable std::vector<int16_t> frames_;
};

}
