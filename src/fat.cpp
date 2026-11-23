// Copyright 2026 Ian Karlsson
// A FAT volume, bare or behind an MBR: its directories and files (docs/formats.md, "FAT")
#include "fat.hpp"
#include "roland/roland.hpp"

#include "audio.hpp"

#include <cctype>
#include <unordered_set>

namespace syfs {

namespace fat {

namespace {

constexpr uint32_t MBR_SECTOR = 512;
constexpr uint64_t FAT_CHUNK = 4096;
constexpr int DEPTH = 32;
constexpr size_t ENTRIES = 1 << 18;
constexpr uint64_t DIR_BYTES = 65536 * 32;
constexpr size_t NAME = sizeof(syfs::Entry::name) - 1;

// the partition types of FAT volumes (the hidden ones too)
bool fat_type(uint8_t t)
{
	switch (t & ~0x10) {
	case 0x01: case 0x04: case 0x06: case 0x0B: case 0x0C: case 0x0E: return true;
	default: return false;
	}
}

bool power_of_two(uint32_t v) { return v && !(v & (v - 1)); }

}

std::optional<Geometry> geometry(const uint8_t *bs, uint64_t base, uint64_t image_size)
{
	if (!(bs[0] == 0xEB && bs[2] == 0x90) && bs[0] != 0xE9)
		return std::nullopt;
	Geometry g;
	g.base = base;
	g.sector = le16(bs + 11);
	uint32_t spc = bs[13], reserved = le16(bs + 14), fats = bs[16];
	g.root_entries = le16(bs + 17);
	uint32_t total16 = le16(bs + 19), fat16 = le16(bs + 22);
	uint8_t media = bs[21];
	if (g.sector < 512 || g.sector > 4096 || !power_of_two(g.sector) || !power_of_two(spc) || !reserved || !fats
		|| fats > 4 || (media != 0xF0 && media < 0xF8))
		return std::nullopt;
	g.cluster = g.sector * spc;
	g.total = total16 ? total16 : le32(bs + 32);
	uint64_t fat_sectors = fat16 ? fat16 : le32(bs + 36);
	if (!g.total || !fat_sectors || (!fat16 && g.root_entries))
		return std::nullopt;
	uint64_t root_sectors = (uint64_t(g.root_entries) * 32 + g.sector - 1) / g.sector;
	uint64_t data_sector = reserved + fats * fat_sectors + root_sectors;
	if (data_sector >= g.total)
		return std::nullopt;
	g.fat_at = uint64_t(reserved) * g.sector;
	g.fat_bytes = fat_sectors * g.sector;
	g.root_at = g.fat_at + fats * g.fat_bytes;
	g.data_at = data_sector * g.sector;
	uint64_t clusters = (g.total - data_sector) / spc;
	if (!clusters || clusters > 0x0FFFFFF5)
		return std::nullopt;
	g.clusters = uint32_t(clusters);
	g.bits = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
	if ((g.bits == 32) != !fat16 || g.fat_bytes * 8 / g.bits < clusters + 2)
		return std::nullopt;
	if (base + g.data_at > image_size)
		return std::nullopt;
	const uint8_t *ext = bs + (g.bits == 32 ? 64 : 36);
	if (g.bits == 32)
		g.root_cluster = le32(bs + 44);
	if (ext[2] == 0x29)
		memcpy(g.label, ext + 7, 11);
	return g;
}

std::optional<Geometry> find(const Image &im)
{
	uint8_t s0[512];
	if (!im.read(0, s0, sizeof s0))
		return std::nullopt;
	if (auto g = geometry(s0, 0, im.size()))
		return g;
	if (s0[510] != 0x55 || s0[511] != 0xAA)
		return std::nullopt;
	for (int p = 0; p < 4; p++) {
		const uint8_t *e = s0 + 446 + 16 * p;
		uint64_t start = uint64_t(le32(e + 8)) * MBR_SECTOR;
		uint8_t bs[512];
		if ((e[0] & 0x7F) || !fat_type(e[4]) || !start || !le32(e + 12) || !im.read(start, bs, sizeof bs))
			continue;
		if (auto g = geometry(bs, start, im.size()))
			return g;
	}
	return std::nullopt;
}

}

using namespace fat;

namespace {

constexpr uint64_t floppies[] = { 160, 180, 320, 360, 720, 1200, 1440, 1680, 1722, 2880 };

void utf8(std::string &s, uint32_t c)
{
	if (c < 0x80)
		s += char(c);
	else if (c < 0x800)
		s += char(0xC0 | c >> 6), s += char(0x80 | (c & 0x3F));
	else if (c < 0x10000)
		s += char(0xE0 | c >> 12), s += char(0x80 | (c >> 6 & 0x3F)), s += char(0x80 | (c & 0x3F));
	else
		s += char(0xF0 | c >> 18), s += char(0x80 | (c >> 12 & 0x3F)), s += char(0x80 | (c >> 6 & 0x3F)),
			s += char(0x80 | (c & 0x3F));
}

// a long name's UTF-16 to UTF-8, up to its terminator, cut at a character to fit an entry's name
std::string long_name(const std::vector<uint16_t> &u)
{
	std::string s;
	for (size_t k = 0; k < u.size() && u[k]; k++) {
		uint32_t c = u[k];
		if (c >= 0xD800 && c < 0xDC00 && k + 1 < u.size() && u[k + 1] >= 0xDC00 && u[k + 1] < 0xE000)
			c = 0x10000 + ((c - 0xD800) << 10) + (u[++k] - 0xDC00);
		else if (c >= 0xD800 && c < 0xE000)
			c = '?';
		std::string one;
		utf8(one, c);
		if (s.size() + one.size() > NAME)
			break;
		s += one;
	}
	return s;
}

// an 8.3 name: its base and extension, each lower-cased where the entry says so, other than ASCII shown as '?'
std::string short_name(const uint8_t *e)
{
	auto part = [&](int from, int n, bool lower) {
		std::string s;
		for (int k = 0; k < n; k++) {
			uint8_t c = e[from + k];
			if (from == 0 && k == 0 && c == 0x05)
				c = 0xE5;
			s += c >= 0x80 || c < 0x20 ? '?' : lower ? char(tolower(c)) : char(c);
		}
		return trimmed(s);
	};
	std::string base = part(0, 8, e[12] & 0x08), ext = part(8, 3, e[12] & 0x10);
	return ext.empty() ? base : base + "." + ext;
}

uint8_t checksum(const uint8_t *e)
{
	uint8_t sum = 0;
	for (int k = 0; k < 11; k++)
		sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + e[k]);
	return sum;
}

std::string label_of(const uint8_t *p)
{
	std::string s;
	for (int k = 0; k < 11; k++)
		s += p[k] >= 0x20 && p[k] < 0x80 ? char(p[k]) : '?';
	return trimmed(s);
}

}

