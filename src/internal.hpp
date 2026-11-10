// Copyright 2026 Ian Karlsson
#pragma once

#include <samplerfs/samplerfs.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sfs {

inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t le24(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16); }
inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
inline uint32_t be24(const uint8_t *p) { return uint32_t(p[0] << 16 | p[1] << 8 | p[2]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1] << 16 | p[2] << 8 | p[3]); }

// The disc's 2048-byte user data, from a plain image or one of raw 2352-byte sectors (a .bin: mode 1 or mode 2 form 1)
class Image {
public:
	explicit Image(const samplerfs_io &io);
	// n bytes at offset of the user data; false where any is missing
	bool read(uint64_t offset, void *buf, size_t n) const;
	std::vector<uint8_t> read(uint64_t offset, size_t n) const;
	uint64_t size() const { return size_; }
	bool raw() const { return raw_; }

private:
	samplerfs_io io_;
	bool raw_ = false;
	unsigned data_at_ = 0;
	uint64_t size_ = 0;
};

// An opened image: its label and entries, read by its format's subclass, which also answers what an entry uses and
// a sample's header
class Medium {
public:
	Medium(const samplerfs_io &io, samplerfs_format format) : image(io), format(format) {}
	virtual ~Medium() = default;
	Medium(const Medium &) = delete;
	Medium &operator=(const Medium &) = delete;

	// the label and the entries read from the image
	virtual void list() {}
	// the entries entry i uses, by index, each once in the order first used
	virtual std::vector<size_t> refs(size_t) const { return {}; }
	// entry i's wave and how it plays; false where it is no sample or its header cannot be read
	virtual bool sample(size_t, samplerfs_sample &) const { return false; }

	const Image image;
	const samplerfs_format format;
	samplerfs_medium medium = SAMPLERFS_MEDIUM_UNKNOWN;
	std::string label;
	std::vector<samplerfs_entry> entries;

protected:
	// where each entry's record or data starts in the image, beside entries
	std::vector<uint64_t> where;

	size_t add(samplerfs_kind kind, samplerfs_format format, uint32_t number, size_t parent, std::string_view name,
		uint64_t bytes, uint64_t at);
	void tag(size_t i, std::string_view t);
	// the entry of a kind with the number under parent, or SAMPLERFS_NONE
	size_t child(size_t parent, samplerfs_kind kind, uint32_t number) const;
};

// a sample's points kept within its frames, the loop within the frames played
void clamp_points(samplerfs_sample &s);

// Each format is a Medium subclass, in its maker's directory and namespace (src/roland, src/akai, src/emu), or here
// for ISO 9660, with two static members: probe, whether an image is one (the format, else SAMPLERFS_UNKNOWN), and open,
// an image of it opened, not yet listed.

std::string trimmed(std::string_view s);

}
