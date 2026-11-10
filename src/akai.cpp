// Copyright 2026 Ian Karlsson
// The AKAI S1000 and S3000 partitions (docs/formats.md, "AKAI")
#include "internal.hpp"

namespace scd::akai {

constexpr uint64_t BLOCK = 8192;
constexpr int PARTITIONS = 26;
constexpr uint32_t VOLUMES_AT = 0xCA, VOLUMES = 100, FAT_AT = 0x70A;
constexpr uint16_t BLOCKS_MIN = 4, BLOCKS_MAX = 0x1E00;
constexpr size_t MULTI_PARTS_AT = 0x400, MULTI_PARTS = 16, MULTI_PART = 0xC0;

constexpr char charset[] = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";

struct Partition {
	uint64_t base;
	uint16_t blocks;
};

static std::vector<Partition> partitions(const Image &im)
{
	std::vector<Partition> out;
	uint64_t pos = 0;
	uint8_t h[2 + 2 * HEADER_WORDS];
	while (out.size() < PARTITIONS && im.read(pos, h, sizeof h)) {
		uint16_t blocks = le16(h);
		if (blocks < BLOCKS_MIN || blocks > BLOCKS_MAX)
			break;
		bool ok = true;
		for (uint32_t k = 0; k < HEADER_WORDS && ok; k++)
			ok = le16(h + 2 + 2 * k) == uint16_t(k * HEADER_STEP);
		if (!ok)
			break;
		out.push_back({ pos, blocks });
		pos += blocks * BLOCK;
	}
	return out;
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
	case 'm': return SAMPLECD_MULTI;
	default: return SAMPLECD_OTHER;
	}
}

// a volume's type: 1 an S1000's; 3 and 7 an S3000's
static samplecd_format format_of(uint8_t type)
{
	return type == 1 ? SAMPLECD_AKAI_S1000 : SAMPLECD_AKAI_S3000;
}

samplecd_format probe(const Image &im)
{
	auto parts = partitions(im);
	if (parts.empty())
		return SAMPLECD_UNKNOWN;
	for (const Partition &P : parts) {
		uint8_t h[VOLUMES_AT + 16 * VOLUMES];
		if (!im.read(P.base, h, sizeof h))
			continue;
		for (uint32_t v = 0; v < VOLUMES; v++) {
			uint8_t type = h[VOLUMES_AT + 16 * v + 12];
			if (type && format_of(type) == SAMPLECD_AKAI_S3000)
				return SAMPLECD_AKAI_S3000;
		}
	}
	return SAMPLECD_AKAI_S1000;
}

void list(Disc &d)
{
	auto parts = partitions(d.image);
	for (size_t p = 0; p < parts.size(); p++) {
		const Partition &P = parts[p];
		auto h = d.image.read(P.base, FAT_AT + 2 * P.blocks);
		if (h.empty())
			continue;
		size_t pi = d.add(SAMPLECD_PARTITION, SAMPLECD_AKAI_S1000, uint32_t(p), SAMPLECD_NONE, std::string(1, char('A' + p)),
			P.blocks * BLOCK, P.base);
		for (uint32_t v = 0; v < VOLUMES; v++) {
			const uint8_t *e = &h[VOLUMES_AT + 16 * v];
			uint8_t type = e[12];
			uint16_t start = le16(e + 14);
			if (!type || start >= P.blocks)
				continue;
			// an S1000 volume's directory is 126 entries in its block; the S3000's 510 run on into the block its FAT names
			samplecd_format fmt = format_of(type);
			std::vector<uint8_t> dir = d.image.read(P.base + start * BLOCK, BLOCK);
			if (dir.empty())
				continue;
			uint32_t count = 126;
			if (fmt == SAMPLECD_AKAI_S3000) {
				uint16_t next = le16(&h[FAT_AT + 2 * start]);
				std::vector<uint8_t> more;
				if ((next & 0xC000) != 0xC000 && (next & 0x3FFF) < P.blocks)
					more = d.image.read(P.base + (next & 0x3FFF) * BLOCK, BLOCK);
				dir.insert(dir.end(), more.begin(), more.end());
				count = std::min<uint32_t>(510, uint32_t(dir.size() / 24));
				d.entries[pi].format = SAMPLECD_AKAI_S3000;
			}
			size_t vi = d.add(SAMPLECD_VOLUME, fmt, v, pi, name_of(e), 0, P.base + start * BLOCK);
			uint64_t bytes = 0;
			for (uint32_t k = 0; k < count; k++) {
				const uint8_t *f = &dir[24 * k];
				if (!f[16])
					continue;
				size_t fi = d.add(kind_of(f[16]), fmt, k, vi, name_of(f), le24(f + 17), P.base + le16(f + 20) * BLOCK);
				d.tag(fi, std::string(1, char(f[16] & 0x7F)));
				bytes += le24(f + 17);
			}
			d.entries[vi].bytes = bytes;
		}
	}
}

