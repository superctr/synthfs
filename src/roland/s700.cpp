// Copyright 2026 Ian Karlsson
// The Roland S-700 series volume, the S-760's and the S-770's, on a disk and on a floppy (docs/formats.md, "S-760 and
// S-770")
#include "roland.hpp"

#include <algorithm>
#include <map>

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

namespace {

constexpr uint32_t EXTENDED = 0x19AE;  // the S-760's extended partials, 128 bytes each
// a directory entry after its name: its list's type, the entries after and before it in the list's order (0x8000
// set, 0xFFFF none), its place in that order, a sample's first segment, and the segments it sounds
constexpr int E_TYPE = 16, E_NEXT = 18, E_PREV = 20, E_RANK = 22, E_TOP = 28, E_SIZE = 30;
constexpr uint32_t LINKED = 0x8000, NO_ENTRY = 0xFFFF;

void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }

bool listed(const std::vector<uint8_t> &dir, uint32_t n, uint32_t count)
{
	return n < count && dir[size_t(n) * 32] >= 32 && dir[size_t(n) * 32] <= 126;
}

// where in a record of list l the numbers it names in the list below are, a halfword each
std::vector<int> names_at(int l, const uint8_t *rec)
{
	std::vector<int> at;
	switch (l) {
	case 0:
		for (int k = 0; k < 64; k++)
			at.push_back(32 + 2 * k);
		break;
	case 1:
		for (int k = 0; k < 32; k++)
			at.push_back(0x100 + 2 * k);
		break;
	case 2:
		for (int k = 0; k < 88; k++)
			if (rec[32 + k] != 255)
				at.push_back(0x100 + 2 * k);
		break;
	case 3:
		for (int k = 0; k < 4; k++)
			at.push_back(16 + 16 * k);
		break;
	default:
		break;
	}
	return at;
}

uint64_t record_at(int l, uint32_t n)
{
	return uint64_t(lists[l].record) * SECTOR + uint64_t(n) * lists[l].size;
}

uint64_t segment_at(uint32_t seg)
{
	return (WAVES + uint64_t(SEGMENT_BYTES / SECTOR) * (seg - FIRST_SEGMENT)) * SECTOR;
}

}