syfs_key Fat::probe(const Image &im)
{
	return find(im) ? SYFS_FORMAT_FAT : SYFS_NONE;
}

syfs_key Fat::medium_of(const Image &im)
{
	auto g = find(im);
	if (!g || g->base)
		return SYFS_MEDIUM_DISK;
	uint64_t bytes = g->total * g->sector;
	for (uint64_t k : floppies)
		if (bytes == k * 1024)
			return SYFS_MEDIUM_FLOPPY;
	return SYFS_MEDIUM_DISK;
}

uint32_t Fat::next(uint32_t c) const
{
	auto byte = [&](uint64_t at) -> int {
		if (at >= g_.fat_bytes)
			return -1;
		uint64_t chunk = at / FAT_CHUNK;
		auto it = fat_cache_.find(chunk);
		if (it == fat_cache_.end()) {
			uint64_t n = std::min(FAT_CHUNK, g_.fat_bytes - chunk * FAT_CHUNK);
			it = fat_cache_.emplace(chunk, image.read(g_.base + g_.fat_at + chunk * FAT_CHUNK, n)).first;
		}
		size_t k = size_t(at % FAT_CHUNK);
		return k < it->second.size() ? it->second[k] : -1;
	};
	auto word = [&](uint64_t at, int n) -> int64_t {
		int64_t v = 0;
		for (int k = 0; k < n; k++) {
			int b = byte(at + k);
			if (b < 0)
				return -1;
			v |= int64_t(b) << 8 * k;
		}
		return v;
	};
	int64_t v;
	switch (g_.bits) {
	case 12:
		v = word(c + c / 2, 2);
		if (v >= 0)
			v = c & 1 ? v >> 4 : v & 0xFFF;
		break;
	case 16: v = word(uint64_t(c) * 2, 2); break;
	default: v = word(uint64_t(c) * 4, 4); if (v >= 0) v &= 0x0FFFFFFF; break;
	}
	return v < 0 ? 0 : uint32_t(v);
}

