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

// the FAT probe on its own leaves every other format's image alone, floppies of DOS sizes among them
static bool not_fat(Bytes &b)
{
	samplerfs_io io = io_of(b);
	return sfs::Fat::probe(sfs::Image(io)) == SAMPLERFS_UNKNOWN;
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
	samplerfs_io io = io_of(b);
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_ISO9660 && not_fat(b));
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
	samplerfs_sample s;
	CHECK(samplerfs_sample_info(fs, kick, &s) && s.rate == 32000 && s.key == 36 && s.frames == 500 && s.end == 500);
	CHECK(s.loop == SAMPLERFS_LOOP_FORWARD && s.loop_start == 10 && s.loop_end == 400 && s.mode == 0);
	CHECK(samplerfs_sample_info(fs, find(fs, "SNARE.AIF"), &s) && s.rate == 22050 && s.key == 64 && s.frames == 300);
	CHECK(s.loop == SAMPLERFS_LOOP_ALTERNATE && s.loop_start == 20 && s.loop_end == 250 && s.mode == 2);
	CHECK(!samplerfs_sample_info(fs, find(fs, "DRUMS"), &s));
	samplerfs_close(fs);
}

// ---- FAT: images sparse in memory, a volume laid out by hand
struct Sparse {
	static constexpr uint64_t PAGE = 4096;
	uint64_t size;
	std::map<uint64_t, std::vector<uint8_t>> pages;
	explicit Sparse(uint64_t size) : size(size) {}
	void put(uint64_t at, const void *p, size_t n)
	{
		auto *s = static_cast<const uint8_t *>(p);
		for (size_t k = 0; k < n; k++, at++) {
			auto &page = pages[at / PAGE];
			if (page.empty())
				page.assign(PAGE, 0);
			page[at % PAGE] = s[k];
		}
	}
	void get(uint64_t at, void *p, size_t n) const
	{
		auto *d = static_cast<uint8_t *>(p);
		for (size_t k = 0; k < n; k++, at++) {
			auto it = pages.find(at / PAGE);
			d[k] = it == pages.end() ? 0 : it->second[at % PAGE];
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

static samplerfs_io io_of(Sparse &s)
{
	return { &s, sparse_read, s.size };
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

static void test_fat_volume(Sparse &img, samplerfs_medium medium, bool damaged, const char *label)
{
	samplerfs_io io = io_of(img);
	samplerfs_medium m;
	CHECK(samplerfs_probe(&io, &m) == SAMPLERFS_FAT && m == medium);
	samplerfs *fs = samplerfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	CHECK(samplerfs_format_of(fs) == SAMPLERFS_FAT && samplerfs_medium_of(fs) == medium);
	CHECK(!strcmp(samplerfs_label(fs), label));
	const char *kick_name = "Kick drum \xE2\x80\x93 long name.wav";
	std::vector<std::string> dirs { "Samples", "DRUMS" }, files { "README.TXT", kick_name, "WRONG~1.WAV" };
	if (damaged) {
		dirs.push_back("SELF");
		dirs.push_back("FAR");
		files.push_back("LOOP.WAV");
	}
	CHECK(names(fs, SAMPLERFS_DIRECTORY) == dirs);
	CHECK(names(fs, SAMPLERFS_FILE) == files);
	size_t samples = find(fs, "Samples"), drums = find(fs, "DRUMS"), kick = find(fs, kick_name);
	CHECK(samplerfs_entry_at(fs, samples)->parent == SAMPLERFS_NONE && samplerfs_entry_at(fs, samples)->number == 1);
	CHECK(samplerfs_entry_at(fs, drums)->parent == samples);
	CHECK(kick != SAMPLERFS_NONE && samplerfs_entry_at(fs, kick)->parent == drums);
	CHECK(samplerfs_entry_at(fs, find(fs, "README.TXT"))->bytes == 100);
	samplerfs_sample s;
	CHECK(samplerfs_sample_info(fs, kick, &s) && s.rate == 44100 && s.key == 60);
	CHECK(s.frames == samplerfs_entry_at(fs, kick)->bytes / 2 - 56 && s.end == s.frames);
	CHECK(s.loop == SAMPLERFS_LOOP_FORWARD && s.loop_start == 100 && s.loop_end == 900);
	CHECK(!samplerfs_sample_info(fs, find(fs, "README.TXT"), &s) && !samplerfs_sample_info(fs, drums, &s));
	if (damaged)
		CHECK(!samplerfs_sample_info(fs, find(fs, "LOOP.WAV"), &s));
	samplerfs_close(fs);
}

static void test_fat()
{
	// FAT12: a 1.44 MB floppy, the label in the root directory
	for (bool damaged : { false, true }) {
		Sparse img(2880 * 512);
		FatVolume v { img, 0, 12, 1, 1, 224, 9, 2880, 0xF0 };
		fat_fill(v, damaged, true);
		test_fat_volume(img, SAMPLERFS_MEDIUM_FLOPPY, damaged, "TEST VOLUME");
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
		test_fat_volume(img, SAMPLERFS_MEDIUM_DISK, damaged, "BPB LABEL");
	}
	// FAT32: a bare volume of 66 000 clusters of 512 bytes, sparse in memory
	for (bool damaged : { false, true }) {
		const uint64_t clusters = 66000, fat_sectors = (clusters + 2) * 4 / 512 + 1, sectors = 32 + 2 * fat_sectors + clusters;
		Sparse img(sectors * 512);
		FatVolume v { img, 0, 32, 1, 32, 0, uint32_t(fat_sectors), sectors, 0xF8 };
		fat_fill(v, damaged, true);
		test_fat_volume(img, SAMPLERFS_MEDIUM_DISK, damaged, "TEST VOLUME");
	}
	// a bare volume's boot sector without its jump is no FAT
	Sparse img(2880 * 512);
	FatVolume v { img, 0, 12, 1, 1, 224, 9, 2880, 0xF0 };
	v.boot("");
	samplerfs_io io = io_of(img);
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_FAT);
	uint8_t no_jump = 0;
	img.put(0, &no_jump, 1);
	CHECK(samplerfs_probe(&io, nullptr) == SAMPLERFS_UNKNOWN);
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
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_S760 && medium == SAMPLERFS_MEDIUM_FLOPPY && not_fat(b));
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
	CHECK(sfs::roland::S700Disk::probe(sfs::Image(io)) == SAMPLERFS_UNKNOWN);
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
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_S550 && medium == SAMPLERFS_MEDIUM_FLOPPY && not_fat(b));
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
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_AKAI_S1000 && medium == SAMPLERFS_MEDIUM_FLOPPY && not_fat(b));
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
	CHECK(samplerfs_probe(&io, &medium) == SAMPLERFS_AKAI_S900 && medium == SAMPLERFS_MEDIUM_FLOPPY && not_fat(b));
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
	test_fat();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
