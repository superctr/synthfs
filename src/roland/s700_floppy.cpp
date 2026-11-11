// Copyright 2026 Ian Karlsson
// The Roland S-700 series volume on a floppy: one volume's lists, its records and waves (docs/formats.md, "On a
// floppy")
#include "roland.hpp"

namespace sfs::roland {

using namespace s700;

namespace {

struct FloppyList {
	samplerfs_kind kind;
	uint32_t names;   // first name sector, 16 bytes a name
	uint32_t cap;
	uint32_t record;  // first record sector
	uint32_t size;    // bytes a record
	uint32_t per;     // records a sector
};
constexpr FloppyList lists[4] = {
	{ SAMPLERFS_PERFORMANCE, 9, 64, 39, 256, 2 },
	{ SAMPLERFS_PATCH, 11, 128, 71, 256, 2 },
	{ SAMPLERFS_PARTIAL, 15, 256, 135, 128, 4 },
	{ SAMPLERFS_SAMPLE, 23, 480, 199, 48, 10 },
};
constexpr uint64_t MAX_SIZE = 2 * 1024 * 1024;
constexpr uint32_t WAVES = 252;

// a sound disk, 2HD and 2DD: Roland's S-760s and S-770s, a DJ-70's
bool sound(uint8_t type)
{
	return type == '2' || type == '3' || type == '>' || type == '?';
}

}

samplerfs_format S700Floppy::probe(const Image &im)
{
	uint8_t s0[0x40];
	if (im.size() > MAX_SIZE || !im.read(0, s0, sizeof s0) || memcmp(s0, "\0\0\0\0S770 MR25A", 15) || s0[0x0F] < '0')
		return SAMPLERFS_UNKNOWN;
	std::string line = system_line(s0);
	if (line.starts_with("S-760"))
		return SAMPLERFS_S760;
	if (line.starts_with("DJ-70"))
		return SAMPLERFS_DJ70;
	return SAMPLERFS_S770;
}

void S700Floppy::list()
{
	auto s0 = image.read(0, SECTOR);
	if (s0.empty())
		return;
	label = system_line(s0.data());
	// a save under way leaves 0xFF at +0x101
	if (!sound(s0[0x0F]) || s0[0x101] == 0xFF)
		return;
	if (le16(&s0[0x106]) == 1) {
		volume_ = add(SAMPLERFS_VOLUME, format, 0, SAMPLERFS_NONE, name_of(&s0[0x180]), uint64_t(le16(&s0[0x110])) * SEGMENT_BYTES, 0);
		tag(volume_, category(&s0[0x180]));
	}
	for (int l = 0; l < 4; l++) {
		const FloppyList &L = lists[l];
		uint32_t count = std::min<uint32_t>(le16(&s0[0x108 + 2 * l]), L.cap);
		numbered_[l].assign(L.cap, SAMPLERFS_NONE);
		auto names = image.read(uint64_t(L.names) * SECTOR, count * 16u);
		for (uint32_t n = 0; n < count && !names.empty(); n++) {
			const uint8_t *e = &names[n * 16];
			if (e[0] < 32 || e[0] > 126)
				continue;
			uint64_t at = uint64_t(L.record + n / L.per) * SECTOR + (n % L.per) * L.size;
			uint64_t bytes = 0;
			if (L.kind == SAMPLERFS_SAMPLE) {
				uint8_t rec[48];
				if (image.read(at, rec, sizeof rec))
					bytes = uint64_t(le16(rec + 0x2A)) * SEGMENT_BYTES;
			}
			size_t i = add(L.kind, format, n, SAMPLERFS_NONE, name_of(e), bytes, at);
			tag(i, category(e));
			numbered_[l][n] = i;
		}
	}
}

// the volume's performances, a byte each at +0x1A0 of sector 0 up to 0xFF; a performance's parts' patches, bytes at
// +0x10; a patch's keys' partials, bytes at +0x20; a partial's layers' samples, halfwords at +16, +32, +48, +64
std::vector<size_t> S700Floppy::refs(size_t i) const
{
	const samplerfs_entry &e = entries[i];
	std::vector<int32_t> nums;
	int to = 0;
	if (i == volume_) {
		auto s0 = image.read(0, SECTOR);
		for (size_t k = 0x1A0; k < 0x1FE && !s0.empty() && s0[k] != 0xFF; k++)
			nums.push_back(s0[k]);
	} else {
		int l = 0;
		while (l < 3 && lists[l].kind != e.kind)
			l++;
		if (l == 3)
			return {};
		to = l + 1;
		auto rec = image.read(where[i], lists[l].size);
		if (rec.empty())
			return {};
		switch (e.kind) {
		case SAMPLERFS_PERFORMANCE:
			for (int k = 0; k < 32; k++)
				if (rec[0x10 + k] != 0xFF)
					nums.push_back(rec[0x10 + k]);
			break;
		case SAMPLERFS_PATCH:
			for (int k = 0; k < 88; k++)
				if (rec[0x20 + k] != 0xFF)
					nums.push_back(rec[0x20 + k]);
			break;
		default:
			for (int k = 0; k < 4; k++)
				nums.push_back(int16_t(le16(&rec[16 + 16 * k])));
			break;
		}
	}
	const auto &list = numbered_[to];
	std::vector<size_t> out;
	for (int32_t n : nums)
		if (n >= 0 && size_t(n) < list.size() && list[n] != SAMPLERFS_NONE && std::find(out.begin(), out.end(), list[n]) == out.end())
			out.push_back(list[n]);
	return out;
}

bool S700Floppy::sample(size_t i, samplerfs_sample &out) const
{
	if (entries[i].kind != SAMPLERFS_SAMPLE)
		return false;
	uint8_t rec[48];
	if (!image.read(where[i], rec, sizeof rec))
		return false;
	sample_of(rec, uint64_t(le16(rec + 0x2A)) * SEGMENT_WORDS, out);
	return true;
}

// the sample's place at +0x28 (the disk of its set, the segment on it), segment n of a disk at sector 252 + 18n, the
// wave running on from a disk's last segment to the next disk's segment 0; what lies on this disk is read
size_t S700Floppy::wave(size_t i, const samplerfs_sample &, uint64_t first, int16_t *out, size_t frames) const
{
	uint8_t rec[48], s0[0x101];
	if (!image.read(where[i], rec, sizeof rec) || !image.read(0, s0, sizeof s0) || image.size() / SECTOR <= WAVES)
		return 0;
	uint64_t per = (image.size() / SECTOR - WAVES) / (SEGMENT_BYTES / SECTOR);
	uint64_t disk = rec[0x29], segment = rec[0x28];
	size_t done = 0;
	while (done < frames) {
		uint64_t f = first + done, n = segment + f / SEGMENT_WORDS, at = f % SEGMENT_WORDS;
		if (disk + n / per != s0[0x100])
			break;
		size_t k = size_t(std::min<uint64_t>(frames - done, SEGMENT_WORDS - at));
		uint64_t sector = WAVES + (n % per) * (SEGMENT_BYTES / SECTOR);
		size_t got = read_words(image, sector * SECTOR + 2 * at, out + done, k);
		done += got;
		if (got < k)
			break;
	}
	return done;
}

std::unique_ptr<Medium> S700Floppy::open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<S700Floppy>(io, format);
}

}
