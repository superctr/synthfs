// Copyright 2026 Ian Karlsson
// The Roland S-700 series volume (docs/formats.md, "S-7xx")
#include "internal.hpp"

#include <algorithm>

namespace scd::s7 {

constexpr uint32_t SECTOR = 512;
constexpr uint32_t MACHINES = SAMPLECD_S760 | SAMPLECD_S770 | SAMPLECD_XV5080;

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

bool probe(const Image &im)
{
	uint8_t s0[0x11E];
	uint16_t n[5];
	return im.read(0, s0, sizeof s0) && !memcmp(s0, "\0\0\0\0S770 MR25A", 14) && counts(s0, n);
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
	std::string label;
	for (int i = 0x20; i < 0x40 && s0[i]; i++)
		label += s0[i] >= 32 && s0[i] < 127 ? char(s0[i]) : ' ';
	size_t a = label.find_first_not_of(' ');
	d.label = a == std::string::npos ? "" : trimmed(label.substr(a));
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
			size_t i = d.add(L.kind, n, SAMPLECD_NONE, name_of(e), bytes, MACHINES, uint64_t(L.record) * SECTOR + uint64_t(n) * L.size);
			for (const char *c : categories)
				if (!memcmp(e, c, 3))
					memcpy(d.entries[i].category, c, 3);
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
	return true;
}

}
