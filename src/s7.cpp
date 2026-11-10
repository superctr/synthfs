// Copyright 2026 Ian Karlsson
// The Roland S-700 series volume, the S-760's and the S-770's (docs/formats.md, "S-760 and S-770")
#include "internal.hpp"

#include <algorithm>

namespace scd::s7 {

constexpr uint32_t SECTOR = 512;

struct List {
	samplecd_kind kind;
	uint32_t dir;     // first directory sector
	uint32_t cap;     // entries, 16 to a sector
	uint32_t record;  // first parameter sector
	uint32_t size;    // bytes a record
};
constexpr List lists[5] = {
	{ SAMPLECD_VOLUME, 0x504, 128, 0x86C, 256 },
	{ SAMPLECD_PERFORMANCE, 0x50C, 512, 0x8AC, 512 },
	{ SAMPLECD_PATCH, 0x52C, 1024, 0xAAC, 512 },
	{ SAMPLECD_PARTIAL, 0x56C, 4096, 0xEAC, 128 },
	{ SAMPLECD_SAMPLE, 0x66C, 8192, 0x12AC, 48 },
};
constexpr uint32_t SEGMENT_BYTES = 18 * SECTOR;

constexpr const char *categories[] = { "---", "BEL", "BRS", "BS ", "BTS", "CMB", "CYM", "DRM", "FLT", "GTR", "HAT", "HIT",
	"KEY", "KIK", "KIT", "MLT", "ORC", "ORG", "PAD", "PNO", "PRC", "SAX", "SFX", "SNR", "STR", "TOM", "VOX", "WND" };
constexpr uint32_t rates[8] = { 48000, 44100, 24000, 22050, 30000, 15000, 32000, 16000 };

static bool counts(const uint8_t *s0, uint16_t *out)
{
	for (int l = 0; l < 5; l++)
		if ((out[l] = le16(s0 + 0x114 + 2 * l)) > lists[l].cap)
			return false;
	return true;
}

// the system line at +0x20, the machine that made the disc
static std::string system_line(const uint8_t *s0)
{
	std::string line;
	for (int i = 0x20; i < 0x40 && s0[i]; i++)
		line += s0[i] >= 32 && s0[i] < 127 ? char(s0[i]) : ' ';
	size_t a = line.find_first_not_of(' ');
	return a == std::string::npos ? "" : trimmed(line.substr(a));
}

samplecd_format probe(const Image &im)
{
	uint8_t s0[0x11E];
	uint16_t n[5];
	if (!im.read(0, s0, sizeof s0) || memcmp(s0, "\0\0\0\0S770 MR25A", 14) || !counts(s0, n))
		return SAMPLECD_UNKNOWN;
	return system_line(s0).starts_with("S-760") ? SAMPLECD_S760 : SAMPLECD_S770;
}

static std::string name_of(const uint8_t *p)
{
	std::string s;
	for (int i = 0; i < 16; i++)
		s += p[i] >= 32 && p[i] < 127 ? char(p[i]) : '?';
	return s;
}

void list(Disc &d)
{
	auto s0 = d.image.read(0, SECTOR);
	uint16_t count[5];
	if (s0.empty() || !counts(s0.data(), count))
		return;
	d.label = system_line(s0.data());
	for (int l = 0; l < 5; l++) {
		const List &L = lists[l];
		d.numbered[l].assign(L.cap, SAMPLECD_NONE);
		// the header's count of slots, each listed where its name starts printable
		auto dir = d.image.read(uint64_t(L.dir) * SECTOR, count[l] * 32u);
		for (uint32_t n = 0; n < count[l] && !dir.empty(); n++) {
			const uint8_t *e = &dir[n * 32];
			if (e[0] < 32 || e[0] > 126)
				continue;
			uint64_t bytes = L.kind == SAMPLECD_SAMPLE ? uint64_t(le16(e + 30)) * SEGMENT_BYTES : 0;
			size_t i = d.add(L.kind, d.format, n, SAMPLECD_NONE, name_of(e), bytes, uint64_t(L.record) * SECTOR + uint64_t(n) * L.size);
			for (const char *c : categories)
				if (!memcmp(e, c, 3))
					d.tag(i, c);
			d.numbered[l][n] = i;
		}
	}
}

std::vector<size_t> refs(const Disc &d, size_t i)
{
	const samplecd_entry &e = d.entries[i];
	int l = 0;
	while (l < 4 && lists[l].kind != e.kind)
		l++;
	if (l == 4 || d.numbered[l + 1].empty())
		return {};
	auto rec = d.image.read(d.where[i], lists[l].size);
	if (rec.empty())
		return {};
	std::vector<int32_t> nums;
	switch (e.kind) {
	case SAMPLECD_VOLUME:
		for (int k = 0; k < 64; k++)
			nums.push_back(le16(&rec[32 + 2 * k]));
		break;
	case SAMPLECD_PERFORMANCE:
		for (int k = 0; k < 32; k++)
			nums.push_back(le16(&rec[0x100 + 2 * k]));
		break;
	case SAMPLECD_PATCH:
		for (int k = 0; k < 88; k++)
			if (rec[32 + k] != 255)
				nums.push_back(le16(&rec[0x100 + 2 * k]));
		break;
	default:
		for (int k = 0; k < 4; k++)
			nums.push_back(int16_t(le16(&rec[16 + 16 * k])));
		break;
	}
	const auto &to = d.numbered[l + 1];
	std::vector<size_t> out;
	for (int32_t n : nums)
		if (n >= 0 && size_t(n) < to.size() && to[n] != SAMPLECD_NONE && std::find(out.begin(), out.end(), to[n]) == out.end())
			out.push_back(to[n]);
	return out;
}

bool sample(const Disc &d, size_t i, samplecd_sample &out)
{
	if (d.entries[i].kind != SAMPLECD_SAMPLE)
		return false;
	auto rec = d.image.read(d.where[i], 48);
	if (rec.empty())
		return false;
	out.rate = rates[rec[44] & 7];
	out.key = rec[45] <= 127 ? rec[45] : -1;
	out.frames = d.entries[i].bytes / 2;
	// the start, the sustain loop's start and end, the release loop's (24.8 words, from the sample's first)
	uint64_t p[5];
	for (int k = 0; k < 5; k++)
		p[k] = le32(&rec[16 + 4 * k]) >> 8;
	out.mode = rec[36];
	out.start = p[0];
	out.end = p[2] + 1;
	out.loop_start = p[1];
	out.loop_end = p[2] + 1;
	switch (out.mode) {
	case 0: out.loop = SAMPLECD_LOOP_FORWARD; break;
	case 1: case 3: out.loop = SAMPLECD_LOOP_FORWARD; out.end = p[4] + 1; break;
	case 4: out.loop = SAMPLECD_LOOP_ALTERNATE; break;
	case 5: out.reverse = 1; break;
	case 6: out.reverse = 1; out.loop = SAMPLECD_LOOP_FORWARD; out.loop_start = p[0]; out.loop_end = p[1] + 1; break;
	default: break;
	}
	return true;
}

}
