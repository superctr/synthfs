// Copyright 2026 Ian Karlsson
// The AKAI S1000 and S3000 partitions, and their floppies: one volume each (docs/formats.md, "AKAI")
#include "akai.hpp"

namespace sfs::akai {

namespace {

constexpr uint64_t BLOCK = 8192;
constexpr int PARTITIONS = 26;
constexpr uint32_t VOLUMES_AT = 0xCA, VOLUMES = 100, FAT_AT = 0x70A;
constexpr uint16_t BLOCKS_MIN = 4, BLOCKS_MAX = 0x1E00;
constexpr size_t MULTI_PARTS_AT = 0x400, MULTI_PARTS = 16, MULTI_PART = 0xC0;
constexpr uint64_t FLOPPY_BLOCK = 1024, FLOPPY_FAT = 0x600, FLOPPY_DD = 819200, FLOPPY_HD = 1638400;
constexpr uint32_t FLOPPY_FILES = 64;

// the blocks the directory, the FAT and the name take: four on a 2DD floppy, five on a 2HD one
uint32_t system_blocks(uint64_t size)
{
	return size == FLOPPY_HD ? 5 : 4;
}

constexpr char charset[] = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";

struct Partition {
	uint64_t base;
	uint16_t blocks;
};

std::vector<Partition> partitions(const Image &im)
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

std::string name_of(const uint8_t *p)
{
	std::string s;
	for (int i = 0; i < 12; i++)
		s += p[i] < sizeof charset - 1 ? charset[p[i]] : '?';
	return s;
}

samplerfs_kind kind_of(uint8_t type)
{
	switch (type & 0x7F) {
	case 'p': return SAMPLERFS_PROGRAM;
	case 's': return SAMPLERFS_SAMPLE;
	case 'm': return SAMPLERFS_MULTI;
	default: return SAMPLERFS_OTHER;
	}
}

// a volume's type: 1 an S1000's; 3 and 7 an S3000's
samplerfs_format format_of(uint8_t type)
{
	return type == 1 ? SAMPLERFS_AKAI_S1000 : SAMPLERFS_AKAI_S3000;
}

}

samplerfs_format S1000Disk::probe(const Image &im)
{
	auto parts = partitions(im);
	if (parts.empty())
		return SAMPLERFS_UNKNOWN;
	for (const Partition &P : parts) {
		uint8_t h[VOLUMES_AT + 16 * VOLUMES];
		if (!im.read(P.base, h, sizeof h))
			continue;
		for (uint32_t v = 0; v < VOLUMES; v++) {
			uint8_t type = h[VOLUMES_AT + 16 * v + 12];
			if (type && format_of(type) == SAMPLERFS_AKAI_S3000)
				return SAMPLERFS_AKAI_S3000;
		}
	}
	return SAMPLERFS_AKAI_S1000;
}

// an AKAI S1000 / S3000 disk
// floppy: 1024-byte blocks, the FAT at 0x600, the system's blocks marked 0x4000
samplerfs_format S1000Floppy::probe(const Image &im)
{
	uint8_t fat[2 * 5];
	if ((im.size() != FLOPPY_DD && im.size() != FLOPPY_HD) || !im.read(FLOPPY_FAT, fat, sizeof fat))
		return SAMPLERFS_UNKNOWN;
	for (uint32_t b = 0; b < system_blocks(im.size()); b++)
		if (le16(fat + 2 * b) != 0x4000)
			return SAMPLERFS_UNKNOWN;
	uint8_t dir[24 * FLOPPY_FILES];
	if (!im.read(0, dir, sizeof dir))
		return SAMPLERFS_UNKNOWN;
	for (uint32_t k = 0; k < FLOPPY_FILES; k++) {
		uint8_t t = dir[24 * k + 16];
		if (t == 0xF0 || t == 0xF3 || t == 0xED)
			return SAMPLERFS_AKAI_S3000;
	}
	return SAMPLERFS_AKAI_S1000;
}

void S1000Disk::list()
{
	auto parts = partitions(image);
	for (size_t p = 0; p < parts.size(); p++) {
		const Partition &P = parts[p];
		auto h = image.read(P.base, FAT_AT + 2 * P.blocks);
		if (h.empty())
			continue;
		size_t pi = add(SAMPLERFS_PARTITION, SAMPLERFS_AKAI_S1000, uint32_t(p), SAMPLERFS_NONE, std::string(1, char('A' + p)),
			P.blocks * BLOCK, P.base);
		stores_.push_back({ P.base, BLOCK, P.base + FAT_AT, P.blocks });
		for (uint32_t v = 0; v < VOLUMES; v++) {
			const uint8_t *e = &h[VOLUMES_AT + 16 * v];
			uint8_t type = e[12];
			uint16_t start = le16(e + 14);
			if (!type || start >= P.blocks)
				continue;
			// an S1000 volume's directory is 126 entries in its block; the S3000's 510 run on into the block its FAT names
			samplerfs_format fmt = format_of(type);
			std::vector<uint8_t> dir = image.read(P.base + start * BLOCK, BLOCK);
			if (dir.empty())
				continue;
			uint32_t count = 126;
			if (fmt == SAMPLERFS_AKAI_S3000) {
				uint16_t next = le16(&h[FAT_AT + 2 * start]);
				std::vector<uint8_t> more;
				if ((next & 0xC000) != 0xC000 && (next & 0x3FFF) < P.blocks)
					more = image.read(P.base + (next & 0x3FFF) * BLOCK, BLOCK);
				dir.insert(dir.end(), more.begin(), more.end());
				count = std::min<uint32_t>(510, uint32_t(dir.size() / 24));
				entries[pi].format = SAMPLERFS_AKAI_S3000;
			}
			size_t vi = add(SAMPLERFS_VOLUME, fmt, v, pi, name_of(e), 0, P.base + start * BLOCK);
			volume_store_.emplace_back(vi, stores_.size() - 1);
			uint64_t bytes = 0;
			for (uint32_t k = 0; k < count; k++) {
				const uint8_t *f = &dir[24 * k];
				if (!f[16])
					continue;
				size_t fi = add(kind_of(f[16]), fmt, k, vi, name_of(f), le24(f + 17), P.base + le16(f + 20) * BLOCK);
				tag(fi, std::string(1, char(f[16] & 0x7F)));
				bytes += le24(f + 17);
			}
			entries[vi].bytes = bytes;
		}
	}
}

// a floppy: one volume, its 64 files' directory at 0, the FAT at 0x600 (a halfword a block) and the volume's name
// after it
void S1000Floppy::list()
{
	uint32_t blocks = uint32_t(image.size() / FLOPPY_BLOCK);
	uint8_t name[12];
	auto dir = image.read(0, 24 * FLOPPY_FILES);
	if (dir.empty() || !image.read(FLOPPY_FAT + 2 * blocks, name, sizeof name))
		return;
	label = trimmed(name_of(name));
	stores_.push_back({ 0, FLOPPY_BLOCK, FLOPPY_FAT, blocks });
	size_t vi = add(SAMPLERFS_VOLUME, format, 0, SAMPLERFS_NONE, label, 0, 0);
	volume_store_.emplace_back(vi, 0);
	uint64_t bytes = 0;
	for (uint32_t k = 0; k < FLOPPY_FILES; k++) {
		const uint8_t *f = &dir[24 * k];
		uint16_t start = le16(f + 20);
		if (!f[16] || start < system_blocks(image.size()) || start >= blocks)
			continue;
		size_t fi = add(kind_of(f[16]), format, k, vi, name_of(f), le24(f + 17), start * FLOPPY_BLOCK);
		char t = char(f[16] & 0x7F);
		if (t >= 'a' && t <= 'z')
			tag(fi, std::string(1, t));
		else {
			char hex[4];
			snprintf(hex, sizeof hex, "%02X", f[16]);
			tag(fi, hex);
		}
		bytes += le24(f + 17);
	}
	entries[vi].bytes = bytes;
}

// file i's first max bytes, block by block along its volume's FAT
std::vector<uint8_t> S1000::read_file(size_t i, uint64_t max) const
{
	const samplerfs_entry &e = entries[i];
	const Store *s = nullptr;
	for (auto [v, k] : volume_store_)
		if (v == e.parent)
			s = &stores_[k];
	if (!s)
		return {};
	uint64_t left = std::min(max, e.bytes), b = (where[i] - s->base) / s->block;
	std::vector<uint8_t> out;
	while (left && b && b < s->blocks) {
		auto chunk = image.read(s->base + b * s->block, size_t(std::min(left, s->block)));
		if (chunk.empty())
			break;
		out.insert(out.end(), chunk.begin(), chunk.end());
		left -= chunk.size();
		uint8_t next[2];
		if (!left || !image.read(s->fat + 2 * b, next, 2))
			break;
		b = le16(next);
	}
	return out;
}

// the entry of that kind and name in volume vol, or SAMPLERFS_NONE
size_t S1000::in_volume(size_t vol, samplerfs_kind kind, const std::string &name) const
{
	for (size_t j = vol + 1; j < entries.size() && entries[j].parent == vol; j++)
		if (entries[j].kind == kind && name == entries[j].name)
			return j;
	return SAMPLERFS_NONE;
}

namespace {

void add_ref(std::vector<size_t> &out, size_t j)
{
	if (j != SAMPLERFS_NONE && std::find(out.begin(), out.end(), j) == out.end())
		out.push_back(j);
}

}

// a program: a header, then its keygroups (150 bytes each on an S1000, 192 on an S3000), the count at +42; each
// keygroup's four velocity zones name their samples at +34, +58, +82, +106.  A multi: 16 parts of 0xC0 bytes from
// +0x400, each naming its program at +3.  Names are found in the same volume.
std::vector<size_t> S1000::refs(size_t i) const
{
	const samplerfs_entry &e = entries[i];
	std::vector<size_t> out;
	if (e.kind == SAMPLERFS_MULTI) {
		auto m = read_file(i, MULTI_PARTS_AT + MULTI_PARTS * MULTI_PART);
		for (size_t k = 0; k < MULTI_PARTS && MULTI_PARTS_AT + (k + 1) * MULTI_PART <= m.size(); k++) {
			std::string name = trimmed(name_of(&m[MULTI_PARTS_AT + k * MULTI_PART + 3]));
			if (!name.empty())
				add_ref(out, in_volume(e.parent, SAMPLERFS_PROGRAM, name));
		}
		return out;
	}
	if (e.kind != SAMPLERFS_PROGRAM)
		return {};
	size_t block = e.format == SAMPLERFS_AKAI_S1000 ? 150 : 192;
	auto pr = read_file(i, block * 100);
	if (pr.size() < block || pr[0] != 1)
		return {};
	for (size_t k = 0; k < pr[42] && (k + 2) * block <= pr.size(); k++) {
		const uint8_t *kg = &pr[(k + 1) * block];
		if (kg[0] != 2)
			break;
		for (int z = 0; z < 4; z++) {
			std::string name = trimmed(name_of(kg + 34 + 24 * z));
			if (!name.empty())
				add_ref(out, in_volume(e.parent, SAMPLERFS_SAMPLE, name));
		}
	}
	return out;
}

// a sample's header: 3 at +0, the key at +2, the loops in use at +0x10, the loop mode at +0x13, the frames, start and
// end at +0x1A, +0x1E, +0x22, eight loops of 12 bytes from +0x26 (the point a loop ends at, then its length at +6),
// the rate at +0x8A
bool S1000::sample(size_t i, samplerfs_sample &out) const
{
	if (entries[i].kind != SAMPLERFS_SAMPLE)
		return false;
	auto h = read_file(i, 0x8C);
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
		out.loop = SAMPLERFS_LOOP_FORWARD;
		out.loop_start = at - length;
		out.loop_end = at;
	}
	return true;
}

std::unique_ptr<Medium> S1000Disk::open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<S1000Disk>(io, format);
}

std::unique_ptr<Medium> S1000Floppy::open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<S1000Floppy>(io, format);
}

}
