// Copyright 2026 Ian Karlsson
// Each format's reader on a small image made here: what is listed, what is left out, what entries use
#include "../src/akai/akai.hpp"
#include "../src/fat.hpp"
#include "../src/roland/roland.hpp"

#include <cstdio>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

static int failures = 0;
#define CHECK(c) \
	do { \
		if (!(c)) { \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
			failures++; \
		} \
	} while (0)

using Bytes = std::vector<uint8_t>;

static size_t mem_read(void *user, uint64_t offset, void *buf, size_t n)
{
	auto *b = static_cast<Bytes *>(user);
	if (offset >= b->size())
		return 0;
	n = std::min<size_t>(n, b->size() - offset);
	memcpy(buf, b->data() + offset, n);
	return n;
}

static syfs_io io_of(Bytes &b)
{
	return { &b, mem_read, nullptr, b.size() };
}

// the FAT probe on its own leaves every other format's image alone, floppies of DOS sizes among them
static bool not_fat(Bytes &b)
{
	syfs_io io = io_of(b);
	return syfs::Fat::probe(syfs::Image(io)) == SYFS_NONE;
}

// ---- the interface as the cases below ask of it: nodes by their index from 0, an entry and a sample's header whole
static const size_t NONE = size_t(-1);

static int64_t num(syfs_image *fs, syfs_node n, syfs_key key, int64_t none = 0)
{
	int64_t v = none;
	syfs_get_int(fs, n, key, &v);
	return v;
}

static syfs_key probe(const syfs_io *io, syfs_key *medium)
{
	if (medium)
		*medium = SYFS_NONE;
	syfs_image *fs = io ? syfs_open(io, nullptr, 0) : nullptr;
	if (!fs)
		return SYFS_NONE;
	syfs_key format = syfs_key(num(fs, SYFS_ROOT, SYFS_PROP_FORMAT));
	if (medium)
		*medium = syfs_key(num(fs, SYFS_ROOT, SYFS_PROP_MEDIUM));
	syfs_close(fs);
	return format;
}

static std::map<syfs_image *, std::vector<syfs::Entry>> listed;
static std::map<syfs_image *, std::string> labels;

static const std::vector<syfs::Entry> &entries_of(syfs_image *fs)
{
	auto it = listed.find(fs);
	if (it != listed.end())
		return it->second;
	std::vector<syfs::Entry> &v = listed[fs];
	std::vector<syfs_node> nodes(syfs_list(fs, SYFS_ROOT, SYFS_REL_ALL, nullptr, 0));
	CHECK(syfs_list(fs, SYFS_ROOT, SYFS_REL_ALL, nodes.data(), nodes.size()) == nodes.size());
	for (size_t i = 0; i < nodes.size(); i++) {
		syfs_info info;
		syfs::Entry e {};
		CHECK(nodes[i] == i + 1 && syfs_node_info(fs, nodes[i], &info));
		e.kind = info.type;
		e.format = syfs_key(num(fs, nodes[i], SYFS_PROP_FORMAT));
		e.number = info.number;
		e.parent = info.parent == SYFS_ROOT ? NONE : info.parent - 1;
		snprintf(e.name, sizeof e.name, "%s", info.name);
		syfs_get_str(fs, nodes[i], SYFS_PROP_TAG, e.tag, sizeof e.tag);
		e.bytes = info.size;
		char name[64];
		CHECK(syfs_get_str(fs, nodes[i], SYFS_PROP_NAME, name, sizeof name) && !strcmp(name, e.name));
		CHECK(num(fs, nodes[i], SYFS_PROP_TYPE) == e.kind && num(fs, nodes[i], SYFS_PROP_NUMBER) == e.number
			&& uint64_t(num(fs, nodes[i], SYFS_PROP_SIZE)) == e.bytes);
		v.push_back(e);
	}
	// every property listed reads as its kind says, each key once
	for (size_t n = 0; n <= v.size(); n++) {
		syfs_prop prop;
		std::vector<syfs_key> keys;
		for (size_t k = 0; syfs_prop_at(fs, syfs_node(n), k, &prop); k++) {
			int64_t value;
			char text[256];
			CHECK(std::find(keys.begin(), keys.end(), prop.key) == keys.end() && syfs_name(prop.key));
			keys.push_back(prop.key);
			CHECK(syfs_get_int(fs, syfs_node(n), prop.key, &value) == (prop.kind != SYFS_KIND_STRING));
			CHECK(syfs_get_str(fs, syfs_node(n), prop.key, text, sizeof text) == (prop.kind != SYFS_KIND_INT));
			CHECK(!prop.writable && !syfs_set_int(fs, syfs_node(n), prop.key, 0) && *syfs_error(fs));
		}
		CHECK(keys.size() >= (n ? 5u : 3u) && keys[0] == (n ? SYFS_PROP_NAME : SYFS_PROP_FORMAT));
		CHECK(!syfs_get_int(fs, syfs_node(n), syfs_key_of("no such property"), nullptr));
	}
	// each node is among its parent's children, in order, and nowhere else
	std::vector<std::vector<syfs_node>> kids(v.size() + 1);
	for (size_t i = 0; i < v.size(); i++)
		kids[v[i].parent == NONE ? 0 : v[i].parent + 1].push_back(syfs_node(i + 1));
	for (size_t n = 0; n <= v.size(); n++) {
		std::vector<syfs_node> got(syfs_list(fs, syfs_node(n), SYFS_REL_CHILDREN, nullptr, 0));
		syfs_list(fs, syfs_node(n), SYFS_REL_CHILDREN, got.data(), got.size());
		CHECK(got == kids[n]);
	}
	return v;
}

static size_t count_of(syfs_image *fs) { return entries_of(fs).size(); }

static const syfs::Entry *entry_at(syfs_image *fs, size_t i)
{
	const auto &v = entries_of(fs);
	return i < v.size() ? &v[i] : nullptr;
}

static const char *label_of(syfs_image *fs)
{
	char label[256] = "";
	CHECK(syfs_get_str(fs, SYFS_ROOT, SYFS_PROP_LABEL, label, sizeof label));
	return (labels[fs] = label).c_str();
}

// a string property of the image's, "" where it has none
static std::string text_of(syfs_image *fs, syfs_key key)
{
	char text[256] = "";
	syfs_get_str(fs, SYFS_ROOT, key, text, sizeof text);
	return text;
}

static syfs_key format_of(syfs_image *fs) { return syfs_key(num(fs, SYFS_ROOT, SYFS_PROP_FORMAT)); }
static syfs_key medium_of(syfs_image *fs) { return syfs_key(num(fs, SYFS_ROOT, SYFS_PROP_MEDIUM)); }

static void close_image(syfs_image *fs)
{
	listed.erase(fs);
	labels.erase(fs);
	syfs_close(fs);
}

static size_t refs(syfs_image *fs, size_t i, size_t *out, size_t max)
{
	std::vector<syfs_node> v(max);
	size_t n = syfs_list(fs, syfs_node(i + 1), SYFS_REL_USES, v.data(), max);
	for (size_t k = 0; k < n && k < max; k++)
		out[k] = v[k] - 1;
	return n;
}

static int sample_info(syfs_image *fs, size_t i, syfs::Sample *out)
{
	syfs_node n = syfs_node(i + 1);
	int64_t rate;
	if (!syfs_get_int(fs, n, SYFS_PROP_RATE, &rate))
		return 0;
	*out = { uint32_t(rate), uint32_t(num(fs, n, SYFS_PROP_CHANNELS)), uint32_t(num(fs, n, SYFS_PROP_BITS)),
		int32_t(num(fs, n, SYFS_PROP_KEY)), uint64_t(num(fs, n, SYFS_PROP_FRAMES)), uint64_t(num(fs, n, SYFS_PROP_START)),
		uint64_t(num(fs, n, SYFS_PROP_END)), uint64_t(num(fs, n, SYFS_PROP_LOOP_START)),
		uint64_t(num(fs, n, SYFS_PROP_LOOP_END)), syfs_key(num(fs, n, SYFS_PROP_LOOP)), int(num(fs, n, SYFS_PROP_REVERSE)),
		int(num(fs, n, SYFS_PROP_MODE)) };
	return 1;
}

static size_t sample_at(syfs_image *fs, size_t i, int key)
{
	syfs_node n = syfs_at_key(fs, syfs_node(i + 1), key);
	return n == SYFS_NO_NODE ? NONE : n - 1;
}

static size_t sample_read(syfs_image *fs, size_t i, uint64_t first, int16_t *out, size_t frames)
{
	size_t frame = 2 * size_t(num(fs, syfs_node(i + 1), SYFS_PROP_CHANNELS, 1));
	return syfs_read(fs, syfs_node(i + 1), SYFS_AS_S16, first * frame, out, frames * frame) / frame;
}

static int make(syfs_key format, syfs_key medium, const syfs_io *io, const char *label, char *err, size_t err_size)
{
	return syfs_create(io, format, medium, label, err, err_size);
}

static int make(syfs_key format, syfs_key medium, const syfs_io *io, char *err, size_t err_size)
{
	return syfs_create(io, format, medium, nullptr, err, err_size);
}

static void put16(Bytes &b, size_t at, uint16_t v)
{
	b[at] = uint8_t(v);
	b[at + 1] = uint8_t(v >> 8);
}

static void put32(Bytes &b, size_t at, uint32_t v)
{
	put16(b, at, uint16_t(v));
	put16(b, at + 2, uint16_t(v >> 16));
}

static void put_name(Bytes &b, size_t at, const char *name, size_t len)
{
	memset(&b[at], ' ', len);
	memcpy(&b[at], name, strlen(name));
}

static std::vector<std::string> names(syfs_image *fs, syfs_key kind)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < count_of(fs); i++)
		if (entry_at(fs, i)->kind == kind)
			out.push_back(entry_at(fs, i)->name);
	return out;
}

// the first entry of the name, of the kind where one is given
static size_t find(syfs_image *fs, const char *name, syfs_key kind = SYFS_NONE)
{
	for (size_t i = 0; i < count_of(fs); i++)
		if (!strcmp(entry_at(fs, i)->name, name) && (!kind || entry_at(fs, i)->kind == kind))
			return i;
	return NONE;
}

// up to frames frames of entry i's wave from first, as read
static std::vector<int16_t> wave(syfs_image *fs, size_t i, uint64_t first, size_t frames, size_t channels = 1)
{
	std::vector<int16_t> v(frames * channels);
	v.resize(sample_read(fs, i, first, v.data(), frames) * channels);
	return v;
}

static std::vector<std::string> used(syfs_image *fs, size_t i)
{
	size_t out[64];
	size_t n = refs(fs, i, out, 64);
	std::vector<std::string> v;
	for (size_t k = 0; k < n && k < 64; k++)
		v.push_back(entry_at(fs, out[k])->name);
	return v;
}

static std::vector<size_t> refs_of(syfs_image *fs, size_t i)
{
	size_t out[64];
	size_t n = refs(fs, i, out, 64);
	return std::vector<size_t>(out, out + std::min<size_t>(n, 64));
}

// the keys of each split of a board's wave, as its sample entries' tags give them
static std::vector<std::string> splits(syfs_image *fs, size_t wave)
{
	std::vector<std::string> v;
	for (size_t c : refs_of(fs, wave))
		v.push_back(entry_at(fs, c)->tag);
	return v;
}

// ---- S-7xx: one of each list, a gap, a slot past the count, and the references between them
static Bytes s700_image(const char *system)
{
	Bytes b((0x15AC + 18 * 4) * 512, 0);
	memcpy(&b[4], "S770 MR25A", 10);
	put_name(b, 0x20, system, 32);
	const uint16_t count[5] = { 1, 1, 1, 2, 3 };
	for (int l = 0; l < 5; l++)
		put16(b, 0x114 + 2 * l, count[l]);
	auto entry = [&](uint32_t dir, int n, const char *name, uint16_t segs) {
		size_t at = dir * 512 + n * 32;
		put_name(b, at, name, 16);
		b[at + 16] = 0x44;
		put16(b, at + 28, 2);
		put16(b, at + 30, segs);
	};
	entry(0x504, 0, "VOL:One", 0);
	entry(0x50C, 0, "PRF:One", 0);
	entry(0x52C, 0, "STR:Strings", 0);
	entry(0x56C, 0, "PRT:Low", 0);
	entry(0x56C, 1, "PRT:High", 0);
	entry(0x66C, 0, "SMP:Low", 2);
	b[0x66C * 512 + 32] = 0x01;  // slot 1 deleted: its name not printable
	entry(0x66C, 2, "SMP:High", 1);
	entry(0x66C, 3, "SMP:Stale", 1);  // past the header's count
	// the volume's performance 0 (the other 63 slots out of range), the performance's patch 0 in two parts
	for (int k = 0; k < 64; k++)
		put16(b, 0x86C * 512 + 32 + 2 * k, k ? 0xFFFF : 0);
	for (int k = 0; k < 32; k++)
		put16(b, 0x8AC * 512 + 0x100 + 2 * k, k < 2 ? 0 : 0xFFFF);
	// the patch: keys 0-43 partial 1, keys 44-87 partial 0, key 87 off
	size_t patch = 0xAAC * 512;
	for (int k = 0; k < 88; k++) {
		b[patch + 32 + k] = k == 87 ? 255 : 0;
		put16(b, patch + 0x100 + 2 * k, k == 87 ? 7 : k < 44 ? 1 : 0);
	}
	// partial 0: sample 0 and the deleted slot 1; partial 1: sample 2, the others off
	for (int p = 0; p < 2; p++)
		for (int k = 0; k < 4; k++)
			put16(b, 0xEAC * 512 + p * 128 + 16 + 16 * k, p == 0 ? (k == 0 ? 0 : k == 1 ? 1 : 0xFFFF) : (k == 0 ? 2 : 0xFFFF));
	// SMP:Low's wave in segments 3 then 5, SMP:High's in 2; segment n's word k is -(1000n + k mod 100)
	put16(b, 0x66C * 512 + 28, 3);
	put16(b, 0x404 * 512 + 2 * 3, 5);
	put16(b, 0x404 * 512 + 2 * 5, 0xFFFF);
	put16(b, 0x404 * 512 + 2 * 2, 0xFFF8);
	for (uint32_t n = 2; n < 6; n++)
		for (uint32_t k = 0; k < 4608; k++)
			put16(b, (0x15AC + 18 * (n - 2)) * 512 + 2 * k, uint16_t(-int(1000 * n + k % 100)));
	size_t smp = 0x12AC * 512 + 2 * 48;
	b[smp + 44] = 1;
	b[smp + 45] = 72;
	const uint32_t points[5] = { 10, 100, 199, 250, 299 };
	for (int k = 0; k < 5; k++)
		put32(b, smp + 16 + 4 * k, points[k] << 8 | 0x80);
	b[smp + 36] = 4;
	return b;
}

