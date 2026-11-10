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
	size_t smp = 0x12AC * 512 + 2 * 48;
	b[smp + 44] = 1;
	b[smp + 45] = 72;
	const uint32_t points[5] = { 10, 100, 199, 250, 299 };
	for (int k = 0; k < 5; k++)
		put32(b, smp + 16 + 4 * k, points[k] << 8 | 0x80);
	b[smp + 36] = 4;
	return b;
}

static void test_s700(Bytes b, samplerfs_format format, const char *label)
{
	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io, nullptr) == format);
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
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_AKAI_S3000);
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
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_AKAI_S1000);

	// a size out of range is no partition
	b = akai_image(true);
	put16(b, 0, 0x1E01);
	io = io_of(b);
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_UNKNOWN);
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
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_S550);
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
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_ISO9660);
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
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_EMU3);

	Bytes tiny(100, 0);
	io = io_of(tiny);
	char err[64] = "";
	CHECK(!samplerfs_open(&io, err, sizeof err) && err[0]);
	CHECK(!samplerfs_open_file("/nonexistent/disc.iso", err, sizeof err));
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

	samplerfs_io io = io_of(b);
	samplerfs_medium medium = SAMPLERFS_MEDIUM_UNKNOWN;
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_S760 && medium == SAMPLERFS_MEDIUM_FLOPPY);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(samplerfs_medium_of(fs) == SAMPLERFS_MEDIUM_FLOPPY);
	CHECK(!strcmp(samplerfs_label(fs), "S-760 Sound Disk     Ver. 2.24"));
	CHECK((names(fs, SAMPLERFS_VOLUME) == std::vector<std::string> { "PAD:Floppy Vol" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Vol")) == std::vector<std::string> { "PAD:Floppy Pfm" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Pfm")) == std::vector<std::string> { "PAD:Floppy Pch" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Pch")) == std::vector<std::string> { "PAD:Floppy Prt" }));
	CHECK((used(fs, find(fs, "PAD:Floppy Prt")) == std::vector<std::string> { "PAD:Floppy Smp" }));
	size_t s = find(fs, "PAD:Floppy Smp");
	CHECK(!strcmp(samplerfs_entry_at(fs, s)->tag, "PAD") && samplerfs_entry_at(fs, s)->bytes == 3 * 9216);
	samplerfs_sample info;
	CHECK(samplerfs_sample_info(fs, s, &info) && info.rate == 44100 && info.key == 60 && info.frames == 3 * 4608);
	CHECK(info.loop == SAMPLERFS_LOOP_ALTERNATE && info.loop_start == 100 && info.loop_end == 1000);
	samplerfs_close(fs);

	// a save under way lists nothing; the disk probe leaves a floppy alone
	b[0x101] = 0xFF;
	fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs && samplerfs_format_of(fs) == SAMPLERFS_S760 && samplerfs_count(fs) == 0);
	samplerfs_close(fs);
	CHECK(sfs::s700::probe(sfs::Image(io)) == SAMPLERFS_UNKNOWN);
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

	samplerfs_io io = io_of(b);
	samplerfs_medium medium;
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_S550 && medium == SAMPLERFS_MEDIUM_FLOPPY);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(samplerfs_label(fs), "S-550 System   Ver. 1.12"));
	CHECK((names(fs, SAMPLERFS_AREA) == std::vector<std::string> { "Strings Vol1" }));
	CHECK((used(fs, find(fs, "Strings")) == std::vector<std::string> { "Str C3" }));
	CHECK(samplerfs_entry_at(fs, find(fs, "Str C3"))->bytes == 2 * 24576);
	samplerfs_close(fs);

	memcpy(&b[4], "S330 MT25C", 10);
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_S330);
	memcpy(&b[4], "S-50 MT25A", 10);
	fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs && samplerfs_format_of(fs) == SAMPLERFS_S50 && samplerfs_count(fs) == 0);
	samplerfs_close(fs);
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
	srx.be(0x42, 2, 2);
	const uint32_t waves = 0x1F00000, patches = 0x1F80000;
	srx.be(0x80, 2, 2), srx.be(0x82, 3, 2), srx.be(0x84, waves, 4), srx.be(0x88, 0x100, 4);
	srx.be(0x90, 4, 2), srx.be(0x92, 7, 2), srx.be(0x94, patches, 4), srx.be(0x98, 0x1000, 4);
	srx.be(waves, 2, 2), srx.be(waves + 4, 0x0C, 4), srx.be(waves + 8, 8, 4);
	srx.be(waves + 0x0C, 0x40, 4), srx.be(waves + 0x14, 0x60, 4);
	srx.text(waves + 0x40, "Kick 1");
	srx.text(waves + 0x60, "Snare 1");
	srx.be(patches, 1, 2), srx.be(patches + 4, 0x0E, 4), srx.be(patches + 8, 574, 4);
	srx.packed(patches + 0x0E, "Test Patch", true);
	samplerfs_io io = io_of(srx.b);
	samplerfs_medium medium;
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_SRX && medium == SAMPLERFS_MEDIUM_BOARD);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (fs) {
		CHECK(!strcmp(samplerfs_label(fs), "SRX-99 Test"));
		CHECK((names(fs, SAMPLERFS_WAVE) == std::vector<std::string> { "Kick 1", "Snare 1" }));
		CHECK((names(fs, SAMPLERFS_PATCH) == std::vector<std::string> { "Test Patch" }));
		size_t bank = samplerfs_entry_at(fs, find(fs, "Test Patch"))->parent;
		CHECK(bank != SAMPLERFS_NONE && !strcmp(samplerfs_entry_at(fs, bank)->tag, "4:7"));
		samplerfs_close(fs);
	}

	// an SO-PCM1 card: waves, a patch bank in the JV-880's shape and one in the JV-1080's rhythm set shape
	Rom card { Bytes(1 << 20, 0), srjv_lines, 19 };
	memcpy(&card.b[0], "Roland JV80 O\xb0S\xac", 16);
	memcpy(&card.b[0x10], "Card09   Ver1.00", 16);
	card.put(0x51, 1), card.be(0x62, 2, 2), card.be(0x84, 0xF0000, 4);
	card.put(0x53, 2), card.be(0x66, 1, 2), card.be(0x8C, 0xF1000, 4);
	card.put(0x54, 4), card.be(0x68, 1, 2), card.be(0x90, 0xF2000, 4);
	card.text(0xF0000, "Piano A");
	card.text(0xF0000 + 60, "Piano B");
	card.text(0xF1000, "Card Piano");
	card.packed(0xF2000, "Card Drums", false);
	io = io_of(card.b);
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_SRJV80 && medium == SAMPLERFS_MEDIUM_CARD);
	fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (fs) {
		CHECK(!strcmp(samplerfs_label(fs), "Card09   Ver1.00"));
		CHECK((names(fs, SAMPLERFS_WAVE) == std::vector<std::string> { "Piano A", "Piano B" }));
		CHECK((names(fs, SAMPLERFS_PATCH) == std::vector<std::string> { "Card Piano" }));
		CHECK((names(fs, SAMPLERFS_RHYTHM_SET) == std::vector<std::string> { "Card Drums" }));
		samplerfs_close(fs);
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
	akai_file(b, 24, "PIANO C3", 's', 0x8C + 2 * 300, 7);
	put16(b, 0x600 + 2 * 5, 6);
	put16(b, 0x600 + 2 * 6, 0xC000);
	put16(b, 0x600 + 2 * 7, 0xC000);
	Bytes pr(150 * 8, 0);
	akai_program(pr, 0, 150, { { "PIANO C3" } });
	memcpy(&b[5 * K], pr.data(), pr.size());
	size_t smp = 7 * K;
	b[smp] = 3;
	b[smp + 2] = 60;
	put32(b, smp + 0x1A, 300);
	put32(b, smp + 0x22, 299);
	put16(b, smp + 0x8A, 44100);

	samplerfs_io io = io_of(b);
	samplerfs_medium medium;
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_AKAI_S1000 && medium == SAMPLERFS_MEDIUM_FLOPPY);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(!strcmp(samplerfs_label(fs), "MY FLOPPY"));
	CHECK((names(fs, SAMPLERFS_VOLUME) == std::vector<std::string> { "MY FLOPPY" }));
	CHECK((used(fs, find(fs, "PIANO")) == std::vector<std::string> { "PIANO C3" }));
	samplerfs_sample info;
	CHECK(samplerfs_sample_info(fs, find(fs, "PIANO C3"), &info) && info.rate == 44100 && info.frames == 300 && info.end == 300);
	samplerfs_close(fs);

	b[16] = 0xF0;
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_AKAI_S3000);
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

	samplerfs_io io = io_of(b);
	samplerfs_medium medium;
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_AKAI_S900 && medium == SAMPLERFS_MEDIUM_FLOPPY);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK((names(fs, SAMPLERFS_PROGRAM) == std::vector<std::string> { "BASS" }));
	CHECK((used(fs, find(fs, "BASS")) == std::vector<std::string> { "BASS E1" }));
	samplerfs_sample info;
	CHECK(samplerfs_sample_info(fs, find(fs, "BASS E1"), &info) && info.rate == 25000 && info.key == 40);
	CHECK(info.frames == 1000 && info.start == 10 && info.end == 1000);
	CHECK(info.loop == SAMPLERFS_LOOP_FORWARD && info.loop_start == 800 && info.loop_end == 1000);
	samplerfs_close(fs);

	// a lower-case type is no S900's
	b[24 + 16] = 's';
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_UNKNOWN);
}

int main()
{
	Bytes s760 = s700_image(" S-760 System Disk    Ver. 2.24");
	test_s700(s760, SAMPLERFS_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(raw_of(s760), SAMPLERFS_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(s700_image(" SYS-772 HardDisk Sys Ver. 2.25"), SAMPLERFS_S770, "SYS-772 HardDisk Sys Ver. 2.25");
	test_akai();
	test_s550();
	test_s700_floppy();
	test_s550_floppy();
	test_boards();
	test_akai_floppy();
	test_s900();
	test_iso();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
