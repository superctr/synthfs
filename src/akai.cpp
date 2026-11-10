// Copyright 2026 Ian Karlsson
// The AKAI S1000 and S3000 partitions (docs/formats.md, "AKAI")
#include "internal.hpp"

namespace scd::akai {

constexpr uint64_t BLOCK = 8192;
constexpr int PARTITIONS = 26;
constexpr uint32_t VOLUMES_AT = 0xCA, VOLUMES = 100, FAT_AT = 0x70A;
constexpr uint16_t BLOCKS_MIN = 4, BLOCKS_MAX = 0x1E00;

// what every partition header holds after its size
const uint8_t partition_header[196] = {
	0x00, 0x00, 0x05, 0x0D, 0x0A, 0x1A, 0x0F, 0x27, 0x14, 0x34, 0x19, 0x41, 0x1E, 0x4E, 0x23, 0x5B,
	0x28, 0x68, 0x2D, 0x75, 0x32, 0x82, 0x37, 0x8F, 0x3C, 0x9C, 0x41, 0xA9, 0x46, 0xB6, 0x4B, 0xC3,
	0x50, 0xD0, 0x55, 0xDD, 0x5A, 0xEA, 0x5F, 0xF7, 0x64, 0x04, 0x69, 0x11, 0x6E, 0x1E, 0x73, 0x2B,
	0x78, 0x38, 0x7D, 0x45, 0x82, 0x52, 0x87, 0x5F, 0x8C, 0x6C, 0x91, 0x79, 0x96, 0x86, 0x9B, 0x93,
	0xA0, 0xA0, 0xA5, 0xAD, 0xAA, 0xBA, 0xAF, 0xC7, 0xB4, 0xD4, 0xB9, 0xE1, 0xBE, 0xEE, 0xC3, 0xFB,
	0xC8, 0x08, 0xCD, 0x15, 0xD2, 0x22, 0xD7, 0x2F, 0xDC, 0x3C, 0xE1, 0x49, 0xE6, 0x56, 0xEB, 0x63,
	0xF0, 0x70, 0xF5, 0x7D, 0xFA, 0x8A, 0xFF, 0x97, 0x04, 0xA5, 0x09, 0xB2, 0x0E, 0xBF, 0x13, 0xCC,
	0x18, 0xD9, 0x1D, 0xE6, 0x22, 0xF3, 0x27, 0x00, 0x2C, 0x0D, 0x31, 0x1A, 0x36, 0x27, 0x3B, 0x34,
	0x40, 0x41, 0x45, 0x4E, 0x4A, 0x5B, 0x4F, 0x68, 0x54, 0x75, 0x59, 0x82, 0x5E, 0x8F, 0x63, 0x9C,
	0x68, 0xA9, 0x6D, 0xB6, 0x72, 0xC3, 0x77, 0xD0, 0x7C, 0xDD, 0x81, 0xEA, 0x86, 0xF7, 0x8B, 0x04,
	0x90, 0x11, 0x95, 0x1E, 0x9A, 0x2B, 0x9F, 0x38, 0xA4, 0x45, 0xA9, 0x52, 0xAE, 0x5F, 0xB3, 0x6C,
	0xB8, 0x79, 0xBD, 0x86, 0xC2, 0x93, 0xC7, 0xA0, 0xCC, 0xAD, 0xD1, 0xBA, 0xD6, 0xC7, 0xDB, 0xD4,
	0xE0, 0xE1, 0xE5, 0xEE
};

constexpr char charset[] = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";

struct Partition {
	uint64_t base;
	uint16_t blocks;
};

static std::vector<Partition> partitions(const Image &im)
{
	std::vector<Partition> out;
	uint64_t pos = 0;
	uint8_t h[198];
	while (out.size() < PARTITIONS && im.read(pos, h, sizeof h)) {
		uint16_t blocks = le16(h);
		if (blocks < BLOCKS_MIN || blocks > BLOCKS_MAX || memcmp(h + 2, partition_header, sizeof partition_header))
			break;
		out.push_back({ pos, blocks });
		pos += blocks * BLOCK;
	}
	return out;
}

bool probe(const Image &im)
{
	return !partitions(im).empty();
}

static std::string name_of(const uint8_t *p)
{
	std::string s;
	for (int i = 0; i < 12; i++)
		s += p[i] < sizeof charset - 1 ? charset[p[i]] : '?';
	return s;
}

static samplecd_kind kind_of(uint8_t type)
{
	switch (type & 0x7F) {
	case 'p': return SAMPLECD_PROGRAM;
	case 's': return SAMPLECD_SAMPLE;
	default: return SAMPLECD_OTHER;
	}
}

void list(Disc &d)
{
	auto parts = partitions(d.image);
	for (size_t p = 0; p < parts.size(); p++) {
		const Partition &P = parts[p];
		auto h = d.image.read(P.base, FAT_AT + 2 * P.blocks);
		if (h.empty())
			continue;
		size_t pi = d.add(SAMPLECD_PARTITION, uint32_t(p), SAMPLECD_NONE, std::string(1, char('A' + p)), P.blocks * BLOCK, 0, P.base);
		uint32_t any = 0;
		for (uint32_t v = 0; v < VOLUMES; v++) {
			const uint8_t *e = &h[VOLUMES_AT + 16 * v];
			uint8_t type = e[12];
			uint16_t start = le16(e + 14);
			if (!type || start >= P.blocks)
				continue;
			// an S1000 volume's directory is 126 entries in its block; the S3000's 510 run on into the block its FAT names
			bool s1000 = type == 1;
			uint32_t machines = s1000 ? SAMPLECD_S760 | SAMPLECD_XV5080 : SAMPLECD_XV5080;
			std::vector<uint8_t> dir = d.image.read(P.base + start * BLOCK, BLOCK);
			if (dir.empty())
				continue;
			uint32_t count = 126;
			if (!s1000) {
				uint16_t next = le16(&h[FAT_AT + 2 * start]);
				std::vector<uint8_t> more;
				if ((next & 0xC000) != 0xC000 && (next & 0x3FFF) < P.blocks)
					more = d.image.read(P.base + (next & 0x3FFF) * BLOCK, BLOCK);
				dir.insert(dir.end(), more.begin(), more.end());
				count = std::min<uint32_t>(510, uint32_t(dir.size() / 24));
			}
			size_t vi = d.add(SAMPLECD_VOLUME, v, pi, name_of(e), 0, machines, P.base + start * BLOCK);
			any |= machines;
			uint64_t bytes = 0;
			for (uint32_t k = 0; k < count; k++) {
				const uint8_t *f = &dir[24 * k];
				if (!f[16])
					continue;
				uint64_t at = P.base + le16(f + 20) * BLOCK;
				d.add(kind_of(f[16]), k, vi, name_of(f), le24(f + 17), machines, at);
				bytes += le24(f + 17);
			}
			d.entries[vi].bytes = bytes;
		}
		d.entries[pi].machines = any;
	}
}

bool sample(const Disc &d, size_t i, samplecd_sample &out)
{
	if (d.entries[i].kind != SAMPLECD_SAMPLE)
		return false;
	uint8_t h[0x8C];
	if (!d.image.read(d.where[i], h, sizeof h) || h[0] != 3)
		return false;
	out.rate = le16(h + 0x8A);
	out.key = h[2] <= 127 ? h[2] : -1;
	out.frames = le32(h + 0x1A);
	return true;
}

}