static void test_s700(Bytes b, syfs_key format, const char *label)
{
	syfs_io io = io_of(b);
	CHECK(probe(&io, nullptr) == format);
	char err[64];
	syfs_image *fs = syfs_open(&io, err, sizeof err);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(label_of(fs), label));
	CHECK(format_of(fs) == format && entry_at(fs, 0)->format == format);
	CHECK((names(fs, SYFS_TYPE_SAMPLE) == std::vector<std::string> { "SMP:Low", "SMP:High" }));
	CHECK((names(fs, SYFS_TYPE_PARTIAL) == std::vector<std::string> { "PRT:Low", "PRT:High" }));
	size_t strings = find(fs, "STR:Strings");
	CHECK(strings != NONE && !strcmp(entry_at(fs, strings)->tag, "STR"));
	CHECK(!entry_at(fs, find(fs, "PRT:Low"))->tag[0]);
	CHECK((used(fs, find(fs, "VOL:One")) == std::vector<std::string> { "PRF:One" }));
	CHECK((used(fs, find(fs, "PRF:One")) == std::vector<std::string> { "STR:Strings" }));
	CHECK((used(fs, strings) == std::vector<std::string> { "PRT:High", "PRT:Low" }));
	CHECK((used(fs, find(fs, "PRT:Low")) == std::vector<std::string> { "SMP:Low" }));
	CHECK(used(fs, find(fs, "SMP:Low")).empty());
	// keys 21-64 partial 1, 65-107 partial 0, 108 off; a partial's first layer naming a sample
	size_t lo = find(fs, "SMP:Low"), hi = find(fs, "SMP:High");
	CHECK(sample_at(fs, strings, 21) == hi && sample_at(fs, strings, 64) == hi);
	CHECK(sample_at(fs, strings, 65) == lo && sample_at(fs, strings, 107) == lo);
	CHECK(sample_at(fs, strings, 108) == NONE && sample_at(fs, strings, 20) == NONE);
	CHECK(sample_at(fs, find(fs, "PRT:High"), 0) == hi && sample_at(fs, lo, 60) == lo);
	CHECK(sample_at(fs, find(fs, "PRF:One"), 60) == NONE);
	CHECK(sample_at(fs, find(fs, "VOL:One"), 60) == NONE);
	size_t high = find(fs, "SMP:High");
	CHECK(entry_at(fs, high)->number == 2 && entry_at(fs, high)->bytes == 9216);
	syfs::Sample s;
	CHECK(sample_info(fs, high, &s) && s.rate == 44100 && s.key == 72 && s.frames == 4608);
	CHECK(s.start == 10 && s.end == 200 && s.loop == SYFS_LOOP_ALTERNATE && s.loop_start == 100 && s.loop_end == 200);
	CHECK(s.mode == 4 && !s.reverse && s.channels == 1 && s.bits == 16);
	CHECK(!sample_info(fs, strings, &s));
	CHECK((wave(fs, high, 0, 2) == std::vector<int16_t> { -2000, -2001 }));
	size_t low = find(fs, "SMP:Low");
	auto join = wave(fs, low, 4606, 4);
	CHECK((join == std::vector<int16_t> { -3006, -3007, -5000, -5001 }));
	CHECK(wave(fs, low, 0, 10000).size() == 9216 && wave(fs, low, 9214, 100).size() == 2 && wave(fs, low, 9216, 1).empty());
	CHECK(wave(fs, strings, 0, 1).empty());
	close_image(fs);
}

// the same image as raw 2352-byte sectors of mode 1
static Bytes raw_of(const Bytes &b)
{
	Bytes r;
	for (size_t at = 0; at < b.size(); at += 2048) {
		const uint8_t head[16] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0, 0, 0, 1 };
		size_t n = std::min<size_t>(2048, b.size() - at);
		r.insert(r.end(), head, head + 16);
		r.insert(r.end(), b.begin() + at, b.begin() + at + n);
		r.resize(r.size() + (2048 - n) + 288, 0);
	}
	return r;
}

// ---- AKAI: an S1000 partition and an S3000 one whose directory runs into a second block
constexpr uint64_t AKAI_BLOCK = 8192;

static void akai_name(Bytes &b, size_t at, const char *name)
{
	static const char set[] = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";
	for (int i = 0; i < 12; i++) {
		char c = i < int(strlen(name)) ? name[i] : ' ';
		b[at + i] = uint8_t(strchr(set, c) - set);
	}
}

static void akai_partition(Bytes &b, uint64_t base, uint16_t blocks, const std::function<void(uint64_t)> &fill)
{
	put16(b, base, blocks);
	for (uint32_t k = 0; k < syfs::akai::HEADER_WORDS; k++) {
		b[base + 2 + 2 * k] = uint8_t(k * syfs::akai::HEADER_STEP);
		b[base + 3 + 2 * k] = uint8_t(k * syfs::akai::HEADER_STEP >> 8);
	}
	fill(base);
}

static void akai_file(Bytes &b, size_t at, const char *name, uint8_t type, uint32_t size, uint16_t start)
{
	akai_name(b, at, name);
	b[at + 16] = type;
	b[at + 17] = uint8_t(size);
	b[at + 18] = uint8_t(size >> 8);
	b[at + 19] = uint8_t(size >> 16);
	put16(b, at + 20, start);
}

// a program: a header and keygroups of block bytes each, keygroup k keys 60k to 60k + 59, each zone naming a sample
static void akai_program(Bytes &b, size_t at, size_t block, const std::vector<std::vector<const char *>> &keygroups)
{
	b[at] = 1;
	b[at + 42] = uint8_t(keygroups.size());
	for (size_t k = 0; k < keygroups.size(); k++) {
		size_t kg = at + (k + 1) * block;
		b[kg] = 2;
		b[kg + 3] = uint8_t(60 * k), b[kg + 4] = uint8_t(std::min<size_t>(60 * k + 59, 127));
		for (int z = 0; z < 4; z++)
			akai_name(b, kg + 34 + 24 * z, z < int(keygroups[k].size()) ? keygroups[k][z] : "");
	}
}

static Bytes akai_image(bool s3000_partition)
{
	Bytes b(20 * AKAI_BLOCK, 0);
	akai_partition(b, 0, 8, [&](uint64_t base) {
		akai_name(b, base + 0xCA, "PIANO");
		b[base + 0xCA + 12] = 1;
		put16(b, base + 0xCA + 14, 3);
		size_t dir = base + 3 * AKAI_BLOCK;
		akai_file(b, dir, "PIANO PROG", 'p', 450, 4);
		akai_file(b, dir + 24, "PIANO C3", 's', 150 + 2 * 500, 5);
		akai_file(b, dir + 126 * 24, "VOLUME DATA", 's', 1, 6);  // past an S1000 directory's 126
		akai_program(b, base + 4 * AKAI_BLOCK, 150, { { "PIANO C3", "", "PIANO C3" }, { "MISSING" } });
		size_t smp = base + 5 * AKAI_BLOCK;
		b[smp] = 3;
		b[smp + 2] = 60;
		b[smp + 0x1A] = 0xF4;
		b[smp + 0x1B] = 0x01;
		put16(b, smp + 0x8A, 22050);
		b[smp + 0x10] = 1;
		put32(b, smp + 0x1E, 2);
		put32(b, smp + 0x22, 499);
		put32(b, smp + 0x26, 400);
		put32(b, smp + 0x2C, 100);
		for (uint32_t k = 0; k < 500; k++)
			put16(b, smp + 150 + 2 * k, uint16_t(k * 131 - 30000));
	});
	if (s3000_partition)
		akai_partition(b, 8 * AKAI_BLOCK, 12, [&](uint64_t base) {
			akai_name(b, base + 0xCA, "STRINGS");
			b[base + 0xCA + 12] = 3;
			put16(b, base + 0xCA + 14, 3);
			put16(b, base + 0x70A + 2 * 3, 5);  // the directory's second block
			akai_file(b, base + 3 * AKAI_BLOCK, "STRINGS", 0xF0, 600, 6);
			akai_file(b, base + 3 * AKAI_BLOCK + 24, "FX", 'x', 100, 7);
			akai_file(b, base + 5 * AKAI_BLOCK + (400 * 24 - AKAI_BLOCK), "STRINGS A4", 0xF3, 2000, 7);
			akai_program(b, base + 6 * AKAI_BLOCK, 192, { { "", "STRINGS A4" } });
			// a program of two blocks out of order (8, then 10), its last keygroup in the second
			std::vector<std::vector<const char *>> kgs(49, std::vector<const char *> { "" });
			kgs.push_back({ "STRINGS A4" });
			Bytes long_program(2 * AKAI_BLOCK, 0);
			akai_program(long_program, 0, 192, kgs);
			std::copy_n(long_program.begin(), AKAI_BLOCK, b.begin() + base + 8 * AKAI_BLOCK);
			std::copy_n(long_program.begin() + AKAI_BLOCK, AKAI_BLOCK, b.begin() + base + 10 * AKAI_BLOCK);
			akai_file(b, base + 3 * AKAI_BLOCK + 48, "LONG", 0xF0, 51 * 192, 8);
			put16(b, base + 0x70A + 2 * 8, 10);
			put16(b, base + 0x70A + 2 * 10, 0xC000);
			// a multi whose parts name a program of the volume, one of another, and none
			akai_file(b, base + 3 * AKAI_BLOCK + 72, "SPLIT", 0xED, 4096, 11);
			akai_name(b, base + 11 * AKAI_BLOCK + 0x400 + 3, "LONG");
			akai_name(b, base + 11 * AKAI_BLOCK + 0x400 + 0xC0 + 3, "PIANO PROG");
			akai_name(b, base + 11 * AKAI_BLOCK + 0x400 + 2 * 0xC0 + 3, "");
			akai_name(b, base + 11 * AKAI_BLOCK + 0x400 + 15 * 0xC0 + 3, "STRINGS");
		});
	return b;
}

static void test_akai()
{
	Bytes b = akai_image(true);
	syfs_io io = io_of(b);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_AKAI_S3000);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK((names(fs, SYFS_TYPE_PARTITION) == std::vector<std::string> { "A", "B" }));
	CHECK((names(fs, SYFS_TYPE_SAMPLE) == std::vector<std::string> { "PIANO C3", "STRINGS A4" }));
	CHECK((names(fs, SYFS_TYPE_PROGRAM) == std::vector<std::string> { "PIANO PROG", "STRINGS", "LONG" }));
	CHECK((names(fs, SYFS_TYPE_MULTI) == std::vector<std::string> { "SPLIT" }));
	CHECK((names(fs, SYFS_TYPE_OTHER) == std::vector<std::string> { "FX" }));
	size_t piano = find(fs, "PIANO"), c3 = find(fs, "PIANO C3"), a4 = find(fs, "STRINGS A4");
	CHECK(entry_at(fs, c3)->parent == piano && entry_at(fs, piano)->parent == find(fs, "A"));
	CHECK(entry_at(fs, piano)->format == SYFS_FORMAT_AKAI_S1000 && entry_at(fs, find(fs, "A"))->format == SYFS_FORMAT_AKAI_S1000);
	CHECK(entry_at(fs, a4)->format == SYFS_FORMAT_AKAI_S3000 && entry_at(fs, a4)->number == 400);
	CHECK(entry_at(fs, find(fs, "B"))->format == SYFS_FORMAT_AKAI_S3000);
	CHECK(!strcmp(entry_at(fs, c3)->tag, "s") && !strcmp(entry_at(fs, a4)->tag, "s"));
	CHECK(!strcmp(entry_at(fs, find(fs, "FX"))->tag, "x"));
	CHECK((used(fs, find(fs, "PIANO PROG")) == std::vector<std::string> { "PIANO C3" }));
	// the first keygroup holding the key, its first zone found; the second keygroup's sample is missing
	size_t prog = find(fs, "PIANO PROG");
	CHECK(sample_at(fs, prog, 0) == c3 && sample_at(fs, prog, 59) == c3);
	CHECK(sample_at(fs, prog, 60) == NONE && sample_at(fs, prog, 128) == NONE);
	CHECK(sample_at(fs, find(fs, "STRINGS", SYFS_TYPE_PROGRAM), 60) == NONE);
	CHECK(sample_at(fs, find(fs, "STRINGS", SYFS_TYPE_PROGRAM), 30) == a4);
	CHECK(sample_at(fs, c3, 100) == c3 && sample_at(fs, find(fs, "SPLIT"), 60) == NONE);
	CHECK(sample_at(fs, piano, 60) == NONE);
	CHECK((used(fs, find(fs, "STRINGS", SYFS_TYPE_PROGRAM)) == std::vector<std::string> { "STRINGS A4" }));
	CHECK((used(fs, find(fs, "LONG")) == std::vector<std::string> { "STRINGS A4" }));
	CHECK((used(fs, find(fs, "SPLIT")) == std::vector<std::string> { "LONG", "STRINGS" }));
	CHECK(!strcmp(entry_at(fs, find(fs, "SPLIT"))->tag, "m"));
	syfs::Sample s;
	CHECK(sample_info(fs, c3, &s) && s.rate == 22050 && s.key == 60 && s.frames == 500);
	CHECK(s.start == 2 && s.end == 500 && s.loop == SYFS_LOOP_FORWARD && s.loop_start == 300 && s.loop_end == 400);
	CHECK(!sample_info(fs, a4, &s));  // no header
	CHECK((wave(fs, c3, 0, 2) == std::vector<int16_t> { -30000, -29869 }));
	CHECK((wave(fs, c3, 499, 5) == std::vector<int16_t> { int16_t(499 * 131 - 30000) }));
	CHECK(wave(fs, a4, 0, 1).empty());
	close_image(fs);

	b = akai_image(false);
	io = io_of(b);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_AKAI_S1000);

	// a size out of range is no partition
	b = akai_image(true);
	put16(b, 0, 0x1E01);
	io = io_of(b);
	CHECK(probe(&io, nullptr) == SYFS_NONE);
}

// ---- S-550: a directory of one area, its patch using tones by two key maps
static void put_be(Bytes &b, size_t at, uint32_t v, int n)
{
	for (int i = 0; i < n; i++)
		b[at + i] = uint8_t(v >> (8 * (n - 1 - i)));
}

