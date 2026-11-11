// Copyright 2026 Ian Karlsson
#pragma once

#include <synthfs/synthfs.h>

#include "store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace syfs {

inline constexpr size_t NONE = size_t(-1);

//! What an image holds: a node of the interface, by its index here.
struct Entry {
	syfs_key kind;       //!< SYFS_TYPE_.
	syfs_key format;     //!< The format of the volume or area it is in (an AKAI disk mixes S1000 and S3000).
	uint32_t number;     //!< Its place in its list on the disk, from 0.
	size_t parent;       //!< The entry that holds it, or NONE.
	char name[64];       //!< UTF-8, trailing blanks dropped.
	char tag[8];         //!< The image's own code (docs/properties.md), else empty.
	uint64_t bytes;      //!< Its size on the disk; 0 where unknown.
};

//! A sample's wave and how it plays.
/*!
 * Points are frames from the first frame stored, the ends exclusive.
 */
struct Sample {
	uint32_t rate;       //!< Hz.
	uint32_t channels;   //!< Values a frame.
	uint32_t bits;       //!< Each value's bits as stored; read as 16 whatever they are.
	int32_t key;         //!< The original key, MIDI number; -1 unknown.
	uint64_t frames;     //!< The frames stored.
	uint64_t start, end; //!< The frames played.
	uint64_t loop_start, loop_end;  //!< Equal where loop is SYFS_NONE.
	syfs_key loop;       //!< SYFS_NONE, SYFS_LOOP_FORWARD or SYFS_LOOP_ALTERNATE.
	int reverse;         //!< Played from the end down to the start.
	int mode;            //!< The format's own loop mode code.
};

inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t le24(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16); }
inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
inline uint32_t be24(const uint8_t *p) { return uint32_t(p[0] << 16 | p[1] << 8 | p[2]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1] << 16 | p[2] << 8 | p[3]); }

//! The disc's 2048-byte user data.
/*!
 * From a plain image or one of raw 2352-byte sectors (a .bin: mode 1 or mode 2 form 1).
 */
class Image {
public:
	explicit Image(const syfs_io &io);
	//! Reads n bytes at offset of the user data; false where any is missing.
	bool read(uint64_t offset, void *buf, size_t n) const;
	std::vector<uint8_t> read(uint64_t offset, size_t n) const;
	uint64_t size() const { return stores_.top->size(); }
	bool raw() const { return stores_.raw; }
	//! Whether the image is in a VHD, read through its sectors.
	bool vhd() const { return stores_.vhd; }
	//! Whether the image is a SmartMedia card's, read through its logical sectors.
	bool card() const { return stores_.card; }

private:
	Stores stores_;
};

//! An opened image: its label and entries.
/*!
 * Read by its format's subclass, which also answers what an entry uses and a sample's header.
 */
class Medium {
public:
	Medium(const syfs_io &io, syfs_key format) : image(io), format(format) {}
	virtual ~Medium() = default;
	Medium(const Medium &) = delete;
	Medium &operator=(const Medium &) = delete;

	//! What tells the image apart, read cheaply: its properties, and its label where that needs no listing.
	/*!
	 * Called once, before list.  A reader that sets the label here sets label_known.
	 */
	virtual void identify() {}
	//! The label and the entries read from the image.
	virtual void list() {}
	//! The entries entry i uses, by index, each once in the order first used.
	virtual std::vector<size_t> refs(size_t) const { return {}; }
	//! Entry i's wave and how it plays; false where it is no sample or its header cannot be read.
	virtual bool sample(size_t, syfs::Sample &) const { return false; }
	//! Frames of entry i's wave from frame first, its header s, decoded to 16 bits; the count read.
	/*!
	 * The count asked for is within s.frames.
	 */
	virtual size_t wave(size_t, const syfs::Sample &, uint64_t, int16_t *, size_t) const { return 0; }
	//! The sample entry i sounds at key (0 to 127), or syfs::NONE; here a sample, tone or file with a header itself.
	virtual size_t sample_at(size_t i, int key) const;

	const Image image;
	const syfs_key format;
	syfs_key medium = SYFS_NONE;
	std::string label;
	bool label_known = false;  //!< The label is identify's, not list's.
	std::vector<syfs::Entry> entries;

	//! A property of the image's own beside its format, medium and label.
	struct Prop {
		syfs_key key;
		bool text;       //!< A string, else an integer.
		int64_t number;
		std::string string;
	};
	std::vector<Prop> props;

protected:
	//! Where each entry's record or data starts in the image, beside entries.
	std::vector<uint64_t> where;

	size_t add(syfs_key kind, syfs_key format, uint32_t number, size_t parent, std::string_view name,
		uint64_t bytes, uint64_t at);
	void tag(size_t i, std::string_view t);
	//! Sets a property of the image's.
	void set(syfs_key key, int64_t value) { props.push_back({ key, false, value, {} }); }
	void set(syfs_key key, std::string_view value) { props.push_back({ key, true, 0, std::string(value) }); }
	//! The entry of a kind with the number under parent, or syfs::NONE.
	size_t child(size_t parent, syfs_key kind, uint32_t number) const;
};

//! Reads n 16-bit little-endian values at offset of the image into out; the count read, short where the image ends.
size_t read_words(const Image &im, uint64_t offset, int16_t *out, size_t n);

//! A sample's header as a format's sample() starts from: no key, one channel of 16 bits.
syfs::Sample blank_sample();

//! A sample's points kept within its frames, the loop within the frames played.
void clamp_points(syfs::Sample &s);

// Each format is a Medium subclass, in its maker's directory and namespace (src/roland, src/akai, src/emu), or here
// for ISO 9660, with two static members: probe, whether an image is one (the format, else SYFS_NONE), and open,
// an image of it opened, not yet listed.

std::string trimmed(std::string_view s);

}