// file i's first max bytes, block by block along its partition's FAT
static std::vector<uint8_t> read_file(const Disc &d, size_t i, uint64_t max)
{
	const samplecd_entry &e = d.entries[i];
	if (e.parent == SAMPLECD_NONE || d.entries[e.parent].parent == SAMPLECD_NONE)
		return {};
	size_t part = d.entries[e.parent].parent;
	uint64_t base = d.where[part], blocks = d.entries[part].bytes / BLOCK;
	uint64_t left = std::min(max, e.bytes), b = (d.where[i] - base) / BLOCK;
	std::vector<uint8_t> out;
	while (left && b && b < blocks) {
		auto chunk = d.image.read(base + b * BLOCK, size_t(std::min(left, BLOCK)));
		if (chunk.empty())
			break;
		out.insert(out.end(), chunk.begin(), chunk.end());
		left -= chunk.size();
		uint8_t next[2];
		if (!left || !d.image.read(base + FAT_AT + 2 * b, next, 2))
			break;
		b = le16(next);
	}
	return out;
}

// the entry of that kind and name in volume vol, or SAMPLECD_NONE
static size_t in_volume(const Disc &d, size_t vol, samplecd_kind kind, const std::string &name)
{
	for (size_t j = vol + 1; j < d.entries.size() && d.entries[j].parent == vol; j++)
		if (d.entries[j].kind == kind && name == d.entries[j].name)
			return j;
	return SAMPLECD_NONE;
}

static void add_ref(std::vector<size_t> &out, size_t j)
{
	if (j != SAMPLECD_NONE && std::find(out.begin(), out.end(), j) == out.end())
		out.push_back(j);
}

// a program: a header, then its keygroups (150 bytes each on an S1000, 192 on an S3000), the count at +42; each
// keygroup's four velocity zones name their samples at +34, +58, +82, +106.  A multi: 16 parts of 0xC0 bytes from
// +0x400, each naming its program at +3.  Names are found in the same volume.
std::vector<size_t> refs(const Disc &d, size_t i)
{
	const samplecd_entry &e = d.entries[i];
	std::vector<size_t> out;
	if (e.kind == SAMPLECD_MULTI) {
		auto m = read_file(d, i, MULTI_PARTS_AT + MULTI_PARTS * MULTI_PART);
		for (size_t k = 0; k < MULTI_PARTS && MULTI_PARTS_AT + (k + 1) * MULTI_PART <= m.size(); k++) {
			std::string name = trimmed(name_of(&m[MULTI_PARTS_AT + k * MULTI_PART + 3]));
			if (!name.empty())
				add_ref(out, in_volume(d, e.parent, SAMPLECD_PROGRAM, name));
		}
		return out;
	}
	if (e.kind != SAMPLECD_PROGRAM)
		return {};
	size_t block = e.format == SAMPLECD_AKAI_S1000 ? 150 : 192;
	auto pr = read_file(d, i, block * 100);
	if (pr.size() < block || pr[0] != 1)
		return {};
	for (size_t k = 0; k < pr[42] && (k + 2) * block <= pr.size(); k++) {
		const uint8_t *kg = &pr[(k + 1) * block];
		if (kg[0] != 2)
			break;
		for (int z = 0; z < 4; z++) {
			std::string name = trimmed(name_of(kg + 34 + 24 * z));
			if (!name.empty())
				add_ref(out, in_volume(d, e.parent, SAMPLECD_SAMPLE, name));
		}
	}
	return out;
}

// a sample's header: 3 at +0, the key at +2, the loops in use at +0x10, the loop mode at +0x13, the frames, start and
// end at +0x1A, +0x1E, +0x22, eight loops of 12 bytes from +0x26 (the point a loop ends at, then its length at +6),
// the rate at +0x8A
bool sample(const Disc &d, size_t i, samplecd_sample &out)
{
	if (d.entries[i].kind != SAMPLECD_SAMPLE)
		return false;
	auto h = read_file(d, i, 0x8C);
	if (h.size() < 0x8C || h[0] != 3)
		return false;
	out.rate = le16(&h[0x8A]);
	out.key = h[2] <= 127 ? h[2] : -1;
	out.frames = le32(&h[0x1A]);
	out.start = le32(&h[0x1E]);
	out.end = uint64_t(le32(&h[0x22])) + 1;
	out.mode = h[0x13];
	uint32_t at = le32(&h[0x26]), length = le32(&h[0x2C]);
	if (h[0x10] && out.mode != 2 && length && length <= at && at <= out.frames) {
		out.loop = SAMPLECD_LOOP_FORWARD;
		out.loop_start = at - length;
		out.loop_end = at;
	}
	return true;
}

}