static void test_s550()
{
	const uint32_t start = 4, length = 18 + 48;
	Bytes b((start + length) * 512, 0);
	memcpy(&b[0], "* ROLAND S-550 *", 16);
	put_name(b, 0x20, "Roland Sound Library    L-CD9", 32);
	memset(&b[512], 0xFF, 3 * 512);
	put_name(b, 512, "Pianos #1", 32);
	put_be(b, 512 + 0x30, start, 4);
	put_be(b, 512 + 0x34, length, 4);
	b[512 + 0x38] = 'A';
	b[512 + 0x39] = 0;
	memcpy(&b[512 + 64], &b[512], 64);  // past the count of one
	put_name(b, 512 + 64, "Stale", 32);
	b[0x13A] = 1;
	size_t area = start * 512;
	for (int k = 0; k < 16; k++)
		put_name(b, area + k * 256, "", 12);
	put_name(b, area, "Piano Split", 12);
	b[area + 0x0F] = 1;
	for (int k = 0; k < 109; k++) {
		b[area + 0x1A + k] = k < 50 ? 0 : 0xDF;
		b[area + 0x87 + k] = k < 50 ? 0xDF : 2;
	}
	for (int k = 0; k < 32; k++)
		put_name(b, area + 0x1200 + k * 128, "", 8);
	size_t t0 = area + 0x1200, t2 = t0 + 2 * 128;
	put_name(b, t0, "P-C3", 8);
	b[t0 + 0x0C] = 60;
	b[t0 + 0x0F] = 1;
	put_be(b, t0 + 0x13, 0x2FFF, 3);
	put_be(b, t0 + 0x16, 0x1000, 3);
	put_name(b, t2, "P-C5", 8);
	b[t2 + 0x0B] = 1;
	b[t2 + 0x0C] = 84;
	b[t2 + 0x0F] = 1;
	put_be(b, t2 + 0x10, 0x100, 3);
	put_be(b, t2 + 0x13, 0x1FF, 3);
	b[t2 + 0x19] = 3;
	for (uint32_t k = 0; k < 0x3000; k++)
		put16(b, area + 18 * 512 + 2 * k, uint16_t((int(k % 4096) - 2048) * 16));

	syfs_io io = io_of(b);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_S550);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(label_of(fs), "Roland Sound Library    L-CD9"));
	CHECK((names(fs, SYFS_TYPE_AREA) == std::vector<std::string> { "Pianos #1" }));
	CHECK((names(fs, SYFS_TYPE_PATCH) == std::vector<std::string> { "Piano Split" }));
	CHECK((names(fs, SYFS_TYPE_TONE) == std::vector<std::string> { "P-C3", "P-C5" }));
	size_t pianos = find(fs, "Pianos #1"), c3 = find(fs, "P-C3"), c5 = find(fs, "P-C5");
	CHECK(!strcmp(entry_at(fs, pianos)->tag, "0") && entry_at(fs, c5)->number == 2);
	CHECK(entry_at(fs, c3)->parent == pianos && entry_at(fs, c3)->bytes == 24576);
	CHECK((used(fs, find(fs, "Piano Split")) == std::vector<std::string> { "P-C3", "P-C5" }));
	// the first key map, its first byte key 21: tone 0 on keys 21-70, the second map's tone 2 left out
	size_t split = find(fs, "Piano Split");
	CHECK(sample_at(fs, split, 21) == c3 && sample_at(fs, split, 70) == c3);
	CHECK(sample_at(fs, split, 71) == NONE && sample_at(fs, split, 20) == NONE);
	CHECK(sample_at(fs, c5, 60) == c5 && sample_at(fs, pianos, 60) == NONE);
	syfs::Sample s;
	CHECK(sample_info(fs, c3, &s) && s.rate == 30000 && s.key == 60 && s.frames == 0x3000);
	CHECK(s.start == 0 && s.end == 0x3000 && s.loop == SYFS_LOOP_FORWARD && s.loop_start == 0x1000 && s.loop_end == 0x3000);
	CHECK(sample_info(fs, c5, &s) && s.rate == 15000 && s.key == 84 && s.frames == 0x3000);
	CHECK(s.start == 0x100 && s.end == 0x200 && s.reverse && s.loop == SYFS_NONE && s.mode == 3);
	CHECK(s.bits == 12 && s.channels == 1);
	CHECK((wave(fs, c3, 4095, 2) == std::vector<int16_t> { 2047 * 16, -2048 * 16 }));
	CHECK(wave(fs, c5, 0, 0x4000).size() == 0x3000);
	close_image(fs);
}

// ---- WAV and AIFF files, as a file system holds them
static void put_be32(Bytes &b, size_t at, uint32_t v)
{
	put_be(b, at, v, 4);
}

// a mono 16-bit WAV: fmt, data, then smpl (after the data, so a reader must find it past the wave)
static Bytes wav_file(uint32_t frames, uint32_t rate, uint8_t key, uint32_t loop_start, uint32_t loop_end)
{
	Bytes b(12 + 24 + 8 + 2 * frames + 8 + 60, 0);
	memcpy(&b[0], "RIFF", 4);
	put32(b, 4, uint32_t(b.size() - 8));
	memcpy(&b[8], "WAVE", 4);
	memcpy(&b[12], "fmt ", 4);
	put32(b, 16, 16);
	put16(b, 20, 1), put16(b, 22, 1), put32(b, 24, rate), put32(b, 28, rate * 2), put16(b, 32, 2), put16(b, 34, 16);
	memcpy(&b[36], "data", 4);
	put32(b, 40, 2 * frames);
	for (uint32_t k = 0; k < frames; k++)
		put16(b, 44 + 2 * k, uint16_t(k * 37));
	size_t s = 44 + 2 * frames;
	memcpy(&b[s], "smpl", 4);
	put32(b, s + 4, 60);
	put32(b, s + 8 + 12, key);
	put32(b, s + 8 + 28, 1);
	put32(b, s + 8 + 36 + 4, 0);
	put32(b, s + 8 + 36 + 8, loop_start);
	put32(b, s + 8 + 36 + 12, loop_end - 1);
	return b;
}

// an AIFF of frames at 22050 Hz, its sustain loop between two markers, alternating
static Bytes aiff_file(uint32_t frames, uint8_t key, uint32_t loop_start, uint32_t loop_end)
{
	Bytes b(12 + 26 + 28 + 28, 0);
	memcpy(&b[0], "FORM", 4);
	put_be32(b, 4, uint32_t(b.size() - 8));
	memcpy(&b[8], "AIFF", 4);
	memcpy(&b[12], "COMM", 4);
	put_be32(b, 16, 18);
	put_be(b, 20, 1, 2), put_be32(b, 22, frames), put_be(b, 26, 16, 2);
	const uint8_t rate[10] = { 0x40, 0x0D, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 };  // 22050
	memcpy(&b[28], rate, 10);
	memcpy(&b[38], "MARK", 4);
	put_be32(b, 42, 20);
	put_be(b, 46, 2, 2);
	put_be(b, 48, 1, 2), put_be32(b, 50, loop_start), b[54] = 1, b[55] = 'a';
	put_be(b, 56, 2, 2), put_be32(b, 58, loop_end), b[62] = 1, b[63] = 'b';
	memcpy(&b[66], "INST", 4);
	put_be32(b, 70, 20);
	b[74] = key;
	put_be(b, 82, 2, 2), put_be(b, 84, 1, 2), put_be(b, 86, 2, 2);
	return b;
}

// ---- ISO 9660: the audio files and the directories holding them
static void iso_record(Bytes &b, size_t &at, const char *name, size_t n, uint32_t extent, uint32_t size, bool dir)
{
	size_t len = (33 + n + 1) & ~size_t(1);
	b[at] = uint8_t(len);
	b[at + 2] = uint8_t(extent);
	b[at + 10] = uint8_t(size);
	b[at + 11] = uint8_t(size >> 8);
	b[at + 12] = uint8_t(size >> 16);
	b[at + 25] = dir ? 2 : 0;
	b[at + 32] = uint8_t(n);
	memcpy(&b[at + 33], name, n);
	at += len;
}

static void iso_dir(Bytes &b, uint32_t extent, uint32_t parent, const std::vector<std::pair<const char *, uint32_t>> &files)
{
	size_t at = extent * 2048;
	iso_record(b, at, "\0", 1, extent, 2048, true);
	iso_record(b, at, "\1", 1, parent, 2048, true);
	for (auto [name, ext] : files) {
		bool dir = !strchr(name, '.');
		iso_record(b, at, name, strlen(name), ext, dir ? 2048 : 1234, dir);
	}
}

static void test_iso()
{
	Bytes b(25 * 2048, 0);
	memcpy(&b[16 * 2048], "\1CD001", 6);
	put_name(b, 16 * 2048 + 40, "LOOPS", 32);
	size_t root = 16 * 2048 + 156;
	b[root + 2] = 20;
	b[root + 11] = 0x08;
	iso_dir(b, 20, 20, { { "README.TXT;1", 23 }, { "DRUMS", 21 }, { "DOCS", 22 } });
	iso_dir(b, 21, 20, { { "KICK.WAV;1", 23 }, { "SNARE.AIF;1", 24 } });
	Bytes wav = wav_file(500, 32000, 36, 10, 400), aiff = aiff_file(300, 64, 20, 250);
	std::copy(wav.begin(), wav.end(), b.begin() + 23 * 2048);
	std::copy(aiff.begin(), aiff.end(), b.begin() + 24 * 2048);
	iso_dir(b, 22, 20, { { "INFO.TXT;1", 23 } });
	syfs_io io = io_of(b);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_ISO9660 && not_fat(b));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(label_of(fs), "LOOPS"));
	CHECK((names(fs, SYFS_TYPE_DIRECTORY) == std::vector<std::string> { "DRUMS" }));
	CHECK((names(fs, SYFS_TYPE_FILE) == std::vector<std::string> { "KICK.WAV", "SNARE.AIF" }));
	size_t kick = find(fs, "KICK.WAV");
	CHECK(entry_at(fs, kick)->bytes == 1234 && entry_at(fs, kick)->parent == find(fs, "DRUMS"));
	CHECK(entry_at(fs, kick)->format == SYFS_FORMAT_ISO9660);
	syfs::Sample s;
	CHECK(sample_info(fs, kick, &s) && s.rate == 32000 && s.key == 36 && s.frames == 500 && s.end == 500);
	CHECK(s.loop == SYFS_LOOP_FORWARD && s.loop_start == 10 && s.loop_end == 400 && s.mode == 0);
	CHECK(s.channels == 1 && s.bits == 16);
	CHECK((wave(fs, kick, 499, 4) == std::vector<int16_t> { int16_t(499 * 37) }));
	CHECK(sample_info(fs, find(fs, "SNARE.AIF"), &s) && s.rate == 22050 && s.key == 64 && s.frames == 300);
	CHECK(wave(fs, find(fs, "SNARE.AIF"), 0, 1).empty());  // no sound data chunk
	CHECK(s.loop == SYFS_LOOP_ALTERNATE && s.loop_start == 20 && s.loop_end == 250 && s.mode == 2);
	CHECK(!sample_info(fs, find(fs, "DRUMS"), &s));
	close_image(fs);
}

// ---- WAV and AIFF encodings: two channels of 40 frames, value c of frame k given back as expected(k, c)
static int16_t expected(uint32_t k, uint32_t c)
{
	return int16_t(k * 523 + c * 7919);
}

using PutValue = std::function<void(uint8_t *, int16_t)>;

static Bytes wav_of(uint16_t tag, bool extensible, uint16_t bits, uint32_t width, const PutValue &put)
{
	const uint32_t frames = 40, channels = 2, fmt = extensible ? 40 : 16, data = frames * channels * width;
	Bytes b(12 + 8 + fmt + 8 + data, 0);
	memcpy(&b[0], "RIFF", 4);
	put32(b, 4, uint32_t(b.size() - 8));
	memcpy(&b[8], "WAVE", 4);
	memcpy(&b[12], "fmt ", 4);
	put32(b, 16, fmt);
	put16(b, 20, extensible ? 0xFFFE : tag), put16(b, 22, channels), put32(b, 24, 48000);
	put32(b, 28, 48000 * channels * width), put16(b, 32, uint16_t(channels * width)), put16(b, 34, bits);
	if (extensible)
		put16(b, 36, 22), put16(b, 38, bits), put16(b, 44, tag);
	size_t at = 20 + fmt;
	memcpy(&b[at], "data", 4);
	put32(b, at + 4, data);
	for (uint32_t k = 0; k < frames; k++)
		for (uint32_t c = 0; c < channels; c++)
			put(&b[at + 8 + (k * channels + c) * width], expected(k, c));
	return b;
}

// an AIFF, or an AIFC of the compression type given
static Bytes aiff_of(const char *compression, uint16_t bits, uint32_t width, const PutValue &put)
{
	const uint32_t frames = 40, channels = 2, comm = compression ? 24 : 18, data = frames * channels * width;
	Bytes b(12 + 8 + comm + 16 + data, 0);
	memcpy(&b[0], "FORM", 4);
	put_be32(b, 4, uint32_t(b.size() - 8));
	memcpy(&b[8], compression ? "AIFC" : "AIFF", 4);
	memcpy(&b[12], "COMM", 4);
	put_be32(b, 16, comm);
	put_be(b, 20, channels, 2), put_be32(b, 22, frames), put_be(b, 26, bits, 2);
	const uint8_t rate[10] = { 0x40, 0x0E, 0xBB, 0x80, 0, 0, 0, 0, 0, 0 };  // 48000
	memcpy(&b[28], rate, 10);
	if (compression)
		memcpy(&b[38], compression, 4);
	size_t at = 20 + comm;
	memcpy(&b[at], "SSND", 4);
	put_be32(b, at + 4, 8 + data);
	for (uint32_t k = 0; k < frames; k++)
		for (uint32_t c = 0; c < channels; c++)
			put(&b[at + 16 + (k * channels + c) * width], expected(k, c));
	return b;
}

