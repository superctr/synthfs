// Copyright 2026 Ian Karlsson
#pragma once

#include <samplecd/samplecd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace scd {

inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t le24(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16); }
inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

// The disc's 2048-byte user data, from a plain image or one of raw 2352-byte sectors (a .bin: mode 1 or mode 2 form 1)
class Image {
public:
	explicit Image(const samplecd_io &io);
	// n bytes at offset of the user data; false where any is missing
	bool read(uint64_t offset, void *buf, size_t n) const;
	std::vector<uint8_t> read(uint64_t offset, size_t n) const;
	uint64_t size() const { return size_; }
	bool raw() const { return raw_; }

private:
	samplecd_io io_;
	bool raw_ = false;
	unsigned data_at_ = 0;
	uint64_t size_ = 0;
};

struct Disc {
	Image image;
	samplecd_format format = SAMPLECD_UNKNOWN;
	std::string label;
	std::vector<samplecd_entry> entries;
	// where each entry's record or data starts in the image, beside entries
	std::vector<uint64_t> where;
	// an entry's index by its kind's list and number (the S-7xx lists)
	std::vector<size_t> numbered[5];

	explicit Disc(const samplecd_io &io) : image(io) {}
	size_t add(samplecd_kind kind, uint32_t number, size_t parent, std::string_view name, uint64_t bytes, uint32_t machines,
		uint64_t at);
};

// one per format: whether the image is one, its lists, what an entry uses, a sample's header
namespace s7 {
bool probe(const Image &im);
void list(Disc &d);
std::vector<size_t> refs(const Disc &d, size_t i);
bool sample(const Disc &d, size_t i, samplecd_sample &out);
}
namespace akai {
extern const uint8_t partition_header[196];
bool probe(const Image &im);
void list(Disc &d);
bool sample(const Disc &d, size_t i, samplecd_sample &out);
}
namespace iso9660 {
bool probe(const Image &im);
void list(Disc &d);
}

std::string trimmed(std::string_view s);

}
