// Copyright 2026 Ian Karlsson
// A FAT volume written: files and directories made, removed, named and moved (docs/formats.md, "FAT")
#include "fat.hpp"

#include <cctype>
#include <ctime>

namespace syfs {

namespace {

constexpr size_t SLOT = 32;
constexpr uint8_t FREE = 0xE5, DIRECTORY = 0x10, ARCHIVE = 0x20;

void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

// a name's characters, from UTF-8; false where it is not UTF-8
bool utf16(std::string_view s, std::vector<uint16_t> &out)
{
	for (size_t k = 0; k < s.size();) {
		const uint8_t c = uint8_t(s[k]);
		const int more = c < 0x80 ? 0 : c >> 5 == 6 ? 1 : c >> 4 == 14 ? 2 : c >> 3 == 30 ? 3 : -1;
		if (more < 0 || k + size_t(more) >= s.size() + (more ? 0 : 1))
			return false;
		uint32_t v = more == 0 ? c : c & (0x3F >> more);
		for (int m = 1; m <= more; m++) {
			if ((uint8_t(s[k + m]) & 0xC0) != 0x80)
				return false;
			v = v << 6 | (uint8_t(s[k + m]) & 0x3F);
		}
		k += size_t(more) + 1;
		if (v >= 0x10000) {
			v -= 0x10000;
			out.push_back(uint16_t(0xD800 + (v >> 10)));
			out.push_back(uint16_t(0xDC00 + (v & 0x3FF)));
		} else
			out.push_back(uint16_t(v));
	}
	return true;
}

// what a short name may hold besides letters and digits
bool short_char(uint8_t c)
{
	return isupper(c) || isdigit(c) || (c && strchr("!#$%&'()-@^_`{}~", c));
}

// a name as the eleven bytes of a short one, where it is one as it stands: upper-case, eight and three
bool as_short(std::string_view name, uint8_t *out)
{
	const size_t dot = name.rfind('.');
	const std::string_view base = name.substr(0, dot), ext = dot == std::string_view::npos ? "" : name.substr(dot + 1);
	if (base.empty() || base.size() > 8 || ext.size() > 3 || (dot != std::string_view::npos && ext.empty()))
		return false;
	memset(out, ' ', 11);
	for (size_t k = 0; k < base.size(); k++)
		if (!short_char(uint8_t(out[k] = uint8_t(base[k]))))
			return false;
	for (size_t k = 0; k < ext.size(); k++)
		if (!short_char(uint8_t(out[8 + k] = uint8_t(ext[k]))))
			return false;
	return true;
}

// the short name a long one is given: its letters upper-cased and what a short name cannot hold left out, six of
// its base, a tilde and a number, and three of its extension
void alias(std::string_view name, unsigned number, uint8_t *out)
{
	const size_t dot = name.rfind('.');
	const std::string_view base = name.substr(0, dot), ext = dot == std::string_view::npos || !dot ? "" : name.substr(dot + 1);
	auto fill = [](std::string_view from, uint8_t *to, size_t max) {
		size_t n = 0;
		for (char ch : from) {
			const uint8_t c = uint8_t(toupper(uint8_t(ch)));
			if (n < max && short_char(c))
				to[n++] = c;
		}
		return n;
	};
	memset(out, ' ', 11);
	char tail[12];
	const size_t t = size_t(snprintf(tail, sizeof tail, "~%u", number));
	size_t n = fill(base, out, 8 - t);
	if (!n)
		out[n++] = '_';
	memcpy(out + n, tail, t);
	fill(ext, out + 8, 3);
}

uint8_t sum_of(const uint8_t *e)
{
	uint8_t sum = 0;
	for (int k = 0; k < 11; k++)
		sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + e[k]);
	return sum;
}

bool same_name(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return tolower(uint8_t(x)) == tolower(uint8_t(y));
	});
}

}