static void test_encodings()
{
	auto le = [](uint32_t width, uint32_t low) {
		return [=](uint8_t *p, int16_t v) {
			for (uint32_t k = 0; k + 2 < width; k++)
				p[k] = uint8_t(low >> 8 * k);
			p[width - 2] = uint8_t(v), p[width - 1] = uint8_t(uint16_t(v) >> 8);
		};
	};
	auto be16 = [](uint8_t *p, int16_t v) { p[0] = uint8_t(uint16_t(v) >> 8), p[1] = uint8_t(v); };
	auto f32 = [](bool big) {
		return [=](uint8_t *p, int16_t v) {
			float f = v / 32768.0f;
			uint32_t u;
			memcpy(&u, &f, 4);
			for (int k = 0; k < 4; k++)
				p[big ? 3 - k : k] = uint8_t(u >> 8 * k);
		};
	};
	auto f64 = [](uint8_t *p, int16_t v) {
		double f = v / 32768.0;
		uint64_t u;
		memcpy(&u, &f, 8);
		for (int k = 0; k < 8; k++)
			p[k] = uint8_t(u >> 8 * k);
	};
	struct File {
		const char *name;
		Bytes b;
		uint32_t bits;
		bool eight;  // only the top eight bits kept
	};
	std::vector<File> files {
		{ "U8.WAV", wav_of(1, false, 8, 1, [](uint8_t *p, int16_t v) { p[0] = uint8_t((v >> 8) + 128); }), 8, true },
		{ "S24.WAV", wav_of(1, false, 24, 3, le(3, 0x5A)), 24, false },
		{ "S32.WAV", wav_of(1, true, 32, 4, le(4, 0xA55A)), 32, false },
		{ "F32.WAV", wav_of(3, true, 32, 4, f32(false)), 32, false },
		{ "F64.WAV", wav_of(3, false, 64, 8, f64), 64, false },
		{ "S16.AIF", aiff_of(nullptr, 16, 2, be16), 16, false },
		{ "S8.AIF", aiff_of(nullptr, 8, 1, [](uint8_t *p, int16_t v) { p[0] = uint8_t(v >> 8); }), 8, true },
		{ "SOWT.AIF", aiff_of("sowt", 16, 2, le(2, 0)), 16, false },
		{ "FL32.AIF", aiff_of("fl32", 32, 4, f32(true)), 32, false },
		{ "ULAW.WAV", wav_of(7, false, 8, 1, [](uint8_t *p, int16_t) { p[0] = 0xFF; }), 8, false },
	};
	Bytes b((21 + files.size()) * 2048, 0);
	memcpy(&b[16 * 2048], "\1CD001", 6);
	size_t root = 16 * 2048 + 156;
	b[root + 2] = 20;
	b[root + 11] = 0x08;
	std::vector<std::pair<const char *, uint32_t>> dir;
	for (size_t k = 0; k < files.size(); k++) {
		dir.emplace_back(files[k].name, uint32_t(21 + k));
		std::copy(files[k].b.begin(), files[k].b.end(), b.begin() + (21 + k) * 2048);
	}
	iso_dir(b, 20, 20, dir);
	syfs_io io = io_of(b);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	for (const File &f : files) {
		size_t i = find(fs, f.name);
		syfs::Sample s;
		CHECK(sample_info(fs, i, &s) && s.channels == 2 && s.bits == f.bits && s.frames == 40 && s.rate == 48000);
		auto got = wave(fs, i, 3, 100, 2);
		if (!strcmp(f.name, "ULAW.WAV")) {
			CHECK(got.empty());
			continue;
		}
		bool same = got.size() == 2 * 37;
		for (uint32_t k = 0; same && k < 37; k++)
			for (uint32_t c = 0; c < 2; c++)
				same = same && got[2 * k + c] == int16_t(f.eight ? expected(k + 3, c) & 0xFF00 : expected(k + 3, c));
		if (!same)
			fprintf(stderr, "%s read wrong\n", f.name);
		CHECK(same);
	}
	close_image(fs);
}

// ---- FAT: images sparse in memory, a volume laid out by hand
struct Sparse {
	static constexpr uint64_t PAGE = 4096;
	uint64_t size;
	std::map<uint64_t, std::vector<uint8_t>> pages;
	explicit Sparse(uint64_t size) : size(size) {}
	// a page holding only zeros is not stored
	void put(uint64_t at, const void *p, size_t n)
	{
		auto *s = static_cast<const uint8_t *>(p);
		while (n) {
			size_t k = std::min<size_t>(n, PAGE - at % PAGE);
			auto it = pages.find(at / PAGE);
			if (it == pages.end() && std::all_of(s, s + k, [](uint8_t b) { return !b; })) {
				at += k, s += k, n -= k;
				continue;
			}
			if (it == pages.end())
				it = pages.emplace(at / PAGE, std::vector<uint8_t>(PAGE, 0)).first;
			memcpy(&it->second[at % PAGE], s, k);
			at += k, s += k, n -= k;
		}
	}
	void get(uint64_t at, void *p, size_t n) const
	{
		auto *d = static_cast<uint8_t *>(p);
		while (n) {
			size_t k = std::min<size_t>(n, PAGE - at % PAGE);
			auto it = pages.find(at / PAGE);
			if (it == pages.end())
				memset(d, 0, k);
			else
				memcpy(d, &it->second[at % PAGE], k);
			at += k, d += k, n -= k;
		}
	}
	void put16(uint64_t at, uint16_t v) { uint8_t b[2] = { uint8_t(v), uint8_t(v >> 8) }; put(at, b, 2); }
	void put32(uint64_t at, uint32_t v) { put16(at, uint16_t(v)), put16(at + 2, uint16_t(v >> 16)); }
	uint16_t get16(uint64_t at) const { uint8_t b[2]; get(at, b, 2); return uint16_t(b[0] | b[1] << 8); }
	uint32_t get32(uint64_t at) const { return get16(at) | uint32_t(get16(at + 2)) << 16; }
};

static size_t sparse_read(void *user, uint64_t offset, void *buf, size_t n)
{
	auto *s = static_cast<Sparse *>(user);
	if (offset >= s->size)
		return 0;
	n = size_t(std::min<uint64_t>(n, s->size - offset));
	s->get(offset, buf, n);
	return n;
}

static syfs_io io_of(Sparse &s)
{
	return { &s, sparse_read, nullptr, s.size };
}

struct FatVolume {
	Sparse &img;
	uint64_t base;
	int bits;
	uint32_t spc, reserved, root_entries, fat_sectors;
	uint64_t total;
	uint8_t media;
	uint32_t fats = 2, free_cluster = 3;

	uint64_t fat_at() const { return base + uint64_t(reserved) * 512; }
	uint64_t root_at() const { return fat_at() + uint64_t(fats) * fat_sectors * 512; }
	uint64_t data_at() const { return root_at() + (uint64_t(root_entries) * 32 + 511) / 512 * 512; }
	uint64_t cluster_at(uint32_t c) const { return data_at() + uint64_t(c - 2) * spc * 512; }
	uint32_t bytes() const { return spc * 512; }

	void boot(const char *label)
	{
		uint8_t s[512] = { 0xEB, 0x3C, 0x90 };
		memcpy(s + 3, "TESTFAT ", 8);
		s[11] = 0, s[12] = 2, s[13] = uint8_t(spc), s[14] = uint8_t(reserved), s[16] = uint8_t(fats);
		s[17] = uint8_t(root_entries), s[18] = uint8_t(root_entries >> 8);
		if (total < 65536)
			s[19] = uint8_t(total), s[20] = uint8_t(total >> 8);
		else
			for (int k = 0; k < 4; k++)
				s[32 + k] = uint8_t(total >> 8 * k);
		s[21] = media;
		uint8_t *ext = s + 36;
		if (bits == 32) {
			for (int k = 0; k < 4; k++)
				s[36 + k] = uint8_t(fat_sectors >> 8 * k);
			s[44] = 2;
			ext = s + 64;
		} else
			s[22] = uint8_t(fat_sectors), s[23] = uint8_t(fat_sectors >> 8);
		ext[2] = 0x29;
		memset(ext + 7, ' ', 11);
		memcpy(ext + 7, label, strlen(label));
		s[510] = 0x55, s[511] = 0xAA;
		img.put(base, s, 512);
		set(0, 0x0FFFFF00 | media);
		set(1, 0x0FFFFFFF);
		if (bits == 32)
			set(2, 0x0FFFFFFF);
	}
	void set(uint32_t c, uint32_t v)
	{
		for (uint32_t f = 0; f < fats; f++) {
			uint64_t at = fat_at() + uint64_t(f) * fat_sectors * 512;
			if (bits == 12) {
				uint16_t w = img.get16(at + c + c / 2);
				w = c & 1 ? uint16_t((w & 0x000F) | (v & 0xFFF) << 4) : uint16_t((w & 0xF000) | (v & 0xFFF));
				img.put16(at + c + c / 2, w);
			} else if (bits == 16)
				img.put16(at + 2 * c, uint16_t(v));
			else
				img.put32(at + 4 * c, v & 0x0FFFFFFF);
		}
	}
	uint32_t eoc() const { return bits == 12 ? 0xFFF : bits == 16 ? 0xFFFF : 0x0FFFFFFF; }
	// data written over clusters in the order given, chained so
	void write(const std::vector<uint32_t> &chain, const Bytes &data)
	{
		for (size_t k = 0; k < chain.size(); k++) {
			set(chain[k], k + 1 < chain.size() ? chain[k + 1] : eoc());
			size_t from = k * bytes();
			if (from < data.size())
				img.put(cluster_at(chain[k]), &data[from], std::min<size_t>(bytes(), data.size() - from));
		}
	}
	std::vector<uint32_t> take(size_t n)
	{
		std::vector<uint32_t> c;
		for (size_t k = 0; k < n; k++)
			c.push_back(free_cluster++);
		return c;
	}
};

// a directory's entries, 32 bytes each, long names before their short ones
struct FatDir {
	Bytes b;
	void entry(const char *short11, uint8_t attr, uint32_t cluster, uint32_t size)
	{
		Bytes e(32, 0);
		memcpy(&e[0], short11, 11);
		e[11] = attr;
		put16(e, 20, uint16_t(cluster >> 16));
		put16(e, 26, uint16_t(cluster));
		put32(e, 28, size);
		b.insert(b.end(), e.begin(), e.end());
	}
	void named(const std::u16string &name, const char *short11, uint8_t attr, uint32_t cluster, uint32_t size)
	{
		uint8_t sum = 0;
		for (int k = 0; k < 11; k++)
			sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + uint8_t(short11[k]));
		size_t parts = (name.size() + 12) / 13;
		static const int at13[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
		for (size_t p = parts; p >= 1; p--) {
			Bytes e(32, 0);
			e[0] = uint8_t(p | (p == parts ? 0x40 : 0));
			e[11] = 0x0F;
			e[13] = sum;
			for (int k = 0; k < 13; k++) {
				size_t c = (p - 1) * 13 + k;
				put16(e, at13[k], c < name.size() ? name[c] : c == name.size() ? 0 : 0xFFFF);
			}
			b.insert(b.end(), e.begin(), e.end());
		}
		entry(short11, attr, cluster, size);
	}
};

// a volume with a label, README.TXT, a deleted file, Samples/Drums/ and a WAV of several clusters in reverse order;
// where damaged, Drums also holds a directory pointing at Drums, one pointing past the volume, a file whose chain
// runs back on itself, and Samples' chain loops onto itself
static void fat_fill(FatVolume &v, bool damaged, bool label_in_root)
{
	v.boot(label_in_root ? "NO NAME" : "BPB LABEL");
	auto readme = v.take(1), samples = v.take(1), drums = v.take(1);
	Bytes wav = wav_file(2 * v.bytes(), 44100, 60, 100, 900);
	size_t wav_clusters = (wav.size() + v.bytes() - 1) / v.bytes();
	auto kick = v.take(wav_clusters);
	std::reverse(kick.begin(), kick.end());
	Bytes text(100, 'x');
	v.write(readme, text);

	FatDir root;
	if (label_in_root)
		root.entry("TEST VOLUME", 0x08, 0, 0);
	root.entry("README  TXT", 0x20, readme[0], 100);
	root.named(u"Gone.wav", "\xE5ONE    WAV", 0x20, 0, 0);
	root.b[root.b.size() - 64] = 0xE5;  // the long name's entry deleted too
	root.named(u"Samples", "SAMPLES    ", 0x10, samples[0], 0);
	FatDir sd;
	sd.entry(".          ", 0x10, samples[0], 0);
	sd.entry("..         ", 0x10, 0, 0);
	sd.entry("DRUMS      ", 0x10, drums[0], 0);
	FatDir dd;
	dd.entry(".          ", 0x10, drums[0], 0);
	dd.entry("..         ", 0x10, samples[0], 0);
	dd.named(u"Kick drum – long name.wav", "KICKDR~1WAV", 0x20, kick[0], uint32_t(wav.size()));
	// a long name whose checksum does not match its short entry: the short name is used
	dd.named(u"Wrong checksum.wav", "WRONG~1 WAV", 0x20, 0, 0);
	dd.b[dd.b.size() - 64 + 13] ^= 1;
	dd.b[dd.b.size() - 96 + 13] ^= 1;
	if (damaged) {
		auto loop = v.take(1);
		dd.entry("SELF       ", 0x10, drums[0], 0);
		dd.entry("FAR        ", 0x10, 0x0FFFFF0, 0);
		dd.entry("LOOP    WAV", 0x20, loop[0], 1 << 20);
		v.write(loop, wav);
		v.set(loop[0], loop[0]);
	}
	if (v.bits == 32) {
		v.write({ 2 }, root.b);
	} else
		v.img.put(v.root_at(), root.b.data(), root.b.size());
	v.write(samples, sd.b);
	if (damaged)
		v.set(samples[0], samples[0]);
	v.write(drums, dd.b);
	v.write(kick, wav);
}

static void test_fat_volume(Sparse &img, syfs_key medium, bool damaged, const char *label)
{
	syfs_io io = io_of(img);
	syfs_key m;
	CHECK(probe(&io, &m) == SYFS_FORMAT_FAT && m == medium);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(format_of(fs) == SYFS_FORMAT_FAT && medium_of(fs) == medium);
	CHECK(!strcmp(label_of(fs), label));
	const char *kick_name = "Kick drum \xE2\x80\x93 long name.wav";
	std::vector<std::string> dirs { "Samples", "DRUMS" }, files { "README.TXT", kick_name, "WRONG~1.WAV" };
	if (damaged) {
		dirs.push_back("SELF");
		dirs.push_back("FAR");
		files.push_back("LOOP.WAV");
	}
	CHECK(names(fs, SYFS_TYPE_DIRECTORY) == dirs);
	CHECK(names(fs, SYFS_TYPE_FILE) == files);
	size_t samples = find(fs, "Samples"), drums = find(fs, "DRUMS"), kick = find(fs, kick_name);
	CHECK(entry_at(fs, samples)->parent == NONE && entry_at(fs, samples)->number == 1);
	CHECK(entry_at(fs, drums)->parent == samples);
	CHECK(kick != NONE && entry_at(fs, kick)->parent == drums);
	CHECK(entry_at(fs, find(fs, "README.TXT"))->bytes == 100);
	syfs::Sample s;
	CHECK(sample_info(fs, kick, &s) && s.rate == 44100 && s.key == 60);
	CHECK(s.frames == entry_at(fs, kick)->bytes / 2 - 56 && s.end == s.frames);
	CHECK(s.loop == SYFS_LOOP_FORWARD && s.loop_start == 100 && s.loop_end == 900);
	auto all = wave(fs, kick, 0, size_t(s.frames));
	bool same = all.size() == s.frames;
	for (size_t k = 0; same && k < all.size(); k++)
		same = all[k] == int16_t(k * 37);
	CHECK(same);
	CHECK(!sample_info(fs, find(fs, "README.TXT"), &s) && !sample_info(fs, drums, &s));
	if (damaged)
		CHECK(!sample_info(fs, find(fs, "LOOP.WAV"), &s));
	close_image(fs);
}

