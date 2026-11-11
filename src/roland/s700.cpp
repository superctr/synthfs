// Copyright 2026 Ian Karlsson
// The Roland S-700 series volume, the S-760's and the S-770's, on a disk and on a floppy (docs/formats.md, "S-760 and
// S-770")
#include "roland.hpp"

#include <algorithm>

namespace syfs::roland {

namespace {

struct List {
	syfs_key kind;
	uint32_t dir;     // first directory sector
	uint32_t cap;     // entries, 16 to a sector
	uint32_t record;  // first parameter sector
	uint32_t size;    // bytes a record
};
constexpr List lists[5] = {
	{ SYFS_TYPE_VOLUME, 0x504, 128, 0x86C, 256 },
	{ SYFS_TYPE_PERFORMANCE, 0x50C, 512, 0x8AC, 512 },
	{ SYFS_TYPE_PATCH, 0x52C, 1024, 0xAAC, 512 },
	{ SYFS_TYPE_PARTIAL, 0x56C, 4096, 0xEAC, 128 },
	{ SYFS_TYPE_SAMPLE, 0x66C, 8192, 0x12AC, 48 },
};

constexpr const char *categories[] = { "---", "BEL", "BRS", "BS ", "BTS", "CMB", "CYM", "DRM", "FLT", "GTR", "HAT", "HIT",
	"KEY", "KIK", "KIT", "MLT", "ORC", "ORG", "PAD", "PNO", "PRC", "SAX", "SFX", "SNR", "STR", "TOM", "VOX", "WND" };
constexpr uint32_t rates[8] = { 48000, 44100, 24000, 22050, 30000, 15000, 32000, 16000 };
constexpr uint32_t SEGMENT_TABLE = 0x404, WAVES = 0x15AC, FIRST_SEGMENT = 2, LAST_SEGMENT = 0xFFF8;

static bool counts(const uint8_t *s0, uint16_t *out)
{
	for (int l = 0; l < 5; l++)
		if ((out[l] = le16(s0 + 0x114 + 2 * l)) > lists[l].cap)
			return false;
	return true;
}

}

namespace s700 {

// the system line at +0x20, the machine that made the disc
std::string system_line(const uint8_t *s0)
{
	std::string line;
	for (int i = 0x20; i < 0x40 && s0[i]; i++)
		line += s0[i] >= 32 && s0[i] < 127 ? char(s0[i]) : ' ';
	size_t a = line.find_first_not_of(' ');
	return a == std::string::npos ? "" : trimmed(line.substr(a));
}

std::string name_of(const uint8_t *p)
{
	std::string s;
	for (int i = 0; i < 16; i++)
		s += p[i] >= 32 && p[i] < 127 ? char(p[i]) : '?';
	return s;
}

std::string_view category(const uint8_t *name)
{
	for (const char *c : categories)
		if (!memcmp(name, c, 3))
			return c;
	return {};
}

// the start, the sustain loop's start and end, the release loop's (24.8 words from the sample's first) at +16; the
// loop mode at +36, the rate code at +44, the original key at +45
void sample_of(const uint8_t *rec, uint64_t frames, syfs::Sample &out)
{
	out.rate = rates[rec[44] & 7];
	out.key = rec[45] <= 127 ? rec[45] : -1;
	out.frames = frames;
	uint64_t p[5];
	for (int k = 0; k < 5; k++)
		p[k] = le32(&rec[16 + 4 * k]) >> 8;
	out.mode = rec[36];
	out.start = p[0];
	out.end = p[2] + 1;
	out.loop_start = p[1];
	out.loop_end = p[2] + 1;
	switch (out.mode) {
	case 0: out.loop = SYFS_LOOP_FORWARD; break;
	case 1: case 3: out.loop = SYFS_LOOP_FORWARD; out.end = p[4] + 1; break;
	case 4: out.loop = SYFS_LOOP_ALTERNATE; break;
	case 5: out.reverse = 1; break;
	case 6: out.reverse = 1; out.loop = SYFS_LOOP_FORWARD; out.loop_start = p[0]; out.loop_end = p[1] + 1; break;
	default: break;
	}
}

}

using namespace s700;

// byte 0x0F: 0 on a disk, '@' on one the S-760's system was saved to; a floppy's kind otherwise
static bool disk_type(uint8_t type)
{
	return type == 0 || type == '@' || type == 'A';
}

syfs_key S700Disk::probe(const Image &im)
{
	uint8_t s0[0x11E];
	uint16_t n[5];
	if (!im.read(0, s0, sizeof s0) || memcmp(s0, "\0\0\0\0S770 MR25A", 15) || !disk_type(s0[0x0F]) || !counts(s0, n))
		return SYFS_NONE;
	return system_line(s0).starts_with("S-760") ? SYFS_FORMAT_S760 : SYFS_FORMAT_S770;
}

void S700Disk::list()
{
	auto s0 = image.read(0, SECTOR);
	uint16_t count[5];
	if (s0.empty() || !counts(s0.data(), count))
		return;
	label = system_line(s0.data());
	for (int l = 0; l < 5; l++) {
		const List &L = lists[l];
		numbered_[l].assign(L.cap, syfs::NONE);
		// the header's count of slots, each listed where its name starts printable
		auto dir = image.read(uint64_t(L.dir) * SECTOR, count[l] * 32u);
		for (uint32_t n = 0; n < count[l] && !dir.empty(); n++) {
			const uint8_t *e = &dir[n * 32];
			if (e[0] < 32 || e[0] > 126)
				continue;
			uint64_t bytes = L.kind == SYFS_TYPE_SAMPLE ? uint64_t(le16(e + 30)) * SEGMENT_BYTES : 0;
			size_t i = add(L.kind, format, n, syfs::NONE, name_of(e), bytes, uint64_t(L.record) * SECTOR + uint64_t(n) * L.size);
			tag(i, category(e));
			numbered_[l][n] = i;
		}
	}
}

std::vector<size_t> S700Disk::refs(size_t i) const
{
	const syfs::Entry &e = entries[i];
	int l = 0;
	while (l < 4 && lists[l].kind != e.kind)
		l++;
	if (l == 4 || numbered_[l + 1].empty())
		return {};
	auto rec = image.read(where[i], lists[l].size);
	if (rec.empty())
		return {};
	std::vector<int32_t> nums;
	switch (e.kind) {
	case SYFS_TYPE_VOLUME:
		for (int k = 0; k < 64; k++)
			nums.push_back(le16(&rec[32 + 2 * k]));
		break;
	case SYFS_TYPE_PERFORMANCE:
		for (int k = 0; k < 32; k++)
			nums.push_back(le16(&rec[0x100 + 2 * k]));
		break;
	case SYFS_TYPE_PATCH:
		for (int k = 0; k < 88; k++)
			if (rec[32 + k] != 255)
				nums.push_back(le16(&rec[0x100 + 2 * k]));
		break;
	default:
		for (int k = 0; k < 4; k++)
			nums.push_back(int16_t(le16(&rec[16 + 16 * k])));
		break;
	}
	const auto &to = numbered_[l + 1];
	std::vector<size_t> out;
	for (int32_t n : nums)
		if (n >= 0 && size_t(n) < to.size() && to[n] != syfs::NONE && std::find(out.begin(), out.end(), to[n]) == out.end())
			out.push_back(to[n]);
	return out;
}

// A patch's key: its partial (keys 21 to 108), and that partial's first layer naming a sample
size_t S700Disk::sample_at(size_t i, int key) const
{
	const syfs::Entry &e = entries[i];
	if (e.kind == SYFS_TYPE_PATCH) {
		if (key < 21 || key > 108)
			return syfs::NONE;
		auto rec = image.read(where[i], lists[2].size);
		if (rec.empty() || rec[32 + key - 21] == 255)
			return syfs::NONE;
		uint16_t n = le16(&rec[0x100 + 2 * (key - 21)]);
		return n < numbered_[3].size() && numbered_[3][n] != syfs::NONE ? sample_at(numbered_[3][n], key) : syfs::NONE;
	}
	if (e.kind == SYFS_TYPE_PARTIAL) {
		auto rec = image.read(where[i], lists[3].size);
		for (int k = 0; k < 4 && !rec.empty(); k++) {
			int16_t n = int16_t(le16(&rec[16 + 16 * k]));
			if (n >= 0 && size_t(n) < numbered_[4].size() && numbered_[4][n] != syfs::NONE)
				return numbered_[4][n];
		}
		return syfs::NONE;
	}
	return Medium::sample_at(i, key);
}

bool S700Disk::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind != SYFS_TYPE_SAMPLE)
		return false;
	auto rec = image.read(where[i], 48);
	if (rec.empty())
		return false;
	sample_of(rec.data(), entries[i].bytes / 2, out);
	return true;
}

