// Copyright 2026 Ian Karlsson
// Each format's reader on a small image made here: what is listed, what is left out, what entries use
#include "../src/internal.hpp"

#include <cstdio>
#include <algorithm>
#include <functional>
#include <string>
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

static samplerfs_io io_of(Bytes &b)
{
	return { &b, mem_read, b.size() };
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

static std::vector<std::string> names(samplerfs *fs, samplerfs_kind kind)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < samplerfs_count(fs); i++)
		if (samplerfs_entry_at(fs, i)->kind == kind)
			out.push_back(samplerfs_entry_at(fs, i)->name);
	return out;
}

// the first entry of the name, of the kind where one is given
static size_t find(samplerfs *fs, const char *name, int kind = -1)
{
	for (size_t i = 0; i < samplerfs_count(fs); i++)
		if (!strcmp(samplerfs_entry_at(fs, i)->name, name) && (kind < 0 || samplerfs_entry_at(fs, i)->kind == kind))
			return i;
	return SAMPLERFS_NONE;
}

static std::vector<std::string> used(samplerfs *fs, size_t i)
{
	size_t out[64];
	size_t n = samplerfs_refs(fs, i, out, 64);
	std::vector<std::string> v;
	for (size_t k = 0; k < n && k < 64; k++)
		v.push_back(samplerfs_entry_at(fs, out[k])->name);
	return v;
}

// ---- S-7xx: one of each list, a gap, a slot past the count, and the references between them
static Bytes s7_image(const char *system)
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
	size_t smp = 0x12AC * 512 + 2 * 48;
	b[smp + 44] = 1;
	b[smp + 45] = 72;
	const uint32_t points[5] = { 10, 100, 199, 250, 299 };
	for (int k = 0; k < 5; k++)
		put32(b, smp + 16 + 4 * k, points[k] << 8 | 0x80);
	b[smp + 36] = 4;
	return b;
}