static void test_fat()
{
	// FAT12: a 1.44 MB floppy, the label in the root directory
	for (bool damaged : { false, true }) {
		Sparse img(2880 * 512);
		FatVolume v { img, 0, 12, 1, 1, 224, 9, 2880, 0xF0 };
		fat_fill(v, damaged, true);
		test_fat_volume(img, SYFS_MEDIUM_FLOPPY, damaged, "TEST VOLUME");
	}
	// FAT16: a 10 MB disk behind an MBR, its partition at sector 63 the second of the table; the label the BPB's
	for (bool damaged : { false, true }) {
		const uint64_t sectors = 20480, start = 63;
		Sparse img((start + sectors) * 512);
		uint8_t mbr[512] = {};
		mbr[446 + 4] = 0x83;  // a partition of another kind first
		mbr[446 + 8] = 1, mbr[446 + 12] = 1;
		uint8_t *e = mbr + 446 + 16;
		e[4] = 0x06;
		e[8] = uint8_t(start);
		e[12] = uint8_t(sectors), e[13] = uint8_t(sectors >> 8);
		mbr[510] = 0x55, mbr[511] = 0xAA;
		img.put(0, mbr, 512);
		FatVolume v { img, start * 512, 16, 4, 1, 512, 20, sectors, 0xF8 };
		fat_fill(v, damaged, false);
		test_fat_volume(img, SYFS_MEDIUM_DISK, damaged, "BPB LABEL");
	}
	// FAT32: a bare volume of 66 000 clusters of 512 bytes, sparse in memory
	for (bool damaged : { false, true }) {
		const uint64_t clusters = 66000, fat_sectors = (clusters + 2) * 4 / 512 + 1, sectors = 32 + 2 * fat_sectors + clusters;
		Sparse img(sectors * 512);
		FatVolume v { img, 0, 32, 1, 32, 0, uint32_t(fat_sectors), sectors, 0xF8 };
		fat_fill(v, damaged, true);
		test_fat_volume(img, SYFS_MEDIUM_DISK, damaged, "TEST VOLUME");
	}
	// a bare volume's boot sector without its jump is no FAT
	Sparse img(2880 * 512);
	FatVolume v { img, 0, 12, 1, 1, 224, 9, 2880, 0xF0 };
	v.boot("");
	syfs_io io = io_of(img);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_FAT);
	uint8_t no_jump = 0;
	img.put(0, &no_jump, 1);
	CHECK(probe(&io, nullptr) == SYFS_NONE);
}

// ---- formatting: a blank image made a volume, checked against the specification and read back
static size_t sparse_write(void *user, uint64_t offset, const void *buf, size_t n)
{
	auto *s = static_cast<Sparse *>(user);
	if (offset >= s->size)
		return 0;
	n = size_t(std::min<uint64_t>(n, s->size - offset));
	s->put(offset, buf, n);
	return n;
}

static syfs_io writer_of(Sparse &s)
{
	return { &s, nullptr, sparse_write, s.size };
}

// the boot sector at base and what it lays out, as the specification requires: the signature, the FAT type the
// cluster count's, the FATs identical and room for every cluster, the root directory empty but for the label
static void check_fat_volume(const Sparse &img, uint64_t base, int bits, const char *label)
{
	uint8_t bs[512];
	img.get(base, bs, 512);
	CHECK(bs[0] == 0xEB && bs[2] == 0x90 && bs[510] == 0x55 && bs[511] == 0xAA);
	uint32_t bps = bs[11] | bs[12] << 8, spc = bs[13], reserved = bs[14] | bs[15] << 8, fats = bs[16];
	uint32_t root_entries = bs[17] | bs[18] << 8;
	uint64_t total = (bs[19] | bs[20] << 8) ? uint32_t(bs[19] | bs[20] << 8) : img.get32(base + 32);
	uint32_t fat16 = bs[22] | bs[23] << 8, fat_sectors = fat16 ? fat16 : img.get32(base + 36);
	CHECK(bps == 512 && fats == 2 && reserved >= 1 && (bits == 32) == !fat16 && (bits == 32) == !root_entries);
	uint64_t root_sectors = (root_entries * 32 + 511) / 512, data = reserved + 2 * fat_sectors + root_sectors;
	uint64_t clusters = (total - data) / spc;
	CHECK(bits == 12 ? clusters < 4085 : bits == 16 ? clusters >= 4085 && clusters < 65525 : clusters >= 65525);
	CHECK(uint64_t(fat_sectors) * 512 * 8 / bits >= clusters + 2);
	const uint8_t *ext = bs + (bits == 32 ? 64 : 36);
	CHECK(ext[2] == 0x29 && !memcmp(ext + 18, bits == 12 ? "FAT12   " : bits == 16 ? "FAT16   " : "FAT32   ", 8));
	char bpb_label[12] = {};
	memcpy(bpb_label, ext + 7, 11);
	std::string want = label ? label : "NO NAME";
	want.resize(11, ' ');
	CHECK(want == bpb_label);
	Bytes fat0(fat_sectors * 512), fat1(fat_sectors * 512);
	img.get(base + reserved * 512, fat0.data(), fat0.size());
	img.get(base + (reserved + fat_sectors) * 512, fat1.data(), fat1.size());
	CHECK(fat0 == fat1 && fat0[0] == bs[21]);
	size_t used = bits == 12 ? 3 : bits == 16 ? 4 : 12;
	CHECK(std::all_of(fat0.begin() + used, fat0.end(), [](uint8_t b) { return !b; }));
	uint64_t root = bits == 32 ? data : reserved + 2 * fat_sectors;
	Bytes dir(bits == 32 ? spc * 512 : root_entries * 32);
	img.get(base + root * 512, dir.data(), dir.size());
	size_t from = 0;
	if (label) {
		CHECK(dir[11] == 0x08 && !memcmp(&dir[0], want.data(), 11));
		from = 32;
	}
	CHECK(std::all_of(dir.begin() + from, dir.end(), [](uint8_t b) { return !b; }));
	if (bits == 32) {
		CHECK(img.get32(base + 44) == 2 && img.get32(base + reserved * 512 + 8) == 0x0FFFFFFF);
		CHECK(img.get32(base + 512) == 0x41615252 && img.get32(base + 512 + 484) == 0x61417272);
		CHECK(img.get32(base + 512 + 488) == clusters - 1 && img.get32(base + 512 + 508) == 0xAA550000);
		uint8_t backup[512];
		img.get(base + 6 * 512, backup, 512);
		CHECK(!memcmp(backup, bs, 512));
	}
}

// read back through the library: a FAT volume of the medium, with the label and no entries
static void check_fat_read(Sparse &img, syfs_key medium, const char *label)
{
	syfs_io io = io_of(img);
	syfs_key m;
	CHECK(probe(&io, &m) == SYFS_FORMAT_FAT && m == medium);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_FORMAT_FAT && medium_of(fs) == medium);
	if (fs) {
		CHECK(count_of(fs) == 0 && !strcmp(label_of(fs), label));
		close_image(fs);
	}
}

static void test_format_fat()
{
	char err[128];
	for (uint64_t kb : { 720, 1440 }) {
		Sparse img(kb * 1024);
		syfs_io w = writer_of(img);
		CHECK(make(SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, &w, "Samples 1", err, sizeof err));
		check_fat_volume(img, 0, 12, "SAMPLES 1");
		check_fat_read(img, SYFS_MEDIUM_FLOPPY, "SAMPLES 1");
		uint8_t bs[32];
		img.get(0, bs, 32);
		CHECK(bs[21] == (kb == 720 ? 0xF9 : 0xF0) && bs[24] == (kb == 720 ? 9 : 18) && bs[26] == 2);
		CHECK(uint64_t(bs[19] | bs[20] << 8) == kb * 2 && bs[13] == (kb == 720 ? 2 : 1));
	}
	{
		Sparse img(1440 * 1024);
		syfs_io w = writer_of(img);
		CHECK(make(SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, &w, nullptr, err, sizeof err));
		check_fat_volume(img, 0, 12, nullptr);
		check_fat_read(img, SYFS_MEDIUM_FLOPPY, "");
		err[0] = 0;
		CHECK(!make(SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, &w, "TWELVE CHARS", err, sizeof err) && err[0]);
		err[0] = 0;
		CHECK(!make(SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, &w, "A/B", err, sizeof err) && err[0]);
		Sparse odd(800 * 1024);
		w = writer_of(odd);
		err[0] = 0;
		CHECK(!make(SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, &w, nullptr, err, sizeof err) && err[0]);
	}
	// disks: FAT16 below 512 MB, FAT32 from there; the MBR's one partition from sector 63 to the end
	const uint64_t MB = 1 << 20;
	for (auto [bytes, bits] : std::vector<std::pair<uint64_t, int>> { { 5 * MB, 16 }, { 32 * MB, 16 }, { 512 * MB - 512, 16 },
		     { 512 * MB, 32 }, { 3072 * MB, 32 } }) {
		Sparse img(bytes);
		syfs_io w = writer_of(img);
		CHECK(make(SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, &w, "DISK", err, sizeof err));
		uint8_t mbr[512];
		img.get(0, mbr, 512);
		const uint8_t *e = mbr + 446;
		CHECK(mbr[510] == 0x55 && mbr[511] == 0xAA && img.get32(446 + 8) == 63 && img.get32(446 + 12) == bytes / 512 - 63);
		CHECK(bits == 16 ? e[4] == 0x04 || e[4] == 0x06 : e[4] == 0x0B || e[4] == 0x0C);
		CHECK(!e[16 + 4] && !e[32 + 4] && !e[48 + 4]);
		check_fat_volume(img, 63 * 512, bits, "DISK");
		check_fat_read(img, SYFS_MEDIUM_DISK, "DISK");
	}
	for (uint64_t bytes : { uint64_t(4 * MB), uint64_t(3) << 40, uint64_t(5 * MB + 1) }) {
		Sparse img(bytes);
		syfs_io w = writer_of(img);
		err[0] = 0;
		CHECK(!make(SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, &w, nullptr, err, sizeof err) && err[0]);
	}
	syfs_io none { nullptr, nullptr, nullptr, 0 };
	CHECK(!make(SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, &none, nullptr, err, sizeof err));
}

// ---- S-700 series formatting: each layout checked byte by byte against what the machines' own format writes, and
// read back as an empty volume

// the first 96 bytes of an S-700 series sector 0 as a machine writes them: the ID, the kind at 0x0F, a line of 15
// spaces, the system line, the copyright line, each line ending in a zero
static Bytes s700_head(uint8_t kind, const char *line)
{
	Bytes h(0x60, 0);
	memcpy(&h[4], "S770 MR25A", 10);
	h[0x0F] = kind;
	memset(&h[0x10], ' ', 15);
	memcpy(&h[0x20], line, 31);
	memcpy(&h[0x40], "       Copyright   Roland      ", 31);
	return h;
}

static bool all_of_byte(const Sparse &img, uint64_t sector, uint64_t sectors, uint8_t b)
{
	Bytes s(sectors * 512), want(sectors * 512);
	img.get(sector * 512, s.data(), s.size());
	memset(want.data(), b, want.size());
	return !memcmp(s.data(), want.data(), s.size());
}

static void check_s700_read(Sparse &img, syfs_key format, syfs_key medium, const char *label)
{
	syfs_io io = io_of(img);
	syfs_key m;
	CHECK(probe(&io, &m) == format && m == medium);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == format && medium_of(fs) == medium);
	if (fs) {
		CHECK(count_of(fs) == 0 && !strcmp(label_of(fs), label));
		close_image(fs);
	}
}

static void test_format_s700()
{
	char err[128];
	// disks: the last sector kept (up to 0x1214D4), the whole segments below it from sector 0x15AC (a segment ending
	// on the last sector not among them)
	struct Case {
		uint64_t sectors;
		uint32_t last, segments;
	};
	for (bool s760 : { true, false })
		for (Case c : { Case { 19999, 19998, 802 }, Case { 0x15AC + 18 * 300, 0x15AC + 18 * 300 - 1, 299 },
			     Case { 2048000, 0x1214D4, 65524 } }) {
			Sparse img(c.sectors * 512);
			const uint8_t junk[512] = { 0xA5 };
			img.put(2 * 512, junk, 512);
			img.put(0x15AC * 512, junk, 512);
			syfs_io w = writer_of(img);
			CHECK(s760 ? make(SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, &w, err, sizeof err) : make(SYFS_FORMAT_S770, SYFS_MEDIUM_DISK, &w, err, sizeof err));
			uint8_t s0[512];
			img.get(0, s0, 512);
			Bytes head = s700_head(0, s760 ? " S-760 System Disk    Ver. 2.24" : " SYS-772 HardDisk Sys Ver. 2.25");
			CHECK(!memcmp(s0, head.data(), 0x60));
			CHECK(std::all_of(s0 + 0x60, s0 + 0x100, [](uint8_t b) { return b == 0xFF; }));
			CHECK(!memcmp(s0 + 0x100, "ID0:Formatted   ", 16) && img.get32(0x110) == c.last);
			CHECK(std::all_of(s0 + 0x114, s0 + 0x11E, [](uint8_t b) { return !b; }));
			CHECK(std::all_of(s0 + 0x11E, s0 + 512, [](uint8_t b) { return b == 0xFF; }));
			CHECK(all_of_byte(img, 1, 1, 0xFF));
			// the segment table
			const uint64_t t = 0x404 * 512;
			uint32_t reserved = s760 ? 114 : 0;
			CHECK(img.get16(t) == 0xFFFA && img.get16(t + 2) == c.segments - reserved);
			Bytes table(0x100 * 512);
			img.get(t, table.data(), table.size());
			bool same = true;
			for (uint32_t n = 2; n < 65536; n++) {
				uint16_t want = n >= 2 + c.segments ? 0xFFFF : n < 59 && s760 ? 0xFFFE : n < 116 && s760 ? 0xFFFD : 0;
				if (n == 0xFFFE && s760)
					want = 0xFFFE;
				same = same && (table[2 * n] | table[2 * n + 1] << 8) == want;
			}
			CHECK(same);
			CHECK(all_of_byte(img, 0x504, 0x368, 0) && all_of_byte(img, 0x86C, 0xD40, 0xFF));
			// the S-760's extended partials 0xFF; segments 2 to 58 and the system's sectors left as they were
			CHECK(all_of_byte(img, 0x19AE, 0x400, 0xFF) == s760);
			CHECK(img.get16(2 * 512) == 0xA5 && img.get16(0x15AC * 512) == 0xA5);
			check_s700_read(img, s760 ? SYFS_FORMAT_S760 : SYFS_FORMAT_S770, SYFS_MEDIUM_DISK,
				s760 ? "S-760 System Disk    Ver. 2.24" : "SYS-772 HardDisk Sys Ver. 2.25");
		}
	for (auto [sectors, ok760, ok770] : std::vector<std::tuple<uint64_t, bool, bool>> { { 7619, true, true },
		     { 7618, false, true }, { 5567, false, true }, { 5566, false, false }, { 100, false, false } }) {
		Sparse img(sectors * 512);
		syfs_io w = writer_of(img);
		err[0] = 0;
		CHECK(make(SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, &w, err, sizeof err) == ok760 && (ok760 || err[0]));
		CHECK(make(SYFS_FORMAT_S770, SYFS_MEDIUM_DISK, &w, err, sizeof err) == ok770);
	}
	Sparse odd(100000 * 512 + 1);
	syfs_io w = writer_of(odd);
	CHECK(!make(SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, &w, err, sizeof err));

	// floppies: sector 0, every other sector the format's fill
	for (bool s760 : { true, false })
		for (uint64_t sectors : { 1440, 2880 }) {
			Sparse img(sectors * 512);
			syfs_io fw = writer_of(img);
			CHECK(s760 ? make(SYFS_FORMAT_S760, SYFS_MEDIUM_FLOPPY, &fw, err, sizeof err) : make(SYFS_FORMAT_S770, SYFS_MEDIUM_FLOPPY, &fw, err, sizeof err));
			uint8_t s0[512];
			img.get(0, s0, 512);
			const char *line = s760 ? " S-760 Formatted Disk Ver. 2.24" : " SYS-772 Formatted    Ver. 2.25";
			Bytes head = s700_head(sectors == 2880 ? ':' : ';', line);
			CHECK(!memcmp(s0, head.data(), 0x60) && std::all_of(s0 + 0x60, s0 + 512, [](uint8_t b) { return b == 0xFF; }));
			CHECK(all_of_byte(img, 1, sectors - 1, 0x0F));
			check_s700_read(img, s760 ? SYFS_FORMAT_S760 : SYFS_FORMAT_S770, SYFS_MEDIUM_FLOPPY, line + 1);
		}
	Sparse other(1200 * 1024);
	w = writer_of(other);
	CHECK(!make(SYFS_FORMAT_S760, SYFS_MEDIUM_FLOPPY, &w, err, sizeof err) && !make(SYFS_FORMAT_S770, SYFS_MEDIUM_FLOPPY, &w, err, sizeof err));
}