std::vector<uint32_t> Fat::chain(uint32_t first, uint64_t max) const
{
	std::vector<uint32_t> out;
	std::unordered_set<uint32_t> seen;
	max = std::min<uint64_t>(max, g_.clusters);
	for (uint32_t c = first; valid(c) && out.size() < max && seen.insert(c).second; c = next(c))
		out.push_back(c);
	return out;
}

bool Fat::read_file(const std::vector<uint32_t> &clusters, uint64_t offset, void *buf, size_t n) const
{
	auto *out = static_cast<uint8_t *>(buf);
	while (n) {
		uint64_t k = offset / g_.cluster;
		if (k >= clusters.size())
			return false;
		size_t at = size_t(offset % g_.cluster), m = std::min<size_t>(n, g_.cluster - at);
		if (!image.read(g_.base + g_.data_at + uint64_t(clusters[k] - 2) * g_.cluster + at, out, m))
			return false;
		out += m;
		offset += m;
		n -= m;
	}
	return true;
}

// a directory's entries: its long names gathered before each short one, its subdirectories walked once each
void Fat::walk(const std::vector<uint8_t> &dir, const std::vector<uint32_t> *clusters, size_t parent, int depth)
{
	const bool root = parent == syfs::NONE;
	// a slot's place in the volume
	auto slot = [&](size_t at) {
		return clusters ? cluster_at((*clusters)[at / g_.cluster]) + at % g_.cluster : g_.root_at + at;
	};
	std::vector<uint64_t> run;
	std::vector<uint16_t> lfn;
	int expect = 0;
	uint8_t sum = 0;
	uint32_t number = 0;
	for (size_t at = 0; at + 32 <= dir.size() && entries.size() < ENTRIES; at += 32) {
		const uint8_t *e = &dir[at];
		if (!e[0])
			break;
		if (e[0] == 0xE5) {
			expect = 0;
			continue;
		}
		uint8_t attr = e[11];
		if ((attr & 0x3F) == 0x0F) {
			int ord = e[0] & 0x1F;
			if (e[0] & 0x40)
				run.clear();
			run.push_back(slot(at));
			if (e[0] & 0x40) {
				lfn.assign(size_t(ord) * 13, 0xFFFF);
				expect = ord;
				sum = e[13];
			} else if (ord != expect - 1 || e[13] != sum) {
				expect = 0;
			} else {
				expect = ord;
			}
			if (expect && ord >= 1) {
				static const int at13[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
				for (int k = 0; k < 13; k++)
					lfn[size_t(ord - 1) * 13 + k] = le16(e + at13[k]);
			} else
				expect = 0;
			continue;
		}
		bool named = expect == 1 && checksum(e) == sum;
		expect = 0;
		if (attr & 0x08) {
			if (root && label.empty())
				label = label_of(e);
			continue;
		}
		if (!memcmp(e, ".          ", 11) || !memcmp(e, "..         ", 11))
			continue;
		std::string name = named ? long_name(lfn) : short_name(e);
		uint32_t first = le16(e + 26) | (g_.bits == 32 ? uint32_t(le16(e + 20)) << 16 : 0);
		bool is_dir = attr & 0x10;
		size_t i = add(is_dir ? SYFS_TYPE_DIRECTORY : SYFS_TYPE_FILE, SYFS_FORMAT_FAT, number++, parent, name,
			is_dir ? 0 : le32(e + 28), valid(first) ? g_.base + g_.data_at + uint64_t(first - 2) * g_.cluster : 0);
		first_.push_back(first);
		// the slots it is entered in: its long name's where that is its name, and its own
		if (!named)
			run.clear();
		run.push_back(slot(at));
		slots_.push_back(run);
		run.clear();
		if (!is_dir || depth >= DEPTH || !valid(first)
			|| std::find(visited_.begin(), visited_.end(), first) != visited_.end())
			continue;
		visited_.push_back(first);
		auto clusters = chain(first, DIR_BYTES / g_.cluster + 1);
		std::vector<uint8_t> sub(clusters.size() * g_.cluster);
		if (!read_file(clusters, 0, sub.data(), sub.size()))
			sub.clear();
		walk(sub, &clusters, i, depth + 1);
	}
}

void Fat::list()
{
	auto g = find(image);
	if (!g)
		return;
	g_ = *g;
	std::vector<uint8_t> root;
	if (g_.bits == 32) {
		visited_.push_back(g_.root_cluster);
		auto clusters = chain(g_.root_cluster, DIR_BYTES / g_.cluster + 1);
		root.resize(clusters.size() * g_.cluster);
		if (!read_file(clusters, 0, root.data(), root.size()))
			root.clear();
		walk(root, &clusters, syfs::NONE, 0);
	} else {
		root = image.read(g_.base + g_.root_at, size_t(g_.root_entries) * 32);
		walk(root, nullptr, syfs::NONE, 0);
	}
	if (label.empty() && memcmp(g_.label, "NO NAME    ", 11))
		label = label_of(g_.label);
	// a file that holds sounds has them under it: an XV's .SVD, by its name and its head
	const size_t files = entries.size();
	for (size_t i = 0; i < files; i++) {
		const std::string_view name = entries[i].name;
		if (entries[i].kind != SYFS_TYPE_FILE || name.size() < 4 || name[name.size() - 4] != '.'
			|| (name[name.size() - 3] | 0x20) != 's' || (name[name.size() - 2] | 0x20) != 'v' || (name[name.size() - 1] | 0x20) != 'd')
			continue;
		const uint64_t bytes = entries[i].bytes;
		const auto clusters = chain(first_[i], (bytes + g_.cluster - 1) / g_.cluster);
		if (clusters.size() * uint64_t(g_.cluster) < bytes)
			continue;
		const roland::svd::Read read = [&](uint64_t at, void *buf, size_t n) {
			return at <= bytes && n <= bytes - at && read_file(clusters, at, buf, n);
		};
		const syfs_key whose = roland::svd::is(read, bytes);
		if (whose == SYFS_NONE)
			continue;
		roland::svd::list(read, bytes, [&](const roland::svd::Record &r) {
			add(r.kind, whose, r.number, i, r.name, r.bytes, r.at);
			first_.push_back(0);
			slots_.emplace_back();
		});
	}
}

bool Fat::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	if (!within(i) || entries[i].parent == syfs::NONE)
		return false;
	const size_t file = entries[i].parent;
	const uint64_t bytes = entries[file].bytes;
	const auto clusters = chain(first_[file], (bytes + g_.cluster - 1) / g_.cluster);
	return roland::svd::data_sets([&](uint64_t at, void *buf, size_t n) {
		return at <= bytes && n <= bytes - at && read_file(clusters, at, buf, n);
	}, entries[i].format, entries[i].kind, entries[i].number, where[i], out);
}

