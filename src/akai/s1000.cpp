// Copyright 2026 Ian Karlsson
// The AKAI S1000 and S3000 partitions, and their floppies: one volume each (docs/formats.md, "AKAI")
#include "akai.hpp"

namespace syfs::akai {

namespace {

constexpr uint64_t BLOCK = 8192;
constexpr int PARTITIONS = 26;
constexpr uint32_t VOLUMES_AT = 0xCA, VOLUMES = 100, FAT_AT = 0x70A;
constexpr uint16_t BLOCKS_MIN = 4, BLOCKS_MAX = 0x1E00;
constexpr size_t MULTI_PARTS_AT = 0x400, MULTI_PARTS = 16, MULTI_PART = 0xC0;
constexpr uint64_t FLOPPY_BLOCK = 1024, FLOPPY_FAT = 0x600, FLOPPY_DD = 819200, FLOPPY_HD = 1638400;
constexpr uint32_t FLOPPY_FILES = 64;
constexpr uint64_t S1000_HEADER = 150, S3000_HEADER = 192;

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

syfs_key kind_of(uint8_t type)
{
	switch (type & 0x7F) {
	case 'p': return SYFS_TYPE_PROGRAM;
	case 's': return SYFS_TYPE_SAMPLE;
	case 'm': return SYFS_TYPE_MULTI;
	default: return SYFS_TYPE_OTHER;
	}
}

// a volume's type: 1 an S1000's; 3 and 7 an S3000's
syfs_key format_of(uint8_t type)
{
	return type == 1 ? SYFS_FORMAT_AKAI_S1000 : SYFS_FORMAT_AKAI_S3000;
}

}

syfs_key S1000Disk::probe(const Image &im)
{
	auto parts = partitions(im);
	if (parts.empty())
		return SYFS_NONE;
	for (const Partition &P : parts) {
		uint8_t h[VOLUMES_AT + 16 * VOLUMES];
		if (!im.read(P.base, h, sizeof h))
			continue;
		for (uint32_t v = 0; v < VOLUMES; v++) {
			uint8_t type = h[VOLUMES_AT + 16 * v + 12];
			if (type && format_of(type) == SYFS_FORMAT_AKAI_S3000)
				return SYFS_FORMAT_AKAI_S3000;
		}
	}
	return SYFS_FORMAT_AKAI_S1000;
}

// an AKAI S1000 / S3000 disk
// floppy: 1024-byte blocks, the FAT at 0x600, the system's blocks marked 0x4000
syfs_key S1000Floppy::probe(const Image &im)
{
	uint8_t fat[2 * 5];
	if ((im.size() != FLOPPY_DD && im.size() != FLOPPY_HD) || !im.read(FLOPPY_FAT, fat, sizeof fat))
		return SYFS_NONE;
	for (uint32_t b = 0; b < system_blocks(im.size()); b++)
		if (le16(fat + 2 * b) != 0x4000)
			return SYFS_NONE;
	uint8_t dir[24 * FLOPPY_FILES];
	if (!im.read(0, dir, sizeof dir))
		return SYFS_NONE;
	for (uint32_t k = 0; k < FLOPPY_FILES; k++) {
		uint8_t t = dir[24 * k + 16];
		if (t == 0xF0 || t == 0xF3 || t == 0xED)
			return SYFS_FORMAT_AKAI_S3000;
	}
	return SYFS_FORMAT_AKAI_S1000;
}

void S1000Disk::list()
{
	auto parts = partitions(image);
	for (size_t p = 0; p < parts.size(); p++) {
		const Partition &P = parts[p];
		auto h = image.read(P.base, FAT_AT + 2 * P.blocks);
		if (h.empty())
			continue;
		size_t pi = add(SYFS_TYPE_PARTITION, SYFS_FORMAT_AKAI_S1000, uint32_t(p), syfs::NONE, std::string(1, char('A' + p)),
			P.blocks * BLOCK, P.base);
		stores_.push_back({ P.base, BLOCK, P.blocks, std::vector<uint8_t>(h.begin() + FAT_AT, h.end()) });
		for (uint32_t v = 0; v < VOLUMES; v++) {
			const uint8_t *e = &h[VOLUMES_AT + 16 * v];
			uint8_t type = e[12];
			uint16_t start = le16(e + 14);
			if (!type || start >= P.blocks)
				continue;
			// an S1000 volume's directory is 126 entries in its block; the S3000's 510 run on into the block its FAT names
			syfs_key fmt = format_of(type);
			std::vector<uint8_t> dir = image.read(P.base + start * BLOCK, BLOCK);
			if (dir.empty())
				continue;
			uint32_t count = 126;
			if (fmt == SYFS_FORMAT_AKAI_S3000) {
				uint16_t next = le16(&h[FAT_AT + 2 * start]);
				std::vector<uint8_t> more;
				if ((next & 0xC000) != 0xC000 && (next & 0x3FFF) < P.blocks)
					more = image.read(P.base + (next & 0x3FFF) * BLOCK, BLOCK);
				dir.insert(dir.end(), more.begin(), more.end());
				count = std::min<uint32_t>(510, uint32_t(dir.size() / 24));
				entries[pi].format = SYFS_FORMAT_AKAI_S3000;
			}
			size_t vi = add(SYFS_TYPE_VOLUME, fmt, v, pi, name_of(e), 0, P.base + start * BLOCK);
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
	auto fat = image.read(FLOPPY_FAT, 2 * blocks);
	if (fat.empty())
		return;
	stores_.push_back({ 0, FLOPPY_BLOCK, blocks, std::move(fat) });
	size_t vi = add(SYFS_TYPE_VOLUME, format, 0, syfs::NONE, label, 0, 0);
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

// file i's bytes from offset, block by block along its volume's FAT
size_t S1000::read_at(size_t i, uint64_t offset, uint8_t *buf, size_t n) const
{
	const syfs::Entry &e = entries[i];
	const Store *s = nullptr;
	for (const auto &[v, k] : volume_store_)
		if (v == e.parent)
			s = &stores_[k];
	if (!s || offset >= e.bytes)
		return 0;
	n = size_t(std::min<uint64_t>(n, e.bytes - offset));
	auto next = [&](uint64_t b) -> uint64_t { return 2 * b + 2 <= s->fat.size() ? le16(&s->fat[2 * b]) : 0; };
	uint64_t b = (where[i] - s->base) / s->block;
	for (uint64_t k = offset / s->block; k && b && b < s->blocks; k--)
		b = next(b);
	size_t done = 0;
	uint64_t at = offset % s->block;
	while (done < n && b && b < s->blocks) {
		size_t k = size_t(std::min<uint64_t>(n - done, s->block - at));
		if (!image.read(s->base + b * s->block + at, buf + done, k))
			break;
		done += k;
		at = 0;
		b = next(b);
	}
	return done;
}

// file i's first max bytes
std::vector<uint8_t> S1000::read_file(size_t i, uint64_t max) const
{
	std::vector<uint8_t> out(size_t(std::min(max, entries[i].bytes)));
	out.resize(read_at(i, 0, out.data(), out.size()));
	return out;
}

// the entry of that kind and name in volume vol, or syfs::NONE
size_t S1000::in_volume(size_t vol, syfs_key kind, const std::string &name) const
{
	for (size_t j = vol + 1; j < entries.size() && entries[j].parent == vol; j++)
		if (entries[j].kind == kind && name == entries[j].name)
			return j;
	return syfs::NONE;
}

namespace {

void add_ref(std::vector<size_t> &out, size_t j)
{
	if (j != syfs::NONE && std::find(out.begin(), out.end(), j) == out.end())
		out.push_back(j);
}

}

// a program: a header, then its keygroups (150 bytes each on an S1000, 192 on an S3000), the count at +42; each
// keygroup's four velocity zones name their samples at +34, +58, +82, +106.  A multi: 16 parts of 0xC0 bytes from
// +0x400, each naming its program at +3.  Names are found in the same volume.
std::vector<size_t> S1000::refs(size_t i) const
{
	const syfs::Entry &e = entries[i];
	std::vector<size_t> out;
	if (e.kind == SYFS_TYPE_MULTI) {
		auto m = read_file(i, MULTI_PARTS_AT + MULTI_PARTS * MULTI_PART);
		for (size_t k = 0; k < MULTI_PARTS && MULTI_PARTS_AT + (k + 1) * MULTI_PART <= m.size(); k++) {
			std::string name = trimmed(name_of(&m[MULTI_PARTS_AT + k * MULTI_PART + 3]));
			if (!name.empty())
				add_ref(out, in_volume(e.parent, SYFS_TYPE_PROGRAM, name));
		}
		return out;
	}
	if (e.kind != SYFS_TYPE_PROGRAM)
		return {};
	size_t block = e.format == SYFS_FORMAT_AKAI_S1000 ? S1000_HEADER : S3000_HEADER;
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
				add_ref(out, in_volume(e.parent, SYFS_TYPE_SAMPLE, name));
		}
	}
	return out;
}

// A program's key: the first keygroup whose keys (lowest at +3, highest at +4) hold it, and its first velocity
// zone's sample found in the volume
size_t S1000::sample_at(size_t i, int key) const
{
	const syfs::Entry &e = entries[i];
	if (e.kind != SYFS_TYPE_PROGRAM)
		return Medium::sample_at(i, key);
	size_t block = e.format == SYFS_FORMAT_AKAI_S1000 ? S1000_HEADER : S3000_HEADER;
	auto pr = read_file(i, block * 100);
	if (pr.size() < block || pr[0] != 1)
		return syfs::NONE;
	for (size_t k = 0; k < pr[42] && (k + 2) * block <= pr.size(); k++) {
		const uint8_t *kg = &pr[(k + 1) * block];
		if (kg[0] != 2)
			break;
		if (key < kg[3] || key > kg[4])
			continue;
		for (int z = 0; z < 4; z++) {
			std::string name = trimmed(name_of(kg + 34 + 24 * z));
			size_t s = name.empty() ? syfs::NONE : in_volume(e.parent, SYFS_TYPE_SAMPLE, name);
			if (s != syfs::NONE)
				return s;
		}
	}
	return syfs::NONE;
}

// a sample's header: 3 at +0, the key at +2, the loops in use at +0x10, the loop mode at +0x13, the frames, start and
// end at +0x1A, +0x1E, +0x22, eight loops of 12 bytes from +0x26 (the point a loop ends at, then its length at +6),
// the rate at +0x8A
bool S1000::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind != SYFS_TYPE_SAMPLE)
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
		out.loop = SYFS_LOOP_FORWARD;
		out.loop_start = at - length;
		out.loop_end = at;
	}
	return true;
}

// the wave after the header (150 bytes on an S1000, 192 on an S3000), 16-bit little-endian
size_t S1000::wave(size_t i, const syfs::Sample &, uint64_t first, int16_t *out, size_t frames) const
{
	uint64_t header = entries[i].format == SYFS_FORMAT_AKAI_S1000 ? S1000_HEADER : S3000_HEADER;
	uint8_t buf[8192];
	size_t done = 0;
	while (done < frames) {
		size_t k = std::min(frames - done, sizeof buf / 2);
		size_t got = read_at(i, header + 2 * (first + done), buf, 2 * k) / 2;
		for (size_t j = 0; j < got; j++)
			out[done + j] = int16_t(le16(buf + 2 * j));
		done += got;
		if (got < k)
			break;
	}
	return done;
}

std::unique_ptr<Medium> S1000Disk::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<S1000Disk>(io, format);
}

std::unique_ptr<Medium> S1000Floppy::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<S1000Floppy>(io, format);
}

}