static void test_others()
{
	Bytes b(4096, 0);
	syfs_io io = io_of(b);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_NONE && count_of(fs) == 0);
	close_image(fs);

	memcpy(&b[0], "EMU3", 4);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_EMU3);

	Bytes tiny(100, 0);
	io = io_of(tiny);
	char err[64] = "";
	CHECK(!syfs_open(&io, err, sizeof err) && err[0]);
	CHECK(!syfs_open_path("/nonexistent/disc.iso", err, sizeof err));
}

// an S-760 sound floppy: a volume, a performance, a patch, a partial, a sample of three segments
static void test_s700_floppy()
{
	Bytes b(2880 * 512, 0x0F);
	memset(&b[0], 0xFF, 512);
	memcpy(&b[0], "\0\0\0\0S770 MR25A\0" "3", 16);
	put_name(b, 0x10, "", 16);
	put_name(b, 0x20, " S-760 Sound Disk     Ver. 2.24", 32);
	b[0x100] = 0;
	b[0x101] = 0;
	for (int k = 0; k < 5; k++)
		put16(b, 0x106 + 2 * k, 1);
	put16(b, 0x110, 3);
	put_name(b, 0x180, "PAD:Floppy Vol", 16);
	b[0x1A0] = 0;
	put_name(b, 9 * 512, "PAD:Floppy Pfm", 16);
	put_name(b, 11 * 512, "PAD:Floppy Pch", 16);
	put_name(b, 15 * 512, "PAD:Floppy Prt", 16);
	put_name(b, 23 * 512, "PAD:Floppy Smp", 16);
	memset(&b[39 * 512 + 0x10], 0xFF, 32);
	b[39 * 512 + 0x10 + 5] = 0;
	memset(&b[71 * 512 + 0x20], 0xFF, 88);
	b[71 * 512 + 0x20 + 39] = 0;
	size_t prt = 135 * 512, smp = 199 * 512;
	for (int k = 0; k < 4; k++)
		put16(b, prt + 16 + 16 * k, k ? 0xFFFF : 0);
	put32(b, smp + 16, 0);
	put32(b, smp + 20, 100 << 8);
	put32(b, smp + 24, 999 << 8);
	b[smp + 36] = 4;
	put16(b, smp + 0x2A, 3);
	b[smp + 44] = 1;
	b[smp + 45] = 60;
	// its wave from segment 1 of disk 0; segment n's word k is 100n + k mod 50
	b[smp + 0x28] = 1;
	b[smp + 0x29] = 0;
	for (uint32_t n = 0; n < 146; n++)
		for (uint32_t k = 0; k < 4608; k++)
			put16(b, (252 + 18 * n) * 512 + 2 * k, uint16_t(100 * n + k % 50));

	syfs_io io = io_of(b);
	syfs_key medium = SYFS_NONE;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_S760 && medium == SYFS_MEDIUM_FLOPPY && not_fat(b));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(medium_of(fs) == SYFS_MEDIUM_FLOPPY);
	CHECK(!strcmp(label_of(fs), "S-760 Sound Disk     Ver. 2.24"));
	CHECK((names(fs, SYFS_TYPE_VOLUME) == std::vector<std::string> { "PAD:Floppy Vol" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Vol")) == std::vector<std::string> { "PAD:Floppy Pfm" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Pfm")) == std::vector<std::string> { "PAD:Floppy Pch" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Pch")) == std::vector<std::string> { "PAD:Floppy Prt" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Prt")) == std::vector<std::string> { "PAD:Floppy Smp" }));
	size_t s = find(fs, "PAD:Floppy Smp");
	CHECK(sample_at(fs, find(fs, "PAD:Floppy Pch"), 60) == s);
	CHECK(sample_at(fs, find(fs, "PAD:Floppy Pch"), 61) == NONE);
	CHECK(!strcmp(entry_at(fs, s)->tag, "PAD") && entry_at(fs, s)->bytes == 3 * 9216);
	syfs::Sample info;
	CHECK(sample_info(fs, s, &info) && info.rate == 44100 && info.key == 60 && info.frames == 3 * 4608);
	CHECK(info.loop == SYFS_LOOP_ALTERNATE && info.loop_start == 100 && info.loop_end == 1000);
	CHECK((wave(fs, s, 0, 2) == std::vector<int16_t> { 100, 101 }));
	CHECK((wave(fs, s, 2 * 4608 - 1, 2) == std::vector<int16_t> { 207, 300 }));
	CHECK(wave(fs, s, 0, 3 * 4608).size() == 3 * 4608);
	close_image(fs);

	// the second disk of a set, the wave from the first disk's last segment on: its last two segments read here
	b[0x100] = 1;
	b[smp + 0x28] = 145;
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(wave(fs, s, 0, 10).empty());
	CHECK((wave(fs, s, 4608, 2) == std::vector<int16_t> { 0, 1 }));
	CHECK(wave(fs, s, 4608, 3 * 4608).size() == 2 * 4608);
	close_image(fs);
	b[0x100] = 0;

	// a save under way lists nothing; the disk probe leaves a floppy alone
	b[0x101] = 0xFF;
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_FORMAT_S760 && count_of(fs) == 0);
	close_image(fs);
	CHECK(syfs::roland::S700Disk::probe(syfs::Image(io)) == SYFS_NONE);
}

// an S-550 floppy: a patch on one tone, the area named from the function block
static void test_s550_floppy()
{
	Bytes b(1440 * 512, 0);
	memcpy(&b[0], "\0\0\0\0S550 MT25B\0\x04", 16);
	put_name(b, 0x20, "S-550 System   Ver. 1.12", 31);
	size_t area = 126 * 512;
	for (int k = 0; k < 16; k++)
		put_name(b, area + k * 256, "", 12);
	put_name(b, area, "Strings", 12);
	for (int k = 0; k < 109; k++)
		b[area + 0x1A + k] = 0;
	for (int k = 0; k < 32; k++)
		put_name(b, area + 0x1200 + k * 128, "", 8);
	put_name(b, area + 0x1200, "Str C3", 8);
	b[area + 0x1200 + 0x0F] = 2;
	put_name(b, area + 8 * 512 + 0x33, "Strings Vol1", 12);
	// two 12-bit values in three bytes from sector 144: value k is k mod 4096 - 2048
	for (uint32_t k = 0; k < 2 * 12288; k += 2) {
		uint32_t v0 = (k % 4096 - 2048) & 0xFFF, v1 = ((k + 1) % 4096 - 2048) & 0xFFF;
		size_t at = 144 * 512 + 3 * (k / 2);
		b[at] = uint8_t(v0 >> 4), b[at + 1] = uint8_t((v0 & 15) << 4 | (v1 & 15)), b[at + 2] = uint8_t(v1 >> 4);
	}

	syfs_io io = io_of(b);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_S550 && medium == SYFS_MEDIUM_FLOPPY && not_fat(b));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(label_of(fs), "S-550 System   Ver. 1.12"));
	CHECK((names(fs, SYFS_TYPE_AREA) == std::vector<std::string> { "Strings Vol1" }));
	CHECK((used(fs, find(fs, "Strings")) == std::vector<std::string> { "Str C3" }));
	size_t c3 = find(fs, "Str C3");
	CHECK(entry_at(fs, c3)->bytes == 2 * 24576);
	CHECK((wave(fs, c3, 0, 3) == std::vector<int16_t> { -2048 * 16, -2047 * 16, -2046 * 16 }));
	CHECK((wave(fs, c3, 2049, 2) == std::vector<int16_t> { 16, 32 }));
	CHECK(wave(fs, c3, 0, 3 * 12288).size() == 2 * 12288);
	close_image(fs);

	memcpy(&b[4], "S330 MT25C", 10);
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_S330);
	memcpy(&b[4], "S-50 MT25A", 10);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_FORMAT_S50 && count_of(fs) == 0);
	close_image(fs);
}

// A ROM as dumped: each byte put where the socket's wiring reads it from
struct Rom {
	Bytes b;
	const uint8_t *lines;
	int bits;
	void put(uint64_t at, uint8_t v)
	{
		static const uint8_t data_lines[8] = { 2, 0, 4, 5, 7, 6, 3, 1 };
		uint64_t a = at & ~((uint64_t(1) << bits) - 1);
		for (int k = 0; k < bits; k++)
			if (at >> k & 1)
				a |= uint64_t(1) << lines[k];
		uint8_t raw = 0;
		for (int k = 0; k < 8; k++)
			if (v >> k & 1)
				raw |= uint8_t(1 << data_lines[k]);
		b[a] = raw;
	}
	void be(uint64_t at, uint32_t v, int n)
	{
		for (int k = 0; k < n; k++)
			put(at + k, uint8_t(v >> 8 * (n - 1 - k)));
	}
	void text(uint64_t at, const char *s)
	{
		for (size_t k = 0; s[k]; k++)
			put(at + k, uint8_t(s[k]));
	}
	// twelve seven-bit characters, from the top bit down or from bit 0 up
	void packed(uint64_t at, const char *s, bool from_top)
	{
		uint8_t out[11] = {};
		for (int i = 0; i < 12; i++) {
			uint8_t c = i < int(strlen(s)) ? uint8_t(s[i]) : ' ';
			for (int k = 0; k < 7; k++) {
				int n = 7 * i + k;
				bool bit = from_top ? c >> (6 - k) & 1 : c >> k & 1;
				if (bit)
					out[n / 8] |= from_top ? uint8_t(0x80 >> n % 8) : uint8_t(1 << n % 8);
			}
		}
		for (int k = 0; k < 11; k++)
			put(at + k, out[k]);
	}
};