bool Fat::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind != SYFS_TYPE_FILE)
		return false;
	uint64_t bytes = entries[i].bytes;
	auto clusters = chain(first_[i], (bytes + g_.cluster - 1) / g_.cluster);
	if (clusters.size() * uint64_t(g_.cluster) < bytes)
		return false;
	return audio_sample([&](uint64_t offset, void *buf, size_t n) { return read_file(clusters, offset, buf, n); },
		bytes, out);
}

bool Fat::raw(size_t i, uint64_t offset, void *buf, size_t n) const
{
	const uint64_t bytes = raw_size(i);
	if (offset > bytes || n > bytes - offset)
		return false;
	return read_file(chain(first_[i], (bytes + g_.cluster - 1) / g_.cluster), offset, buf, n);
}

size_t Fat::wave(size_t i, const syfs::Sample &, uint64_t first, int16_t *out, size_t frames) const
{
	uint64_t bytes = entries[i].bytes;
	auto clusters = chain(first_[i], (bytes + g_.cluster - 1) / g_.cluster);
	return audio_wave([&](uint64_t offset, void *buf, size_t n) { return read_file(clusters, offset, buf, n); }, bytes,
		first, out, frames);
}

std::unique_ptr<Medium> Fat::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Fat>(io, format);
}

}
