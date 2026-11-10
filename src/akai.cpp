// Copyright 2026 Ian Karlsson
// The AKAI S1000 and S3000 partitions (docs/formats.md, "AKAI")
#include "internal.hpp"

namespace sfs::akai {

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

static samplerfs_kind kind_of(uint8_t type)
{
	switch (type & 0x7F) {
	case 'p': return SAMPLERFS_PROGRAM;
	case 's': return SAMPLERFS_SAMPLE;
	case 'm': return SAMPLERFS_MULTI;
	default: return SAMPLERFS_OTHER;
	}
}

// a volume's type: 1 an S1000's; 3 and 7 an S3000's
static samplerfs_format format_of(uint8_t type)
{
	return type == 1 ? SAMPLERFS_AKAI_S1000 : SAMPLERFS_AKAI_S3000;
}

samplerfs_format probe(const Image &im)
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
class AkaiMedium final : public Medium {
public:
	using Medium::Medium;
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;

private:
	std::vector<uint8_t> read_file(size_t i, uint64_t max) const;
	size_t in_volume(size_t vol, samplerfs_kind kind, const std::string &name) const;
};

void AkaiMedium::list()
{
	auto parts = partitions(image);
	for (size_t p = 0; p < parts.size(); p++) {
		const Partition &P = parts[p];
		auto h = image.read(P.base, FAT_AT + 2 * P.blocks);
		if (h.empty())
			continue;
		size_t pi = add(SAMPLERFS_PARTITION, SAMPLERFS_AKAI_S1000, uint32_t(p), SAMPLERFS_NONE, std::string(1, char('A' + p)),
			P.blocks * BLOCK, P.base);
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

// file i's first max bytes, block by block along its partition's FAT
std::vector<uint8_t> AkaiMedium::read_file(size_t i, uint64_t max) const
{
	const samplerfs_entry &e = entries[i];
	if (e.parent == SAMPLERFS_NONE || entries[e.parent].parent == SAMPLERFS_NONE)
		return {};
	size_t part = entries[e.parent].parent;
	uint64_t base = where[part], blocks = entries[part].bytes / BLOCK;
	uint64_t left = std::min(max, e.bytes), b = (where[i] - base) / BLOCK;
	std::vector<uint8_t> out;
	while (left && b && b < blocks) {
		auto chunk = image.read(base + b * BLOCK, size_t(std::min(left, BLOCK)));
		if (chunk.empty())
			break;
		out.insert(out.end(), chunk.begin(), chunk.end());
		left -= chunk.size();
		uint8_t next[2];
		if (!left || !image.read(base + FAT_AT + 2 * b, next, 2))
			break;
		b = le16(next);
	}
	return out;
}

// the entry of that kind and name in volume vol, or SAMPLERFS_NONE
size_t AkaiMedium::in_volume(size_t vol, samplerfs_kind kind, const std::string &name) const
{
	for (size_t j = vol + 1; j < entries.size() && entries[j].parent == vol; j++)
		if (entries[j].kind == kind && name == entries[j].name)
			return j;
	return SAMPLERFS_NONE;
}

static void add_ref(std::vector<size_t> &out, size_t j)
{
	if (j != SAMPLERFS_NONE && std::find(out.begin(), out.end(), j) == out.end())
		out.push_back(j);
}

// a program: a header, then its keygroups (150 bytes each on an S1000, 192 on an S3000), the count at +42; each
// keygroup's four velocity zones name their samples at +34, +58, +82, +106.  A multi: 16 parts of 0xC0 bytes from
// +0x400, each naming its program at +3.  Names are found in the same volume.
std::vector<size_t> AkaiMedium::refs(size_t i) const
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
bool AkaiMedium::sample(size_t i, samplerfs_sample &out) const
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

std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<AkaiMedium>(io, format);
}

}