static void test_boards()
{
	static const uint8_t srx_lines[18] = { 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15 };
	static const uint8_t srjv_lines[19] = { 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14 };

	// an SRX board: a wave table and an XV-5080 patch bank
	Rom srx { Bytes(32 << 20, 0), srx_lines, 18 };
	memcpy(&srx.b[0], "Roland SRX", 10);
	memcpy(&srx.b[0x10], "SRX-99 Test     ", 16);
	srx.text(0x20, "SRX-99 Test");
	srx.be(0x42, 3, 2);
	const uint32_t waves = 0x1F00000, patches = 0x1F80000, samples = 0x1FC0000;
	srx.be(0x80, 2, 2), srx.be(0x82, 3, 2), srx.be(0x84, waves, 4), srx.be(0x88, 0x100, 4);
	srx.be(0x90, 4, 2), srx.be(0x92, 7, 2), srx.be(0x94, patches, 4), srx.be(0x98, 0x1000, 4);
	srx.be(0xA0, 1, 2), srx.be(0xA2, 4, 2), srx.be(0xA4, samples, 4), srx.be(0xA8, 0x100, 4);
	srx.be(waves, 2, 2), srx.be(waves + 4, 0x0C, 4), srx.be(waves + 8, 8, 4);
	srx.be(waves + 0x0C, 0x40, 4), srx.be(waves + 0x10, 28, 4), srx.be(waves + 0x14, 0x60, 4), srx.be(waves + 0x18, 24, 4);
	srx.text(waves + 0x40, "Kick 1");
	srx.be(waves + 0x50, 0x3B00FFFF, 4), srx.be(waves + 0x54, 0x3D000000, 4), srx.be(waves + 0x58, 0x7F00FFFF, 4);
	srx.text(waves + 0x60, "Snare 1");
	srx.be(waves + 0x70, 0x4600FFFF, 4), srx.be(waves + 0x74, 0x7F000001, 4);
	// samples: a back and forth loop at 32 kHz in the second megabyte, its deltas scaled by 2^3; a one-shot at 44.1 kHz
	// a semitone sharp
	srx.be(samples, 2, 2), srx.be(samples + 4, 0x0C, 4), srx.be(samples + 8, 24, 4);
	srx.be(samples + 0x0C, 0x7F013C00, 4), srx.be(samples + 0x10, 0x02000400, 4), srx.be(samples + 0x14, 0x04000140, 4);
	srx.be(samples + 0x18, 0x108000, 4), srx.be(samples + 0x1C, 0x108001, 4), srx.be(samples + 0x20, 0x108002, 4);
	srx.be(samples + 0x24, 0x7F023000, 4), srx.be(samples + 0x28, 0x020007E8, 4), srx.be(samples + 0x2C, 0x040001B9, 4);
	srx.be(samples + 0x30, 0x108010, 4), srx.be(samples + 0x34, 0x108011, 4), srx.be(samples + 0x38, 0x108011, 4);
	srx.put(0x100400, 0x13);
	srx.put(0x108000, 10), srx.put(0x108001, 5), srx.put(0x108002, uint8_t(-5));
	srx.put(0x108010, 4), srx.put(0x108011, uint8_t(-4));
	srx.be(patches, 1, 2), srx.be(patches + 4, 0x0E, 4), srx.be(patches + 8, 574, 4);
	srx.packed(patches + 0x0E, "Test Patch", true);
	srx.be(0x40, 7, 2);
	syfs_io io = io_of(srx.b);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_SRX && medium == SYFS_MEDIUM_BOARD);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (fs) {
		// what the board was sold as, by its number
		CHECK(num(fs, SYFS_ROOT, SYFS_PROP_NUMBER) == 7 && text_of(fs, SYFS_PROP_MODEL) == "SRX-07"
			&& text_of(fs, SYFS_PROP_TITLE) == "Ultimate Keys" && text_of(fs, SYFS_PROP_ID) == "");
		CHECK(!strcmp(label_of(fs), "SRX-99 Test"));
		CHECK((names(fs, SYFS_TYPE_WAVE) == std::vector<std::string> { "Kick 1", "Snare 1" }));
		CHECK((names(fs, SYFS_TYPE_PATCH) == std::vector<std::string> { "Test Patch" }));
		size_t bank = entry_at(fs, find(fs, "Test Patch"))->parent;
		CHECK(bank != NONE && !strcmp(entry_at(fs, bank)->tag, "4:7"));
		CHECK(!strcmp(entry_at(fs, bank)->name, "XV-5080 Patches"));
		// the silent splits have no entry; Snare 1 sounds nothing at middle C and answers with its nearest split
		size_t kick = find(fs, "Kick 1", SYFS_TYPE_WAVE), snare = find(fs, "Snare 1", SYFS_TYPE_WAVE);
		CHECK((splits(fs, kick) == std::vector<std::string> { "60-61" }));
		CHECK((splits(fs, snare) == std::vector<std::string> { "71-127" }));
		size_t k0 = refs_of(fs, kick).at(0), s0 = refs_of(fs, snare).at(0);
		CHECK(sample_at(fs, kick, 60) == k0 && sample_at(fs, kick, 59) == NONE);
		CHECK(sample_at(fs, kick, 62) == NONE && sample_at(fs, snare, 60) == NONE);
		CHECK(sample_at(fs, snare, 71) == s0);
		CHECK(entry_at(fs, k0)->kind == SYFS_TYPE_SAMPLE && entry_at(fs, k0)->parent == kick);
		CHECK(entry_at(fs, k0)->number == 1 && entry_at(fs, k0)->bytes == 3);
		syfs::Sample sm;
		CHECK(sample_info(fs, k0, &sm) && sm.rate == 32000 && sm.key == 60 && sm.frames == 3);
		CHECK(sm.loop == SYFS_LOOP_ALTERNATE && sm.loop_start == 1 && sm.loop_end == 3 && !sm.reverse && sm.mode == 1);
		CHECK((wave(fs, k0, 0, 3) == std::vector<int16_t> { 20, 30, 20 }));
		CHECK(sample_info(fs, snare, &sm) && sm.rate == 46722 && sm.key == 48 && sm.frames == 2);
		CHECK(sm.loop == SYFS_NONE && sm.mode == 2);
		CHECK((wave(fs, snare, 0, 2) == std::vector<int16_t> { 2, 0 }) && (wave(fs, s0, 1, 5) == std::vector<int16_t> { 0 }));
		close_image(fs);
	}

	// an SO-PCM1 card: waves, a patch bank in the JV-880's shape and one in the JV-1080's rhythm set shape
	Rom card { Bytes(1 << 20, 0), srjv_lines, 19 };
	memcpy(&card.b[0], "Roland JV80 O\xb0S\xac", 16);
	memcpy(&card.b[0x10], "Card09   Ver1.00", 16);
	card.put(0x50, 2), card.be(0x60, 3, 2), card.be(0x80, 0xF0800, 4);
	card.put(0x51, 1), card.be(0x62, 2, 2), card.be(0x84, 0xF0000, 4);
	card.put(0x53, 2), card.be(0x66, 1, 2), card.be(0x8C, 0xF1000, 4);
	card.put(0x54, 4), card.be(0x68, 1, 2), card.be(0x90, 0xF2000, 4);
	card.text(0xF0000, "Piano A");
	card.text(0xF0000 + 60, "Piano B");
	// Piano A: samples 0 up to key 59, 1 above; Piano B: sample 2 everywhere
	card.put(0xF000C, 59), card.put(0xF000D, 127), card.be(0xF001C, 0x00000001, 4), card.be(0xF0020, 0xFFFFFFFF, 4);
	card.put(0xF0000 + 60 + 12, 127), card.be(0xF0000 + 60 + 28, 0x0002FFFF, 4);
	// sample 0 loops forward, a semitone sharp, its deltas scaled by 2^2; sample 1 is played once, and crosses into the
	// high nibble's sixteen samples; sample 2 is played once from its end; sample 3 loops from before its start
	const uint8_t recs[3][18] = {
		{ 0x7F, 0x00, 0x80, 0x00, 0x00, 0x80, 0x02, 0x00, 0x80, 0x05, 0, 0, 0, 64, 0x07, 0xE8, 0x04, 0x00 },
		{ 0x7F, 0x00, 0x80, 0x0F, 0x00, 0x80, 0x11, 0x00, 0x80, 0x11, 0, 0, 2, 72, 0x04, 0x00, 0x04, 0x00 },
		{ 0x7F, 0x00, 0x80, 0x00, 0x00, 0x80, 0x01, 0x00, 0x80, 0x01, 0, 0, 6, 48, 0x04, 0x00, 0x04, 0x00 },
	};
	for (int r = 0; r < 3; r++)
		for (int k = 0; k < 18; k++)
			card.put(0xF0800 + 18 * r + k, recs[r][k]);
	card.put(0x400, 0x52);
	const int8_t deltas[] = { 1, 2, 3, -1, -2, 0 };
	for (int k = 0; k < 6; k++)
		card.put(0x8000 + k, uint8_t(deltas[k]));
	card.put(0x800F, 1), card.put(0x8010, 1), card.put(0x8011, uint8_t(-1));
	card.text(0xF1000, "Card Piano");
	card.packed(0xF2000, "Card Drums", false);
	card.text(0x20, "CRD05100"), card.text(0x30, "1992-11-10");
	io = io_of(card.b);
	CHECK(probe(&io, &medium) == SYFS_FORMAT_SRJV80 && medium == SYFS_MEDIUM_CARD);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (fs) {
		// two cards share an identity: the date tells which
		CHECK(text_of(fs, SYFS_PROP_ID) == "CRD05100" && text_of(fs, SYFS_PROP_DATE) == "1992-11-10"
			&& text_of(fs, SYFS_PROP_MODEL) == "SO-PCM1-05" && text_of(fs, SYFS_PROP_TITLE) == "Accordion");
		CHECK(!strcmp(label_of(fs), "Card09   Ver1.00"));
		CHECK((names(fs, SYFS_TYPE_WAVE) == std::vector<std::string> { "Piano A", "Piano B" }));
		CHECK((names(fs, SYFS_TYPE_PATCH) == std::vector<std::string> { "Card Piano" }));
		CHECK((names(fs, SYFS_TYPE_RHYTHM_SET) == std::vector<std::string> { "Card Drums" }));
		CHECK((names(fs, SYFS_TYPE_BANK) == std::vector<std::string> { "JV-880 Patches", "JV-1080 Rhythm Sets" }));
		const syfs::Entry *drums = entry_at(fs, entry_at(fs, find(fs, "Card Drums"))->parent);
		CHECK(drums->kind == SYFS_TYPE_BANK && !strcmp(drums->tag, "4"));
		size_t a = find(fs, "Piano A", SYFS_TYPE_WAVE), b = find(fs, "Piano B", SYFS_TYPE_WAVE);
		CHECK((splits(fs, a) == std::vector<std::string> { "0-59", "60-127" }));
		CHECK((splits(fs, b) == std::vector<std::string> { "0-127" }));
		CHECK((names(fs, SYFS_TYPE_WAVE) == std::vector<std::string> { "Piano A", "Piano B" }));
		size_t a0 = refs_of(fs, a).at(0), b0 = refs_of(fs, b).at(0), a1 = refs_of(fs, a).at(1);
		CHECK(sample_at(fs, a, 0) == a0 && sample_at(fs, a, 59) == a0);
		CHECK(sample_at(fs, a, 60) == a1 && sample_at(fs, a, 127) == a1 && sample_at(fs, a1, 0) == a1);
		CHECK(sample_at(fs, find(fs, "Card Piano"), 60) == NONE);
		syfs::Sample sm;
		CHECK(sample_info(fs, a0, &sm) && sm.rate == 33903 && sm.key == 64 && sm.frames == 6);
		CHECK(sm.loop == SYFS_LOOP_FORWARD && sm.loop_start == 2 && sm.loop_end == 6 && sm.mode == 0);
		CHECK((wave(fs, a0, 0, 6) == std::vector<int16_t> { 1, 3, 6, 5, 3, 3 }));
		CHECK((wave(fs, a0, 4, 9) == std::vector<int16_t> { 3, 3 }));
		CHECK(sample_info(fs, a, &sm) && sm.rate == 32000 && sm.key == 72 && sm.frames == 3);
		CHECK(sm.loop == SYFS_NONE && !sm.reverse && sm.mode == 2);
		CHECK((wave(fs, a, 0, 3) == std::vector<int16_t> { 1, 9, 1 }));
		CHECK(sample_info(fs, b0, &sm) && sm.key == 48 && sm.reverse && sm.loop == SYFS_NONE);
		CHECK(sample_info(fs, b, &sm) && sm.key == 48);
		close_image(fs);
	}

	// a loop starting before its sample's start: played on after the frames from the start
	const uint8_t early[18] = { 0x7F, 0x00, 0x80, 0x02, 0x00, 0x80, 0x00, 0x00, 0x80, 0x03, 0, 0, 0, 60, 0x04, 0x00, 0x04, 0x00 };
	for (int k = 0; k < 18; k++)
		card.put(0xF0800 + 18 * 2 + k, early[k]);
	card.put(0x8000, 1), card.put(0x8001, 2), card.put(0x8002, uint8_t(-1)), card.put(0x8003, uint8_t(-2));
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (fs) {
		size_t b0 = refs_of(fs, find(fs, "Piano B", SYFS_TYPE_WAVE)).at(0);
		syfs::Sample sm;
		CHECK(sample_info(fs, b0, &sm) && sm.frames == 6 && sm.start == 0 && sm.loop_start == 2 && sm.loop_end == 6);
		CHECK(entry_at(fs, b0)->bytes == 4);
		CHECK((wave(fs, b0, 0, 6) == std::vector<int16_t> { -1, -3, -2, 0, -1, -3 }));
		close_image(fs);
	}
}

// an S1000 floppy (2HD): a program over two blocks, a sample, the name after the FAT
static void test_akai_floppy()
{
	const size_t K = 1024, blocks = 1600;
	Bytes b(blocks * K, 0);
	for (uint32_t k = 0; k < 5; k++)
		put16(b, 0x600 + 2 * k, 0x4000);
	akai_name(b, 0x600 + 2 * blocks, "MY FLOPPY");
	akai_file(b, 0, "PIANO", 'p', 150 * 8, 5);
	akai_file(b, 24, "PIANO C3", 's', 150 + 2 * 600, 7);
	put16(b, 0x600 + 2 * 5, 6);
	put16(b, 0x600 + 2 * 6, 0xC000);
	put16(b, 0x600 + 2 * 7, 9);
	put16(b, 0x600 + 2 * 9, 0xC000);
	Bytes pr(150 * 8, 0);
	akai_program(pr, 0, 150, { { "PIANO C3" } });
	memcpy(&b[5 * K], pr.data(), pr.size());
	size_t smp = 7 * K;
	b[smp] = 3;
	b[smp + 2] = 60;
	put32(b, smp + 0x1A, 600);
	put32(b, smp + 0x22, 599);
	put16(b, smp + 0x8A, 44100);
	// frame k is k - 300, over block 7 and on into block 9
	for (uint32_t k = 0; k < 600; k++) {
		uint32_t at = 150 + 2 * k;
		put16(b, (at < K ? 7 * K + at : 9 * K + at - K), uint16_t(k - 300));
	}

	syfs_io io = io_of(b);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_AKAI_S1000 && medium == SYFS_MEDIUM_FLOPPY && not_fat(b));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(label_of(fs), "MY FLOPPY"));
	CHECK((names(fs, SYFS_TYPE_VOLUME) == std::vector<std::string> { "MY FLOPPY" }));
	CHECK((used(fs, find(fs, "PIANO")) == std::vector<std::string> { "PIANO C3" }));
	syfs::Sample info;
	size_t c3 = find(fs, "PIANO C3");
	CHECK(sample_info(fs, c3, &info) && info.rate == 44100 && info.frames == 600 && info.end == 600);
	CHECK((wave(fs, c3, 436, 2) == std::vector<int16_t> { 136, 137 }));
	auto all = wave(fs, c3, 0, 1000);
	CHECK(all.size() == 600 && all[0] == -300 && all[599] == 299);
	close_image(fs);

	b[16] = 0xF0;
	CHECK(probe(&io, nullptr) == SYFS_FORMAT_AKAI_S3000);
}

// an S900 floppy: a program on one sample, the sample's header (60 bytes) and its loop
static void test_s900()
{
	const size_t K = 1024;
	Bytes b(800 * K, 0);
	auto file = [&](size_t at, const char *name, uint8_t type, uint32_t size, uint16_t start) {
		put_name(b, at, name, 10);
		b[at + 16] = type;
		b[at + 17] = uint8_t(size), b[at + 18] = uint8_t(size >> 8), b[at + 19] = uint8_t(size >> 16);
		put16(b, at + 20, start);
	};
	file(0, "BASS", 'P', 0x26 + 70, 4);
	file(24, "BASS E1", 'S', 60 + 1500, 5);
	put16(b, 0x600 + 2 * 4, 0x8000);
	put16(b, 0x600 + 2 * 5, 6);
	put16(b, 0x600 + 2 * 6, 0x8000);
	put_name(b, 4 * K, "BASS", 16);
	b[4 * K + 0x17] = 1;
	b[4 * K + 0x26] = 127, b[4 * K + 0x26 + 1] = 24;
	put_name(b, 4 * K + 0x26 + 0x18, "BASS E1", 10);
	put_name(b, 4 * K + 0x26 + 0x2E, "SAMPLE 2", 10);
	size_t smp = 5 * K;
	put_name(b, smp, "BASS E1", 16);
	put32(b, smp + 0x10, 1000);
	put16(b, smp + 0x14, 25000);
	put16(b, smp + 0x16, 40 * 16);
	b[smp + 0x1A] = 'L';
	put32(b, smp + 0x1C, 1000);
	put32(b, smp + 0x20, 10);
	put32(b, smp + 0x24, 200);
	// value j is (7j mod 4096) - 2048: the first 500 as words of their top eight bits over the low four bits of each
	// and of its partner 500 on, then the last 500's top eight bits; on over blocks 5 and 6
	auto value = [](uint32_t j) { return uint32_t(int(7 * j % 4096) - 2048) & 0xFFF; };
	Bytes data(1500);
	for (uint32_t j = 0; j < 500; j++) {
		data[2 * j] = uint8_t((value(j) & 15) << 4 | (value(j + 500) & 15));
		data[2 * j + 1] = uint8_t(value(j) >> 4);
		data[1000 + j] = uint8_t(value(j + 500) >> 4);
	}
	for (uint32_t k = 0; k < data.size(); k++)
		b[60 + k < K ? smp + 60 + k : 6 * K + 60 + k - K] = data[k];

	syfs_io io = io_of(b);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_AKAI_S900 && medium == SYFS_MEDIUM_FLOPPY && not_fat(b));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK((names(fs, SYFS_TYPE_PROGRAM) == std::vector<std::string> { "BASS" }));
	CHECK((used(fs, find(fs, "BASS")) == std::vector<std::string> { "BASS E1" }));
	CHECK(sample_at(fs, find(fs, "BASS"), 24) == find(fs, "BASS E1") && sample_at(fs, find(fs, "BASS"), 23) == NONE);
	syfs::Sample info;
	CHECK(sample_info(fs, find(fs, "BASS E1"), &info) && info.rate == 25000 && info.key == 40);
	CHECK(info.frames == 1000 && info.start == 10 && info.end == 1000);
	CHECK(info.loop == SYFS_LOOP_FORWARD && info.loop_start == 800 && info.loop_end == 1000);
	CHECK(info.bits == 12);
	auto all = wave(fs, find(fs, "BASS E1"), 0, 2000);
	bool same = all.size() == 1000;
	for (uint32_t j = 0; same && j < 1000; j++)
		same = all[j] == int16_t(value(j) << 4);
	CHECK(same);
	CHECK((wave(fs, find(fs, "BASS E1"), 499, 2) == std::vector<int16_t> { int16_t(value(499) << 4), int16_t(value(500) << 4) }));
	close_image(fs);

	// a lower-case type is no S900's
	b[24 + 16] = 's';
	CHECK(probe(&io, nullptr) == SYFS_NONE);
}