static void test_s7(Bytes b, samplerfs_format format, const char *label)
{
	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io) == format);
	char err[64];
	samplerfs *fs = samplerfs_open(&io, err, sizeof err);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(samplerfs_label(fs), label));
	CHECK(samplerfs_format_of(fs) == format && samplerfs_entry_at(fs, 0)->format == format);
	CHECK((names(fs, SAMPLERFS_SAMPLE) == std::vector<std::string> { "SMP:Low", "SMP:High" }));
	CHECK((names(fs, SAMPLERFS_PARTIAL) == std::vector<std::string> { "PRT:Low", "PRT:High" }));
	size_t strings = find(fs, "STR:Strings");
	CHECK(strings != SAMPLERFS_NONE && !strcmp(samplerfs_entry_at(fs, strings)->tag, "STR"));
	CHECK(!samplerfs_entry_at(fs, find(fs, "PRT:Low"))->tag[0]);
	CHECK((used(fs, find(fs, "VOL:One")) == std::vector<std::string> { "PRF:One" }));
	CHECK((used(fs, find(fs, "PRF:One")) == std::vector<std::string> { "STR:Strings" }));
	CHECK((used(fs, strings) == std::vector<std::string> { "PRT:High", "PRT:Low" }));
	CHECK((used(fs, find(fs, "PRT:Low")) == std::vector<std::string> { "SMP:Low" }));
	CHECK(used(fs, find(fs, "SMP:Low")).empty());
	size_t high = find(fs, "SMP:High");
	CHECK(samplerfs_entry_at(fs, high)->number == 2 && samplerfs_entry_at(fs, high)->bytes == 9216);
	samplerfs_sample s;
	CHECK(samplerfs_sample_info(fs, high, &s) && s.rate == 44100 && s.key == 72 && s.frames == 4608);
	CHECK(s.start == 10 && s.end == 200 && s.loop == SAMPLERFS_LOOP_ALTERNATE && s.loop_start == 100 && s.loop_end == 200);
	CHECK(s.mode == 4 && !s.reverse);
	CHECK(!samplerfs_sample_info(fs, strings, &s));
	samplerfs_close(fs);
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
	for (uint32_t k = 0; k < sfs::akai::HEADER_WORDS; k++) {
		b[base + 2 + 2 * k] = uint8_t(k * sfs::akai::HEADER_STEP);
		b[base + 3 + 2 * k] = uint8_t(k * sfs::akai::HEADER_STEP >> 8);
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

// a program: a header and keygroups of block bytes each, each zone naming a sample
static void akai_program(Bytes &b, size_t at, size_t block, const std::vector<std::vector<const char *>> &keygroups)
{
	b[at] = 1;
	b[at + 42] = uint8_t(keygroups.size());
	for (size_t k = 0; k < keygroups.size(); k++) {
		size_t kg = at + (k + 1) * block;
		b[kg] = 2;
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
		akai_file(b, dir + 24, "PIANO C3", 's', 1000, 5);
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
	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_AKAI_S3000);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK((names(fs, SAMPLERFS_PARTITION) == std::vector<std::string> { "A", "B" }));
	CHECK((names(fs, SAMPLERFS_SAMPLE) == std::vector<std::string> { "PIANO C3", "STRINGS A4" }));
	CHECK((names(fs, SAMPLERFS_PROGRAM) == std::vector<std::string> { "PIANO PROG", "STRINGS", "LONG" }));
	CHECK((names(fs, SAMPLERFS_MULTI) == std::vector<std::string> { "SPLIT" }));
	CHECK((names(fs, SAMPLERFS_OTHER) == std::vector<std::string> { "FX" }));
	size_t piano = find(fs, "PIANO"), c3 = find(fs, "PIANO C3"), a4 = find(fs, "STRINGS A4");
	CHECK(samplerfs_entry_at(fs, c3)->parent == piano && samplerfs_entry_at(fs, piano)->parent == find(fs, "A"));
	CHECK(samplerfs_entry_at(fs, piano)->format == SAMPLERFS_AKAI_S1000 && samplerfs_entry_at(fs, find(fs, "A"))->format == SAMPLERFS_AKAI_S1000);
	CHECK(samplerfs_entry_at(fs, a4)->format == SAMPLERFS_AKAI_S3000 && samplerfs_entry_at(fs, a4)->number == 400);
	CHECK(samplerfs_entry_at(fs, find(fs, "B"))->format == SAMPLERFS_AKAI_S3000);
	CHECK(!strcmp(samplerfs_entry_at(fs, c3)->tag, "s") && !strcmp(samplerfs_entry_at(fs, a4)->tag, "s"));
	CHECK(!strcmp(samplerfs_entry_at(fs, find(fs, "FX"))->tag, "x"));
	CHECK((used(fs, find(fs, "PIANO PROG")) == std::vector<std::string> { "PIANO C3" }));
	CHECK((used(fs, find(fs, "STRINGS", SAMPLERFS_PROGRAM)) == std::vector<std::string> { "STRINGS A4" }));
	CHECK((used(fs, find(fs, "LONG")) == std::vector<std::string> { "STRINGS A4" }));
	CHECK((used(fs, find(fs, "SPLIT")) == std::vector<std::string> { "LONG", "STRINGS" }));
	CHECK(!strcmp(samplerfs_entry_at(fs, find(fs, "SPLIT"))->tag, "m"));
	samplerfs_sample s;
	CHECK(samplerfs_sample_info(fs, c3, &s) && s.rate == 22050 && s.key == 60 && s.frames == 500);
	CHECK(s.start == 2 && s.end == 500 && s.loop == SAMPLERFS_LOOP_FORWARD && s.loop_start == 300 && s.loop_end == 400);
	CHECK(!samplerfs_sample_info(fs, a4, &s));  // no header
	samplerfs_close(fs);

	b = akai_image(false);
	io = io_of(b);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_AKAI_S1000);

	// a size out of range is no partition
	b = akai_image(true);
	put16(b, 0, 0x1E01);
	io = io_of(b);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_UNKNOWN);
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

	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_S550);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(samplerfs_label(fs), "Roland Sound Library    L-CD9"));
	CHECK((names(fs, SAMPLERFS_AREA) == std::vector<std::string> { "Pianos #1" }));
	CHECK((names(fs, SAMPLERFS_PATCH) == std::vector<std::string> { "Piano Split" }));
	CHECK((names(fs, SAMPLERFS_TONE) == std::vector<std::string> { "P-C3", "P-C5" }));
	size_t pianos = find(fs, "Pianos #1"), c3 = find(fs, "P-C3"), c5 = find(fs, "P-C5");
	CHECK(!strcmp(samplerfs_entry_at(fs, pianos)->tag, "0") && samplerfs_entry_at(fs, c5)->number == 2);
	CHECK(samplerfs_entry_at(fs, c3)->parent == pianos && samplerfs_entry_at(fs, c3)->bytes == 24576);
	CHECK((used(fs, find(fs, "Piano Split")) == std::vector<std::string> { "P-C3", "P-C5" }));
	samplerfs_sample s;
	CHECK(samplerfs_sample_info(fs, c3, &s) && s.rate == 30000 && s.key == 60 && s.frames == 0x3000);
	CHECK(s.start == 0 && s.end == 0x3000 && s.loop == SAMPLERFS_LOOP_FORWARD && s.loop_start == 0x1000 && s.loop_end == 0x3000);
	CHECK(samplerfs_sample_info(fs, c5, &s) && s.rate == 15000 && s.key == 84 && s.frames == 0x3000);
	CHECK(s.start == 0x100 && s.end == 0x200 && s.reverse && s.loop == SAMPLERFS_LOOP_NONE && s.mode == 3);
	samplerfs_close(fs);
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
	Bytes b(24 * 2048, 0);
	memcpy(&b[16 * 2048], "\1CD001", 6);
	put_name(b, 16 * 2048 + 40, "LOOPS", 32);
	size_t root = 16 * 2048 + 156;
	b[root + 2] = 20;
	b[root + 11] = 0x08;
	iso_dir(b, 20, 20, { { "README.TXT;1", 23 }, { "DRUMS", 21 }, { "DOCS", 22 } });
	iso_dir(b, 21, 20, { { "KICK.WAV;1", 23 }, { "SNARE.AIF;1", 23 } });
	iso_dir(b, 22, 20, { { "INFO.TXT;1", 23 } });
	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_ISO9660);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(samplerfs_label(fs), "LOOPS"));
	CHECK((names(fs, SAMPLERFS_DIRECTORY) == std::vector<std::string> { "DRUMS" }));
	CHECK((names(fs, SAMPLERFS_FILE) == std::vector<std::string> { "KICK.WAV", "SNARE.AIF" }));
	size_t kick = find(fs, "KICK.WAV");
	CHECK(samplerfs_entry_at(fs, kick)->bytes == 1234 && samplerfs_entry_at(fs, kick)->parent == find(fs, "DRUMS"));
	CHECK(samplerfs_entry_at(fs, kick)->format == SAMPLERFS_ISO9660);
	samplerfs_close(fs);
}

static void test_others()
{
	Bytes b(4096, 0);
	samplerfs_io io = io_of(b);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs && samplerfs_format_of(fs) == SAMPLERFS_UNKNOWN && samplerfs_count(fs) == 0);
	samplerfs_close(fs);

	memcpy(&b[0], "EMU3", 4);
	CHECK(samplerfs_probe(&io) == SAMPLERFS_EMU3);

	Bytes tiny(100, 0);
	io = io_of(tiny);
	char err[64] = "";
	CHECK(!samplerfs_open(&io, err, sizeof err) && err[0]);
	CHECK(!samplerfs_open_file("/nonexistent/disc.iso", err, sizeof err));
}

int main()
{
	Bytes s760 = s7_image(" S-760 System Disk    Ver. 2.24");
	test_s7(s760, SAMPLERFS_S760, "S-760 System Disk    Ver. 2.24");
	test_s7(raw_of(s760), SAMPLERFS_S760, "S-760 System Disk    Ver. 2.24");
	test_s7(s7_image(" SYS-772 HardDisk Sys Ver. 2.25"), SAMPLERFS_S770, "SYS-772 HardDisk Sys Ver. 2.25");
	test_akai();
	test_s550();
	test_iso();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
