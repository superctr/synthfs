// Copyright 2026 Ian Karlsson
// The Roland S-550 / W-30 SCSI format: a directory of areas, each a disk's patches, tones and waves (docs/formats.md, "S-550")
#include "internal.hpp"

namespace sfs::s550 {

constexpr uint64_t BLOCK = 512;
constexpr uint32_t DIRECTORY = 1, DIRECTORY_END = 0x100, ENTRY = 64, COUNT_AT = 0x139;
constexpr uint32_t PARAMETER_BLOCKS = 18;
constexpr uint32_t PATCHES = 16, PATCH = 256, TONES = 32, TONES_AT = 0x1200, TONE = 128;
constexpr uint32_t SEGMENT_WORDS = 12288, KEYS = 109, LAST_TONE = 31;

samplerfs_format probe(const Image &im)
{
	uint8_t s0[16];
	return im.read(0, s0, sizeof s0) && !memcmp(s0, "* ROLAND S-550 *", 16) ? SAMPLERFS_S550 : SAMPLERFS_UNKNOWN;
}

static std::string text(const uint8_t *p, size_t n)
{
	std::string s;
	for (size_t i = 0; i < n; i++)
		s += p[i] >= 32 && p[i] < 127 ? char(p[i]) : ' ';
	return s;
}

static bool blank(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != ' ' && p[i] != 0)
			return false;
	return true;
}

// an S-550 / W-30 SCSI disk
class S550Medium final : public Medium {
public:
	using Medium::Medium;
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, samplerfs_sample &out) const override;
};

void S550Medium::list()
{
	auto s0 = image.read(0, BLOCK);
	if (s0.empty())
		return;
	label = trimmed(text(&s0[0x20], 32));
	uint64_t room = image.size() > DIRECTORY * BLOCK ? image.size() - DIRECTORY * BLOCK : 0;
	auto dir = image.read(DIRECTORY * BLOCK, size_t(std::min<uint64_t>((DIRECTORY_END - DIRECTORY) * BLOCK, room)));
	uint64_t blocks = image.size() / BLOCK;
	// the areas' count at +0x139 (big-endian, 0 none); the directory ends there or at an entry starting 0xFF
	uint32_t count = uint32_t(s0[COUNT_AT]) << 8 | s0[COUNT_AT + 1];
	if (!count)
		count = UINT32_MAX;
	for (uint32_t n = 0; n < dir.size() / ENTRY && n < count; n++) {
		const uint8_t *e = &dir[n * ENTRY];
		if (e[0] == 0xFF)
			break;
		uint32_t start = be32(e + 0x30), length = be32(e + 0x34);
		if (length < PARAMETER_BLOCKS || start + uint64_t(length) > blocks)
			continue;
		size_t ai = add(SAMPLERFS_AREA, SAMPLERFS_S550, n, SAMPLERFS_NONE, text(e, 32), length * BLOCK, start * BLOCK);
		tag(ai, std::to_string(e[0x39]));
		auto par = image.read(start * BLOCK, PARAMETER_BLOCKS * BLOCK);
		if (par.empty())
			continue;
		for (uint32_t k = 0; k < PATCHES; k++)
			if (!blank(&par[k * PATCH], 12))
				add(SAMPLERFS_PATCH, SAMPLERFS_S550, k, ai, text(&par[k * PATCH], 12), 0, start * BLOCK + k * PATCH);
		for (uint32_t k = 0; k < TONES; k++) {
			const uint8_t *t = &par[TONES_AT + k * TONE];
			if (!blank(t, 8))
				add(SAMPLERFS_TONE, SAMPLERFS_S550, k, ai, text(t, 8), uint64_t(t[0x0F]) * SEGMENT_WORDS * 2,
					start * BLOCK + TONES_AT + k * TONE);
		}
	}
}

// a patch's key maps: the first at +0x1A, the second at +0x87 where the key mode (+0x0F) is 1 to 3; a tone number a key
std::vector<size_t> S550Medium::refs(size_t i) const
{
	const samplerfs_entry &e = entries[i];
	if (e.kind != SAMPLERFS_PATCH)
		return {};
	auto rec = image.read(where[i], PATCH);
	if (rec.empty())
		return {};
	std::vector<size_t> out;
	for (int map = 0; map < (rec[0x0F] >= 1 && rec[0x0F] <= 3 ? 2 : 1); map++)
		for (uint32_t k = 0; k < KEYS; k++) {
			uint8_t tone = rec[(map ? 0x87 : 0x1A) + k];
			if (tone > LAST_TONE)
				continue;
			size_t j = child(e.parent, SAMPLERFS_TONE, tone);
			if (j != SAMPLERFS_NONE && std::find(out.begin(), out.end(), j) == out.end())
				out.push_back(j);
		}
	return out;
}

// a tone: 30 kHz where +0x0B is 0, else 15 kHz; the original key at +0x0C; its segments at +0x0F; its start, end and
// loop point at +0x10, +0x13 and +0x16, 24-bit big-endian words from its first segment; the loop mode at +0x19
bool S550Medium::sample(size_t i, samplerfs_sample &out) const
{
	if (entries[i].kind != SAMPLERFS_TONE)
		return false;
	uint8_t t[0x1A];
	if (!image.read(where[i], t, sizeof t))
		return false;
	out.rate = t[0x0B] ? 15000 : 30000;
	out.key = t[0x0C] <= 127 ? t[0x0C] : -1;
	out.frames = uint64_t(t[0x0F]) * SEGMENT_WORDS;
	out.start = be24(t + 0x10);
	out.end = be24(t + 0x13) + 1;
	out.loop_start = be24(t + 0x16);
	out.loop_end = out.end;
	out.mode = t[0x19];
	switch (out.mode) {
	case 0: out.loop = SAMPLERFS_LOOP_FORWARD; break;
	case 1: out.loop = SAMPLERFS_LOOP_ALTERNATE; break;
	case 3: out.reverse = 1; break;
	default: break;
	}
	return true;
}

std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<S550Medium>(io, format);
}

}