bool Fat::link(uint32_t c, uint32_t value)
{
	const uint64_t fats = (g_.root_at - g_.fat_at) / g_.fat_bytes;
	uint64_t at;
	uint8_t b[4];
	size_t n;
	if (g_.bits == 12) {
		at = c + c / 2;
		n = 2;
		if (!image.read(g_.base + g_.fat_at + at, b, 2))
			return false;
		const uint32_t old = b[0] | b[1] << 8;
		put16(b, c & 1 ? (old & 0x000F) | (value & 0xFFF) << 4 : (old & 0xF000) | (value & 0xFFF));
	} else if (g_.bits == 16) {
		at = uint64_t(c) * 2;
		n = 2;
		put16(b, value);
	} else {
		at = uint64_t(c) * 4;
		n = 4;
		if (!image.read(g_.base + g_.fat_at + at, b, 4))
			return false;
		put32(b, (uint32_t(b[3] & 0xF0) << 24) | (value & 0x0FFFFFFF));
	}
	for (uint64_t f = 0; f < fats; f++)
		if (!put(g_.fat_at + f * g_.fat_bytes + at, b, n))
			return false;
	// what is kept of the FAT here is read again
	fat_cache_.erase(at / 4096);
	fat_cache_.erase((at + n - 1) / 4096);
	// FAT32 keeps a count of free clusters, which is now not known
	if (g_.bits == 32 && !fsinfo_stale_) {
		uint8_t bs[512], info[512];
		fsinfo_stale_ = true;
		if (image.read(g_.base, bs, sizeof bs)) {
			const uint64_t sector = uint64_t(bs[48] | bs[49] << 8) * g_.sector;
			if (sector && sector < g_.fat_at && image.read(g_.base + sector, info, sizeof info) && !memcmp(info, "RRaA", 4)
				&& !memcmp(info + 484, "rrAa", 4)) {
				memset(info + 488, 0xFF, 8);
				put(sector, info, sizeof info);
			}
		}
	}
	return true;
}

std::vector<uint32_t> Fat::take(uint64_t n)
{
	std::vector<uint32_t> got;
	const uint32_t count = g_.clusters;
	for (uint32_t tried = 0; tried < count && got.size() < n; tried++) {
		const uint32_t c = 2 + (hint_ - 2 + tried) % count;
		if (!next(c))
			got.push_back(c);
	}
	bool ok = got.size() == n;
	const uint32_t end = g_.bits == 12 ? 0xFFF : g_.bits == 16 ? 0xFFFF : 0x0FFFFFFF;
	// from the last back, so that a chain is whole from wherever it is entered
	for (size_t k = got.size(); ok && k--;)
		ok = link(got[k], k + 1 < got.size() ? got[k + 1] : end);
	if (!ok) {
		for (uint32_t c : got)
			link(c, 0);
		return {};
	}
	if (!got.empty())
		hint_ = got.back() + 1 < count + 2 ? got.back() + 1 : 2;
	return got;
}

bool Fat::release(uint32_t first)
{
	bool ok = true;
	for (uint32_t c : chain(first, g_.clusters))
		ok = link(c, 0) && ok;
	hint_ = 2;
	return ok;
}

std::vector<uint64_t> Fat::slots_of(size_t parent) const
{
	std::vector<uint64_t> out;
	const bool fixed = parent == syfs::NONE && g_.bits != 32;
	if (fixed) {
		for (uint32_t k = 0; k < g_.root_entries; k++)
			out.push_back(g_.root_at + uint64_t(k) * SLOT);
		return out;
	}
	const uint32_t first = parent == syfs::NONE ? g_.root_cluster : first_[parent];
	for (uint32_t c : chain(first, 65536 * SLOT / g_.cluster + 1))
		for (uint32_t at = 0; at < g_.cluster; at += SLOT)
			out.push_back(cluster_at(c) + at);
	return out;
}

std::vector<uint64_t> Fat::room(size_t parent, size_t n, std::string &why)
{
	std::vector<uint64_t> all = slots_of(parent), run;
	bool ended = false;
	for (uint64_t at : all) {
		uint8_t first = 0;
		if (!ended && !image.read(g_.base + at, &first, 1))
			break;
		ended = ended || !first;
		if (ended || first == FREE)
			run.push_back(at);
		else
			run.clear();
		if (run.size() == n)
			return run;
	}
	// a directory that is a chain takes another cluster, zeros, after its last
	const bool fixed = parent == syfs::NONE && g_.bits != 32;
	if (fixed || all.size() + g_.cluster / SLOT > 65536) {
		why = fixed ? "the root directory is full" : "the directory is full";
		return {};
	}
	const uint32_t first = parent == syfs::NONE ? g_.root_cluster : first_[parent];
	auto old = chain(first, g_.clusters);
	auto more = take(1);
	std::vector<uint8_t> zeros(g_.cluster, 0);
	if (old.empty() || more.empty() || !put(cluster_at(more[0]), zeros.data(), zeros.size()) || !link(old.back(), more[0])) {
		if (!more.empty())
			link(more[0], 0);
		why = "the volume is full";
		return {};
	}
	return room(parent, n, why);
}

bool Fat::taken(size_t parent, std::string_view name, size_t but) const
{
	for (size_t i = 0; i < entries.size(); i++)
		if (i != but && !gone(i) && entries[i].parent == parent && same_name(entries[i].name, name))
			return true;
	return false;
}