// Another S-700 disk's entry taken in with all it uses, down to its samples' waves.  What is already here, the
// same in name, record and wave, is used and not made again; the rest goes into the slots after each list's last,
// the records naming their numbers here.
size_t S700Disk::adopt(const Medium &from, size_t i, size_t parent, std::string &why, bool &taken)
{
	const auto *src = dynamic_cast<const S700Disk *>(&from);
	taken = src != nullptr;
	if (!src)
		return syfs::NONE;
	int level = 0;
	while (level < 5 && lists[level].kind != from.entries[i].kind)
		level++;
	if (parent != syfs::NONE || level == 5 || src->numbered_[level].empty() || numbered_[level].empty())
		return why = "an S-700 disk's entries are taken into its lists, under none", syfs::NONE;

	// both disks' counts, directories and segment tables
	uint16_t count[5], scount[5];
	auto s0 = image.read(0, SECTOR), ss0 = src->image.read(0, SECTOR);
	if (s0.empty() || ss0.empty() || !counts(s0.data(), count) || !counts(ss0.data(), scount))
		return why = "the disk could not be read", syfs::NONE;
	std::vector<uint8_t> dir[5], sdir[5];
	for (int l = level; l < 5; l++) {
		dir[l] = image.read(uint64_t(lists[l].dir) * SECTOR, lists[l].cap * 32u);
		sdir[l] = src->image.read(uint64_t(lists[l].dir) * SECTOR, lists[l].cap * 32u);
		if (dir[l].empty() || sdir[l].empty())
			return why = "the disk could not be read", syfs::NONE;
	}
	auto table = image.read(uint64_t(SEGMENT_TABLE) * SECTOR, 2 * 0x10000);
	auto stable = src->image.read(uint64_t(SEGMENT_TABLE) * SECTOR, 2 * 0x10000);
	if (table.empty() || stable.empty())
		return why = "the disk could not be read", syfs::NONE;

	// what is taken: the entry, and in each list below what the ones above name there, in the order first named
	std::vector<uint32_t> need[5];
	std::vector<uint8_t> record[5];  // their records, one after another
	need[level].push_back(from.entries[i].number);
	for (int l = level; l < 5; l++)
		for (size_t k = 0; k < need[l].size(); k++) {
			auto rec = src->image.read(record_at(l, need[l][k]), lists[l].size);
			if (rec.empty())
				return why = "the disk could not be read", syfs::NONE;
			record[l].insert(record[l].end(), rec.begin(), rec.end());
			for (int at : names_at(l, rec.data())) {
				const uint32_t n = le16(&rec[at]);
				if (listed(sdir[l + 1], n, scount[l + 1]) && std::find(need[l + 1].begin(), need[l + 1].end(), n) == need[l + 1].end())
					need[l + 1].push_back(n);
			}
		}

	// a sample's segments, from its directory entry's first along the table
	auto chain = [](const std::vector<uint8_t> &t, const uint8_t *e) {
		std::vector<uint32_t> segs;
		uint32_t seg = le16(e + E_TOP);
		for (uint32_t k = le16(e + E_SIZE); k && seg >= FIRST_SEGMENT && seg < LAST_SEGMENT; k--) {
			segs.push_back(seg);
			seg = le16(&t[2 * seg]);
		}
		return segs.size() == le16(e + E_SIZE) ? segs : std::vector<uint32_t>();
	};

	// the plan: from the samples up, each entry's number here, one already here that is the same or the next slot
	// of its list, and for those made their record, with the numbers it names set to theirs here
	struct Made {
		int l;
		uint32_t from, to;
		std::vector<uint8_t> rec;
		std::vector<uint32_t> segs, from_segs;
		uint32_t size;
	};
	std::vector<Made> made;
	std::map<uint32_t, uint32_t> at[5];
	std::map<uint32_t, std::vector<uint32_t>> sounds[5];  // the samples each entry taken sounds, by their numbers there
	uint32_t next_slot[5];
	for (int l = 0; l < 5; l++)
		next_slot[l] = count[l];
	std::vector<uint8_t> plan = table;
	uint32_t free_seg = FIRST_SEGMENT;
	for (int l = 4; l >= level; l--)
		for (size_t k = 0; k < need[l].size(); k++) {
			const uint32_t n = need[l][k];
			const uint8_t *se = &sdir[l][size_t(n) * 32];
			std::vector<uint8_t> rec(record[l].begin() + ptrdiff_t(k * lists[l].size), record[l].begin() + ptrdiff_t((k + 1) * lists[l].size));
			std::vector<uint32_t> &its = sounds[l][n];
			if (l == 4)
				its.push_back(n);
			// a number that names nothing there must name nothing here
			for (int a : names_at(l, rec.data())) {
				const uint32_t v = le16(&rec[a]);
				auto it = at[l + 1].find(v);
				put16(&rec[a], listed(sdir[l + 1], v, scount[l + 1]) && it != at[l + 1].end() ? it->second : NO_ENTRY);
				if (it != at[l + 1].end())
					for (uint32_t s : sounds[l + 1][v])
						if (std::find(its.begin(), its.end(), s) == its.end())
							its.push_back(s);
			}
			uint32_t size = 0;
			for (uint32_t s : its)
				size += le16(&sdir[4][size_t(s) * 32 + E_SIZE]);
			std::vector<uint32_t> from_segs;
			if (l == 4 && (from_segs = chain(stable, se)).empty() && le16(se + E_SIZE))
				return why = "a sample's wave cannot be followed on the disk it is on", syfs::NONE;
			// the same already here: its name and its record, and for a sample its wave
			uint32_t same = NO_ENTRY;
			for (uint32_t d = 0; d < count[l] && same == NO_ENTRY; d++) {
				const uint8_t *de = &dir[l][size_t(d) * 32];
				if (!listed(dir[l], d, count[l]) || memcmp(de, se, 16) || (l == 4 && le16(de + E_SIZE) != le16(se + E_SIZE)))
					continue;
				auto have = image.read(record_at(l, d), lists[l].size);
				if (have != rec)
					continue;
				bool equal = true;
				if (l == 4) {
					auto here = chain(table, de);
					equal = here.size() == from_segs.size();
					for (size_t g = 0; equal && g < here.size(); g++)
						equal = image.read(segment_at(here[g]), SEGMENT_BYTES) == src->image.read(segment_at(from_segs[g]), SEGMENT_BYTES);
				}
				if (equal)
					same = d;
			}
			if (same != NO_ENTRY) {
				at[l][n] = same;
				continue;
			}
			if (next_slot[l] >= lists[l].cap)
				return why = "the disk's list is full", syfs::NONE;
			Made m { l, n, next_slot[l]++, rec, {}, from_segs, size };
			// a sample's segments: the first free ones, each naming the next and the last ending the chain
			for (size_t g = 0; g < from_segs.size(); g++) {
				while (free_seg < 0xFFF8 && le16(&plan[2 * free_seg]))
					free_seg++;
				if (free_seg >= 0xFFF8)
					return why = "the disk has not the room for the waves", syfs::NONE;
				put16(&plan[2 * free_seg], LAST_SEGMENT);
				if (g)
					put16(&plan[2 * m.segs.back()], free_seg);
				m.segs.push_back(free_seg);
			}
			at[l][n] = m.to;
			made.push_back(std::move(m));
		}

	// each list's last entry in its order, and how many it has
	uint32_t tail[5], rank[5];
	for (int l = level; l < 5; l++) {
		tail[l] = NO_ENTRY;
		rank[l] = 0;
		for (uint32_t d = 0; d < count[l]; d++)
			if (listed(dir[l], d, count[l])) {
				rank[l]++;
				if (le16(&dir[l][size_t(d) * 32 + E_NEXT]) == NO_ENTRY)
					tail[l] = d;
			}
	}

	// written: a sample's wave and every record first, then the directory's entries, then the table and the counts,
	// so that until the last nothing on the disk names what is not whole
	bool ok = true;
	uint32_t taken_segs = 0;
	const std::vector<uint8_t> erased(128, 0xFF);
	for (const Made &m : made) {
		for (size_t g = 0; ok && g < m.segs.size(); g++) {
			auto wave = src->image.read(segment_at(m.from_segs[g]), SEGMENT_BYTES);
			ok = !wave.empty() && image.write(segment_at(m.segs[g]), wave.data(), wave.size());
		}
		taken_segs += uint32_t(m.segs.size());
		ok = ok && image.write(record_at(m.l, m.to), m.rec.data(), m.rec.size());
		// an S-760's disk keeps a partial's sub-samples apart: the other disk's where it is one too, else none
		if (ok && m.l == 3 && format == SYFS_FORMAT_S760) {
			auto ext = src->format == SYFS_FORMAT_S760 ? src->image.read(uint64_t(EXTENDED) * SECTOR + uint64_t(m.from) * 128, 128) : erased;
			ok = image.write(uint64_t(EXTENDED) * SECTOR + uint64_t(m.to) * 128, ext.empty() ? erased.data() : ext.data(), 128);
		}
	}
	for (const Made &m : made) {
		uint8_t *e = &dir[m.l][size_t(m.to) * 32];
		memcpy(e, &sdir[m.l][size_t(m.from) * 32], 32);
		e[E_TYPE] = uint8_t(0x40 + m.l), e[E_TYPE + 1] = 0;
		put16(e + E_NEXT, NO_ENTRY);
		put16(e + E_PREV, tail[m.l] == NO_ENTRY ? NO_ENTRY : tail[m.l] | LINKED);
		put16(e + E_RANK, rank[m.l]++);
		put16(e + E_TOP, m.segs.empty() ? 0 : m.segs[0]);
		put16(e + E_SIZE, m.size);
		ok = ok && image.write(uint64_t(lists[m.l].dir) * SECTOR + uint64_t(m.to) * 32, e, 32);
		if (tail[m.l] != NO_ENTRY) {
			uint8_t *t = &dir[m.l][size_t(tail[m.l]) * 32];
			put16(t + E_NEXT, m.to | LINKED);
			ok = ok && image.write(uint64_t(lists[m.l].dir) * SECTOR + uint64_t(tail[m.l]) * 32 + E_NEXT, t + E_NEXT, 2);
		}
		tail[m.l] = m.to;
	}
	if (ok && taken_segs) {
		put16(&plan[2], le16(&plan[2]) >= taken_segs ? le16(&plan[2]) - taken_segs : 0);
		ok = image.write(uint64_t(SEGMENT_TABLE) * SECTOR, plan.data(), plan.size());
	}
	uint8_t counted[10];
	for (int l = 0; l < 5; l++)
		put16(counted + 2 * l, next_slot[l]);
	ok = ok && image.write(0x114, counted, sizeof counted);
	if (!ok)
		return why = "the image could not be written", syfs::NONE;

	// and listed here
	segments_.clear();
	for (const Made &m : made) {
		const uint8_t *e = &dir[m.l][size_t(m.to) * 32];
		const size_t added = add(lists[m.l].kind, format, m.to, syfs::NONE, name_of(e),
			m.l == 4 ? uint64_t(m.segs.size()) * SEGMENT_BYTES : 0, record_at(m.l, m.to));
		tag(added, category(e));
		numbered_[m.l][m.to] = added;
	}
	return numbered_[level][at[level][from.entries[i].number]];
}

std::unique_ptr<Medium> S700Disk::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<S700Disk>(io, format);
}

}