// ---- a SmartMedia card: a FAT disk in its logical blocks, the physical ones in another order
static void test_smartmedia()
{
	const uint32_t PAGE = 512, PER_BLOCK = 16, PHYSICAL = 1024, LOGICAL = 1000, ROW = PAGE + 16;
	char err[128];
	Sparse disk(uint64_t(LOGICAL) * PER_BLOCK * PAGE);
	syfs_io w = writer_of(disk);
	CHECK(make(SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, &w, "CARD", err, sizeof err));
	Bytes card(1024 + size_t(PHYSICAL) * PER_BLOCK * ROW, 0xFF);
	memset(&card[0], 0, 1024);
	card[0] = 0xEC, card[1] = 0xE6, card[2] = 0xA5;
	// logical block l in physical block (7 l + 3) mod 1024, but for the last, left unclaimed
	std::vector<bool> used(PHYSICAL, false);
	auto row_of = [&](uint32_t block) { return 1024 + size_t(block) * PER_BLOCK * ROW; };
	for (uint32_t l = 0; l + 1 < LOGICAL; l++) {
		uint32_t block = (7 * l + 3) % PHYSICAL;
		used[block] = true;
		for (uint32_t p = 0; p < PER_BLOCK; p++)
			disk.get(uint64_t(l * PER_BLOCK + p) * PAGE, &card[row_of(block) + p * ROW], PAGE);
		uint16_t field = uint16_t(0x1000 | l << 1);
		card[row_of(block) + PAGE + 6] = uint8_t(field >> 8), card[row_of(block) + PAGE + 7] = uint8_t(field);
	}
	// block 5's number claimed again by a later, erased block: the first has it
	for (uint32_t block = 7 * 5 + 3 + 1; block < PHYSICAL; block++)
		if (!used[block]) {
			card[row_of(block) + PAGE + 6] = 0x10, card[row_of(block) + PAGE + 7] = 5 << 1;
			break;
		}
	syfs_io io = io_of(card);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_FAT && medium == SYFS_MEDIUM_SMARTMEDIA);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && count_of(fs) == 0 && !strcmp(label_of(fs), "CARD"));
	close_image(fs);
	{
		syfs::Image im(io);
		uint8_t a[4096], b[4096];
		CHECK(im.card() && !im.raw() && im.size() == disk.size);
		for (uint64_t at : { uint64_t(0), uint64_t(63 * 512 - 100), uint64_t(5 * 8192 - 7) }) {
			disk.get(at, b, sizeof b);
			CHECK(im.read(at, a, sizeof a) && !memcmp(a, b, sizeof a));
		}
		// the block no physical block claims reads as erased; nothing past the last
		CHECK(im.read(disk.size - 8192, a, 4096) && std::all_of(a, a + 4096, [](uint8_t v) { return v == 0xFF; }));
		CHECK(!im.read(disk.size - 100, a, 200));
	}
	// the card in a VHD: complemented, so that what is erased is left out, and read as the card it is
	{
		Sparse vhd(32 << 20), back(card.size());
		syfs_io to = writer_of(vhd);
		const uint64_t bytes = syfs_convert(&io, &to, SYFS_CONTAINER_VHD, err, sizeof err);
		// every block of this card is written, so nothing is left out
		CHECK(bytes > card.size());
		uint8_t first[4];
		vhd.size = bytes;
		// 64 KB blocks; the first's data, past the tables and its bitmap, starts with the header complemented
		CHECK(vhd.get32(512 + 32) == 0x00000100);
		vhd.get(512 + 1024 + 1024 + 512, first, 4);
		CHECK(first[0] == 0x13 && first[1] == 0x19 && first[2] == 0x5A && first[3] == 0xFF);
		syfs_io thin = io_of(vhd);
		CHECK(probe(&thin, &medium) == SYFS_FORMAT_FAT && medium == SYFS_MEDIUM_SMARTMEDIA);
		fs = syfs_open(&thin, nullptr, 0);
		CHECK(fs && num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_VHD && !strcmp(label_of(fs), "CARD"));
		close_image(fs);
		to = writer_of(back);
		CHECK(syfs_convert(&thin, &to, SYFS_CONTAINER_RAW, err, sizeof err) == card.size());
		Bytes whole(card.size());
		back.get(0, whole.data(), whole.size());
		CHECK(whole == card);
	}

	// another device's code for the size, or a size no card has, is no card
	card[1] = 0x73;
	CHECK(probe(&io, &medium) == SYFS_NONE && medium == SYFS_NONE);
	card[1] = 0xE6;
	card.resize(card.size() - 528);
	io = io_of(card);
	CHECK(probe(&io, &medium) == SYFS_NONE && medium == SYFS_NONE);

	// a blank card made: its header, every page erased, told as a card not formatted
	for (unsigned mb : { 2u, 8u, 64u }) {
		Sparse blank(1024 + uint64_t(mb) * 1048576 / 32 * 33);
		w = writer_of(blank);
		CHECK(syfs_create(&w, SYFS_NONE, SYFS_MEDIUM_SMARTMEDIA, nullptr, err, sizeof err));
		uint8_t head[1024 + 4];
		blank.get(0, head, sizeof head);
		CHECK(head[0] == 0xEC && head[1] == (mb == 2 ? 0xEA : mb == 8 ? 0xE6 : 0x76) && head[2] == 0xA5 && !head[3]
			&& !head[1023] && head[1024] == 0xFF);
		blank.get(blank.size - 4, head, 4);
		CHECK(head[0] == 0xFF && head[3] == 0xFF);
		io = io_of(blank);
		CHECK(probe(&io, &medium) == SYFS_NONE && medium == SYFS_MEDIUM_SMARTMEDIA);
		// in a VHD a blank card is one block, for its header
		Sparse vhd(1 << 20);
		syfs_io to = writer_of(vhd);
		const uint64_t bytes = syfs_convert(&io, &to, SYFS_CONTAINER_VHD, err, sizeof err);
		const uint64_t table = (blank.size / 65536 + 1) * 4;
		CHECK(bytes == 512 + 1024 + (table + 511) / 512 * 512 + 512 + 65536 + 512);
		vhd.size = bytes;
		io = io_of(vhd);
		CHECK(probe(&io, &medium) == SYFS_NONE && medium == SYFS_MEDIUM_SMARTMEDIA);
	}
	Sparse odd(5 << 20);
	w = writer_of(odd);
	err[0] = 0;
	CHECK(!syfs_create(&w, SYFS_NONE, SYFS_MEDIUM_SMARTMEDIA, nullptr, err, sizeof err) && err[0]);
	CHECK(!syfs_create(&w, SYFS_FORMAT_S760, SYFS_MEDIUM_SMARTMEDIA, nullptr, err, sizeof err));
}

// ---- VHD: a disk written as a dynamic VHD and read back; a fixed one; what is no VHD
static uint32_t vhd_sum(const uint8_t *p, size_t n, size_t field)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < n; i++)
		if (i < field || i >= field + 4)
			sum += p[i];
	return ~sum;
}

static void test_vhd()
{
	const uint64_t MB = 1 << 20, SIZE = 33 * MB + 512;
	char err[128] = "";
	Sparse disk(SIZE);
	syfs_io w = writer_of(disk);
	CHECK(make(SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, &w, "VHDTEST", err, sizeof err));
	const uint8_t mark[4] = { 1, 2, 3, 4 };
	disk.put(20 * MB + 5, mark, 4);
	disk.put(SIZE - 4, mark, 4);
	Sparse vhd(64 * MB);
	syfs_io from = io_of(disk), to = writer_of(vhd);
	const uint64_t bytes = syfs_convert(&from, &to, SYFS_CONTAINER_VHD, err, sizeof err);
	// the footer twice, the header, a table of 17 blocks in a sector, and the blocks that hold something: the first
	// (the volume's structures), the eleventh and the last, each behind a sector of bitmap
	CHECK(bytes == 512 + 1024 + 512 + 3 * (512 + 2 * MB) + 512);
	uint8_t f[512], h[1024], copy[512];
	vhd.get(bytes - 512, f, 512), vhd.get(0, copy, 512), vhd.get(512, h, 1024);
	CHECK(!memcmp(f, "conectix", 8) && !memcmp(f, copy, 512) && f[63] == 3 && f[23] == 0 && f[22] == 2);
	CHECK(vhd_sum(f, 512, 64) == (uint32_t(f[64]) << 24 | f[65] << 16 | f[66] << 8 | f[67]));
	CHECK(vhd.get32(48) == 0 && f[52] == 0x02 && f[53] == 0x10 && f[54] == 0x02 && f[55] == 0x00);
	CHECK(!memcmp(h, "cxsparse", 8) && h[31] == 17 && h[33] == 0x20 && h[23] == 0 && h[22] == 6);
	CHECK(vhd_sum(h, 1024, 36) == (uint32_t(h[36]) << 24 | h[37] << 16 | h[38] << 8 | h[39]));
	// 67585 sectors: 17 a track, 4 heads, 993 cylinders
	CHECK(f[56] == (993 >> 8) && f[57] == (993 & 255) && f[58] == 4 && f[59] == 17);
	uint8_t bat[8];
	vhd.get(1536, bat, 8);
	CHECK(bat[3] == 4 && bat[4] == 0xFF && bat[7] == 0xFF);

	vhd.size = bytes;
	syfs_io io = io_of(vhd);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_FAT && medium == SYFS_MEDIUM_DISK);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_VHD && !strcmp(label_of(fs), "VHDTEST"));
	close_image(fs);
	fs = syfs_open(&from, nullptr, 0);
	CHECK(fs && num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_RAW);
	close_image(fs);
	{
		syfs::Image im(io);
		uint8_t a[4096], b[4096];
		CHECK(im.vhd() && im.size() == SIZE);
		for (uint64_t at : { uint64_t(0), 2 * MB - 100, 20 * MB, SIZE - 4096 }) {
			disk.get(at, b, sizeof b);
			CHECK(im.read(at, a, sizeof a) && !memcmp(a, b, sizeof a));
		}
		CHECK(!im.read(SIZE - 100, a, 200));
	}
	// and out again, byte for byte
	Sparse back(SIZE);
	to = writer_of(back);
	CHECK(syfs_convert(&io, &to, SYFS_CONTAINER_RAW, err, sizeof err) == SIZE && back.pages == disk.pages);

	// a fixed VHD: the disk, then the footer
	Sparse fixed(SIZE + 512);
	fixed.pages = disk.pages;
	f[63] = 2;
	memset(f + 16, 0xFF, 8);
	uint32_t sum = vhd_sum(f, 512, 64);
	f[64] = uint8_t(sum >> 24), f[65] = uint8_t(sum >> 16), f[66] = uint8_t(sum >> 8), f[67] = uint8_t(sum);
	fixed.put(SIZE, f, 512);
	io = io_of(fixed);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_VHD && !strcmp(label_of(fs), "VHDTEST"));
	close_image(fs);
	CHECK(syfs::Image(io).size() == SIZE);
	// a footer whose checksum is wrong, or of a differencing disk, is no VHD's: the file is read as it lies
	f[70] ^= 1;
	fixed.put(SIZE, f, 512);
	CHECK(!syfs::Image(io).vhd() && syfs::Image(io).size() == SIZE + 512);
	f[70] ^= 1, f[63] = 4;
	sum = vhd_sum(f, 512, 64);
	f[64] = uint8_t(sum >> 24), f[65] = uint8_t(sum >> 16), f[66] = uint8_t(sum >> 8), f[67] = uint8_t(sum);
	fixed.put(SIZE, f, 512);
	CHECK(!syfs::Image(io).vhd());

	// an image not of whole sectors makes no VHD; one all zeros makes one of no blocks
	Sparse odd(MB + 7), out(4 * MB);
	from = io_of(odd), to = writer_of(out);
	err[0] = 0;
	CHECK(!syfs_convert(&from, &to, SYFS_CONTAINER_VHD, err, sizeof err) && err[0]);
	CHECK(!syfs_convert(&from, &to, SYFS_FORMAT_FAT, err, sizeof err));
	Sparse blank(8 * MB);
	from = io_of(blank);
	CHECK(syfs_convert(&from, &to, SYFS_CONTAINER_VHD, err, sizeof err) == 512 + 1024 + 512 + 512);
}

// ---- every key is its name's hash, and the names read back
static void test_keys()
{
	size_t formats = 0;
#define X(constant, cls, name, title) \
	CHECK(syfs_key_of(name) == constant && constant != SYFS_NONE && constant <= 0x7FFFFFFF); \
	CHECK(!strcmp(syfs_name(constant), name) && !strcmp(syfs_title(constant), title)); \
	formats += !strcmp(cls, "format");
	SYFS_KEYS(X)
#undef X
	CHECK(formats == 16 && syfs_format_at(0) == SYFS_FORMAT_S550 && syfs_format_at(formats - 1) == SYFS_FORMAT_FAT
		&& syfs_format_at(formats) == SYFS_NONE);
	CHECK(!strcmp(syfs_name(SYFS_NONE), "none") && !syfs_name(syfs_key_of("no such name")));
	CHECK(syfs_key_of("rhythm-set") == SYFS_TYPE_RHYTHM_SET && !strcmp(syfs_title(SYFS_TYPE_RHYTHM_SET), "rhythm set"));
}

int main()
{
	test_keys();
	Bytes s760 = s700_image(" S-760 System Disk    Ver. 2.24");
	test_s700(s760, SYFS_FORMAT_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(raw_of(s760), SYFS_FORMAT_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(s700_image(" SYS-772 HardDisk Sys Ver. 2.25"), SYFS_FORMAT_S770, "SYS-772 HardDisk Sys Ver. 2.25");
	test_akai();
	test_s550();
	test_s700_floppy();
	test_s550_floppy();
	test_boards();
	test_akai_floppy();
	test_s900();
	test_iso();
	test_encodings();
	test_fat();
	test_format_fat();
	test_format_s700();
	test_smartmedia();
	test_vhd();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