// the sample's segments from its directory entry's segment top (+28), each the next of the one before in the segment
// table, 0xFFF8 and above ending it; segment n at sector 0x15AC + 18 × (n − 2)
size_t S700Disk::wave(size_t i, const syfs::Sample &, uint64_t first, int16_t *out, size_t frames) const
{
	uint8_t e[32];
	if (!image.read(uint64_t(lists[4].dir) * SECTOR + uint64_t(entries[i].number) * 32, e, sizeof e))
		return 0;
	if (segments_.empty())
		segments_ = image.read(uint64_t(SEGMENT_TABLE) * SECTOR, 2 * 0x10000);
	if (segments_.empty())
		return 0;
	auto next = [&](uint32_t n) { return uint32_t(le16(&segments_[2 * n])); };
	uint32_t seg = le16(e + 28);
	for (uint64_t k = first / SEGMENT_WORDS; k && seg >= FIRST_SEGMENT && seg < LAST_SEGMENT; k--)
		seg = next(seg);
	size_t done = 0;
	uint64_t at = first % SEGMENT_WORDS;
	while (done < frames && seg >= FIRST_SEGMENT && seg < LAST_SEGMENT) {
		size_t k = size_t(std::min<uint64_t>(frames - done, SEGMENT_WORDS - at));
		uint64_t sector = WAVES + uint64_t(SEGMENT_BYTES / SECTOR) * (seg - FIRST_SEGMENT);
		size_t got = read_words(image, sector * SECTOR + 2 * at, out + done, k);
		done += got;
		if (got < k)
			break;
		at = 0;
		seg = next(seg);
	}
	return done;
}

std::unique_ptr<Medium> S700Disk::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<S700Disk>(io, format);
}

}