std::vector<uint64_t> Fat::enter(size_t parent, std::string_view name, const uint8_t *entry, std::string &why)
{
	std::vector<uint16_t> wide;
	if (name.empty() || name == "." || name == ".." || name.size() >= sizeof(syfs::Entry::name) || !utf16(name, wide)
		|| wide.size() > 255) {
		why = "a name the volume cannot hold";
		return {};
	}
	for (uint16_t c : wide)
		if (c < 0x20 || (c < 0x80 && strchr("\"*/:<>?\\|", int(c)))) {
			why = "a name the volume cannot hold";
			return {};
		}
	uint8_t e[SLOT];
	memcpy(e, entry, SLOT);
	e[12] = 0;
	// a short name as it stands needs no more; any other is a long name over a short one made for it, which no
	// entry of the directory has yet
	const bool brief = as_short(name, e);
	if (!brief) {
		std::vector<uint64_t> all = slots_of(parent);
		std::vector<uint8_t> used;
		for (uint64_t at : all) {
			uint8_t s[11];
			if (!image.read(g_.base + at, s, sizeof s) || !s[0])
				break;
			used.insert(used.end(), s, s + 11);
		}
		for (unsigned number = 1;; number++) {
			alias(name, number, e);
			bool free = true;
			for (size_t k = 0; free && k < used.size(); k += 11)
				free = memcmp(&used[k], e, 11) != 0;
			if (free)
				break;
			if (number > 999999) {
				why = "the directory has no short name left for it";
				return {};
			}
		}
	}
	const size_t longs = brief ? 0 : (wide.size() + 12) / 13;
	std::vector<uint64_t> slots = room(parent, longs + 1, why);
	if (slots.empty())
		return {};
	// the long name's slots, its last part first, each 13 characters: the name, a zero, then ones
	static const int at13[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
	const uint8_t sum = sum_of(e);
	bool ok = true;
	for (size_t k = 0; ok && k < longs; k++) {
		const size_t ord = longs - k;
		uint8_t l[SLOT] = {};
		l[0] = uint8_t(ord | (k ? 0 : 0x40));
		l[11] = 0x0F;
		l[13] = sum;
		for (size_t c = 0; c < 13; c++) {
			const size_t w = (ord - 1) * 13 + c;
			put16(l + at13[c], w < wide.size() ? wide[w] : w == wide.size() ? 0 : 0xFFFF);
		}
		ok = put(slots[k], l, SLOT);
	}
	// its own entry last: until it is there, the long name's slots are no one's
	if (!ok || !put(slots[longs], e, SLOT)) {
		why = "the image could not be written";
		return {};
	}
	return slots;
}

bool Fat::vacate(const std::vector<uint64_t> &slots)
{
	bool ok = true;
	// its own entry first, so that no long name is left over an entry still there
	for (size_t k = slots.size(); k--;)
		ok = put(slots[k], &FREE, 1) && ok;
	return ok;
}

bool Fat::under(size_t i, size_t maybe_ancestor) const
{
	for (size_t p = i; p != syfs::NONE; p = entries[p].parent)
		if (p == maybe_ancestor)
			return true;
	return false;
}

size_t Fat::create(size_t parent, syfs_key type, std::string_view name, uint64_t bytes, const Source &from,
	std::string &why)
{
	const bool dir = type == SYFS_TYPE_DIRECTORY;
	if (!dir && type != SYFS_TYPE_FILE)
		return why = "a FAT volume holds files and directories", syfs::NONE;
	if (parent != syfs::NONE && (gone(parent) || entries[parent].kind != SYFS_TYPE_DIRECTORY))
		return why = "not a directory to put it in", syfs::NONE;
	if (taken(parent, name, syfs::NONE))
		return why = "the directory has an entry of that name", syfs::NONE;
	if (!dir && bytes > 0xFFFFFFFFu)
		return why = "a file larger than a FAT volume holds", syfs::NONE;
	// its clusters and what is in them first; entered in the directory last, it is nowhere until it is whole
	auto clusters = take(dir ? 1 : (bytes + g_.cluster - 1) / g_.cluster);
	if (clusters.empty() && (dir || bytes))
		return why = "the volume is full", syfs::NONE;
	const uint32_t first = clusters.empty() ? 0 : clusters[0];
	const time_t now = time(nullptr);
	const struct tm *t = localtime(&now);
	const uint32_t date = t && t->tm_year >= 80 ? uint32_t((t->tm_year - 80) << 9 | (t->tm_mon + 1) << 5 | t->tm_mday) : 0x21,
		clock = t ? uint32_t(t->tm_hour << 11 | t->tm_min << 5 | t->tm_sec / 2) : 0;
	uint8_t e[SLOT] = {};
	e[11] = dir ? DIRECTORY : ARCHIVE;
	put16(e + 14, clock), put16(e + 16, date), put16(e + 18, date), put16(e + 22, clock), put16(e + 24, date);
	put16(e + 20, first >> 16), put16(e + 26, first);
	put32(e + 28, dir ? 0 : uint32_t(bytes));
	bool ok = true;
	std::vector<uint8_t> buf(g_.cluster, 0);
	if (dir) {
		// its two entries: itself, and the directory it is in (zero for the root)
		const uint32_t up = parent == syfs::NONE ? 0 : first_[parent];
		memcpy(&buf[0], e, SLOT), memcpy(&buf[0], ".          ", 11);
		memcpy(&buf[SLOT], e, SLOT), memcpy(&buf[SLOT], "..         ", 11);
		put16(&buf[SLOT + 20], up >> 16), put16(&buf[SLOT + 26], up);
		ok = put(cluster_at(first), buf.data(), buf.size());
	} else
		for (size_t k = 0; ok && k < clusters.size(); k++) {
			const uint64_t at = uint64_t(k) * g_.cluster;
			const size_t n = size_t(std::min<uint64_t>(g_.cluster, bytes - at));
			std::fill(buf.begin(), buf.end(), uint8_t(0));
			ok = from && from(at, buf.data(), n) && put(cluster_at(clusters[k]), buf.data(), buf.size());
			if (!ok)
				why = "the bytes for it could not be read, or the image written";
		}
	std::vector<uint64_t> slots;
	if (ok)
		slots = enter(parent, name, e, why);
	if (slots.empty()) {
		if (first)
			release(first);
		if (why.empty())
			why = "the image could not be written";
		return syfs::NONE;
	}
	uint32_t number = 0;
	for (size_t i = 0; i < entries.size(); i++)
		number += !gone(i) && entries[i].parent == parent;
	const size_t i = add(type, SYFS_FORMAT_FAT, number, parent, name, dir ? 0 : bytes, first ? g_.base + cluster_at(first) : 0);
	first_.push_back(first);
	slots_.push_back(slots);
	return i;
}

bool Fat::remove(size_t i, std::string &why)
{
	// what it holds first, so that nothing is left that no directory enters
	for (size_t k = 0; k < entries.size(); k++)
		if (!gone(k) && entries[k].parent == i && !remove(k, why))
			return false;
	if (!vacate(slots_[i]) || (valid(first_[i]) && !release(first_[i])))
		return why = "the image could not be written", false;
	dead.resize(entries.size(), false);
	dead[i] = true;
	return true;
}

bool Fat::rename(size_t i, std::string_view name, std::string &why)
{
	if (same_name(entries[i].name, name) && entries[i].name == name)
		return true;
	if (taken(entries[i].parent, name, i))
		return why = "the directory has an entry of that name", false;
	// the entry as it is, entered again under the new name; then the old slots given up
	uint8_t e[SLOT];
	if (!image.read(g_.base + slots_[i].back(), e, SLOT))
		return why = "the image could not be read", false;
	const std::vector<uint64_t> old = slots_[i];
	std::vector<uint64_t> slots = enter(entries[i].parent, name, e, why);
	if (slots.empty())
		return false;
	vacate(old);
	slots_[i] = slots;
	snprintf(entries[i].name, sizeof entries[i].name, "%.*s", int(name.size()), name.data());
	return true;
}

bool Fat::move(size_t i, size_t parent, std::string &why)
{
	if (parent == entries[i].parent)
		return true;
	if (parent != syfs::NONE && (gone(parent) || entries[parent].kind != SYFS_TYPE_DIRECTORY))
		return why = "not a directory to put it in", false;
	if (parent != syfs::NONE && under(parent, i))
		return why = "a directory cannot go into itself", false;
	if (taken(parent, entries[i].name, i))
		return why = "the directory has an entry of that name", false;
	uint8_t e[SLOT];
	if (!image.read(g_.base + slots_[i].back(), e, SLOT))
		return why = "the image could not be read", false;
	const std::vector<uint64_t> old = slots_[i];
	std::vector<uint64_t> slots = enter(parent, entries[i].name, e, why);
	if (slots.empty())
		return false;
	vacate(old);
	slots_[i] = slots;
	entries[i].parent = parent;
	// a directory's second entry names the one it is in
	if (entries[i].kind == SYFS_TYPE_DIRECTORY && valid(first_[i])) {
		const uint32_t up = parent == syfs::NONE ? 0 : first_[parent];
		uint8_t b[2];
		put16(b, up >> 16);
		put(cluster_at(first_[i]) + SLOT + 20, b, 2);
		put16(b, up);
		put(cluster_at(first_[i]) + SLOT + 26, b, 2);
	}
	return true;
}

}
