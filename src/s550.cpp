// Copyright 2026 Ian Karlsson
// The Roland S-550 / W-30 SCSI format: a directory of areas, each a disk's patches, tones and waves (docs/formats.md, "S-550")
#include "internal.hpp"

namespace scd::s550 {

constexpr uint64_t BLOCK = 512;
constexpr uint32_t DIRECTORY = 1, DIRECTORY_END = 0x100, ENTRY = 64;
constexpr uint32_t PARAMETER_BLOCKS = 18;
constexpr uint32_t PATCHES = 16, PATCH = 256, TONES = 32, TONES_AT = 0x1200, TONE = 128;
constexpr uint32_t SEGMENT_WORDS = 12288, KEYS = 109, LAST_TONE = 31;

bool probe(const Image &im)
{
	uint8_t s0[16];
	return im.read(0, s0, sizeof s0) && !memcmp(s0, "* ROLAND S-550 *", 16);
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

void list(Disc &d)
{
	auto s0 = d.image.read(0, BLOCK);
	if (s0.empty())
		return;
	d.label = trimmed(text(&s0[0x20], 32));
	uint64_t room = d.image.size() > DIRECTORY * BLOCK ? d.image.size() - DIRECTORY * BLOCK : 0;
	auto dir = d.image.read(DIRECTORY * BLOCK, size_t(std::min<uint64_t>((DIRECTORY_END - DIRECTORY) * BLOCK, room)));
	uint64_t blocks = d.image.size() / BLOCK;
	for (uint32_t n = 0; n < dir.size() / ENTRY; n++) {
		const uint8_t *e = &dir[n * ENTRY];
		if (e[0] == 0xFF)
			break;
		uint32_t start = be32(e + 0x30), length = be32(e + 0x34);
		if (length < PARAMETER_BLOCKS || start + uint64_t(length) > blocks)
			continue;
		size_t ai = d.add(SAMPLECD_AREA, SAMPLECD_S550, n, SAMPLECD_NONE, text(e, 32), length * BLOCK, start * BLOCK);
		d.tag(ai, std::to_string(e[0x39]));
		auto par = d.image.read(start * BLOCK, PARAMETER_BLOCKS * BLOCK);
		if (par.empty())
			continue;
		for (uint32_t k = 0; k < PATCHES; k++)
			if (!blank(&par[k * PATCH], 12))
				d.add(SAMPLECD_PATCH, SAMPLECD_S550, k, ai, text(&par[k * PATCH], 12), 0, start * BLOCK + k * PATCH);
		for (uint32_t k = 0; k < TONES; k++) {
			const uint8_t *t = &par[TONES_AT + k * TONE];
			if (!blank(t, 8))
				d.add(SAMPLECD_TONE, SAMPLECD_S550, k, ai, text(t, 8), uint64_t(t[0x0F]) * SEGMENT_WORDS * 2,
					start * BLOCK + TONES_AT + k * TONE);
		}
	}
}

// a patch's key maps: the first at +0x1A, the second at +0x87 where the key mode (+0x0F) is 1 to 3; a tone number a key
std::vector<size_t> refs(const Disc &d, size_t i)
{
	const samplecd_entry &e = d.entries[i];
	if (e.kind != SAMPLECD_PATCH)
		return {};
	auto rec = d.image.read(d.where[i], PATCH);
	if (rec.empty())
		return {};
	std::vector<size_t> out;
	for (int map = 0; map < (rec[0x0F] >= 1 && rec[0x0F] <= 3 ? 2 : 1); map++)
		for (uint32_t k = 0; k < KEYS; k++) {
			uint8_t tone = rec[(map ? 0x87 : 0x1A) + k];
			if (tone > LAST_TONE)
				continue;
			size_t j = d.child(e.parent, SAMPLECD_TONE, tone);
			if (j != SAMPLECD_NONE && std::find(out.begin(), out.end(), j) == out.end())
				out.push_back(j);
		}
	return out;
}

// a tone: 30 kHz where +0x0B is 0, else 15 kHz; the original key at +0x0C; its start and end at +0x10 and +0x13,
// 24-bit big-endian words from its first segment
bool sample(const Disc &d, size_t i, samplecd_sample &out)
{
	if (d.entries[i].kind != SAMPLECD_TONE)
		return false;
	uint8_t t[0x16];
	if (!d.image.read(d.where[i], t, sizeof t))
		return false;
	uint32_t start = be24(t + 0x10), end = be24(t + 0x13);
	out.rate = t[0x0B] ? 15000 : 30000;
	out.key = t[0x0C] <= 127 ? t[0x0C] : -1;
	out.frames = end >= start ? end - start + 1 : 0;
	return true;
}

}
