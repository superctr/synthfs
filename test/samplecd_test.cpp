// Copyright 2026 Ian Karlsson
// Each format's reader on a small image made here: what is listed, what is left out, what entries use
#include "../src/internal.hpp"

#include <cstdio>
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

static samplecd_io io_of(Bytes &b)
{
	return { &b, mem_read, b.size() };
}

static void put16(Bytes &b, size_t at, uint16_t v)
{
	b[at] = uint8_t(v);
	b[at + 1] = uint8_t(v >> 8);
}

static void put_name(Bytes &b, size_t at, const char *name, size_t len)
{
	memset(&b[at], ' ', len);
	memcpy(&b[at], name, strlen(name));
}

static std::vector<std::string> names(samplecd *cd, samplecd_kind kind)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < samplecd_count(cd); i++)
		if (samplecd_entry_at(cd, i)->kind == kind)
			out.push_back(samplecd_entry_at(cd, i)->name);
	return out;
}

static size_t find(samplecd *cd, const char *name)
{
	for (size_t i = 0; i < samplecd_count(cd); i++)
		if (!strcmp(samplecd_entry_at(cd, i)->name, name))
			return i;
	return SAMPLECD_NONE;
}

static std::vector<std::string> used(samplecd *cd, size_t i)
{
	size_t out[64];
	size_t n = samplecd_refs(cd, i, out, 64);
	std::vector<std::string> v;
	for (size_t k = 0; k < n && k < 64; k++)
		v.push_back(samplecd_entry_at(cd, out[k])->name);
	return v;
}

// ---- S-7xx: one of each list, a gap, a slot past the count, and the references between them
static Bytes s7_image()
{
	Bytes b((0x15AC + 18 * 4) * 512, 0);
	memcpy(&b[4], "S770 MR25A", 10);
	put_name(b, 0x20, " S-760 System Disk", 32);
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
	return b;
}

static void test_s7(Bytes b)
{
	samplecd_io io = io_of(b);
	CHECK(samplecd_probe(&io) == SAMPLECD_S7XX);
	char err[64];
	samplecd *cd = samplecd_open(&io, err, sizeof err);
	CHECK(cd);
	if (!cd)
		return;
	CHECK(!strcmp(samplecd_label(cd), "S-760 System Disk"));
	CHECK(samplecd_machines(cd) == (SAMPLECD_S760 | SAMPLECD_S770 | SAMPLECD_XV5080));
	CHECK((names(cd, SAMPLECD_SAMPLE) == std::vector<std::string> { "SMP:Low", "SMP:High" }));
	CHECK((names(cd, SAMPLECD_PARTIAL) == std::vector<std::string> { "PRT:Low", "PRT:High" }));
	size_t strings = find(cd, "STR:Strings");
	CHECK(strings != SAMPLECD_NONE && !strcmp(samplecd_entry_at(cd, strings)->category, "STR"));
	CHECK(!samplecd_entry_at(cd, find(cd, "PRT:Low"))->category[0]);
	CHECK((used(cd, find(cd, "VOL:One")) == std::vector<std::string> { "PRF:One" }));
	CHECK((used(cd, find(cd, "PRF:One")) == std::vector<std::string> { "STR:Strings" }));
	CHECK((used(cd, strings) == std::vector<std::string> { "PRT:High", "PRT:Low" }));
	CHECK((used(cd, find(cd, "PRT:Low")) == std::vector<std::string> { "SMP:Low" }));
	CHECK(used(cd, find(cd, "SMP:Low")).empty());
	size_t high = find(cd, "SMP:High");
	CHECK(samplecd_entry_at(cd, high)->number == 2 && samplecd_entry_at(cd, high)->bytes == 9216);
	samplecd_sample s;
	CHECK(samplecd_sample_info(cd, high, &s) && s.rate == 44100 && s.key == 72 && s.frames == 4608);
	CHECK(!samplecd_sample_info(cd, strings, &s));
	samplecd_close(cd);
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
	memcpy(&b[base + 2], scd::akai::partition_header, 196);
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

static Bytes akai_image(bool with_s1000)
{
	Bytes b(16 * AKAI_BLOCK, 0);
	akai_partition(b, 0, 8, [&](uint64_t base) {
		akai_name(b, base + 0xCA, "PIANO");
		b[base + 0xCA + 12] = with_s1000 ? 1 : 3;
		put16(b, base + 0xCA + 14, 3);
		size_t dir = base + 3 * AKAI_BLOCK;
		akai_file(b, dir, "PIANO PROG", 'p', 450, 4);
		akai_file(b, dir + 24, "PIANO C3", 's', 1000, 5);
		if (with_s1000)
			akai_file(b, dir + 126 * 24, "VOLUME DATA", 's', 1, 6);  // past an S1000 directory's 126
		size_t smp = base + 5 * AKAI_BLOCK;
		b[smp] = 3;
		b[smp + 2] = 60;
		b[smp + 0x1A] = 0xF4;
		b[smp + 0x1B] = 0x01;
		put16(b, smp + 0x8A, 22050);
	});
	akai_partition(b, 8 * AKAI_BLOCK, 8, [&](uint64_t base) {
		akai_name(b, base + 0xCA, "STRINGS");
		b[base + 0xCA + 12] = 3;
		put16(b, base + 0xCA + 14, 3);
		put16(b, base + 0x70A + 2 * 3, 5);  // the directory's second block
		akai_file(b, base + 3 * AKAI_BLOCK, "STRINGS", 0xF0, 600, 6);
		akai_file(b, base + 5 * AKAI_BLOCK + (400 * 24 - AKAI_BLOCK), "STRINGS A4", 0xF3, 2000, 7);
	});
	return b;
}

static void test_akai()
{
	Bytes b = akai_image(true);
	samplecd_io io = io_of(b);
	CHECK(samplecd_probe(&io) == SAMPLECD_AKAI);
	samplecd *cd = samplecd_open(&io, nullptr, 0);
	CHECK(cd);
	if (!cd)
		return;
	CHECK((names(cd, SAMPLECD_PARTITION) == std::vector<std::string> { "A", "B" }));
	CHECK((names(cd, SAMPLECD_SAMPLE) == std::vector<std::string> { "PIANO C3", "STRINGS A4" }));
	CHECK((names(cd, SAMPLECD_PROGRAM) == std::vector<std::string> { "PIANO PROG", "STRINGS" }));
	size_t piano = find(cd, "PIANO"), c3 = find(cd, "PIANO C3"), a4 = find(cd, "STRINGS A4");
	CHECK(samplecd_entry_at(cd, c3)->parent == piano && samplecd_entry_at(cd, piano)->parent == find(cd, "A"));
	CHECK(samplecd_entry_at(cd, piano)->machines == (SAMPLECD_S760 | SAMPLECD_XV5080));
	CHECK(samplecd_entry_at(cd, a4)->machines == SAMPLECD_XV5080 && samplecd_entry_at(cd, a4)->number == 400);
	CHECK(samplecd_machines(cd) == (SAMPLECD_S760 | SAMPLECD_XV5080));
	samplecd_sample s;
	CHECK(samplecd_sample_info(cd, c3, &s) && s.rate == 22050 && s.key == 60 && s.frames == 500);
	samplecd_close(cd);

	b = akai_image(false);
	io = io_of(b);
	cd = samplecd_open(&io, nullptr, 0);
	CHECK(cd && samplecd_machines(cd) == SAMPLECD_XV5080);
	samplecd_close(cd);

	// a size out of range is no partition
	b = akai_image(true);
	put16(b, 0, 0x1E01);
	io = io_of(b);
	CHECK(samplecd_probe(&io) != SAMPLECD_AKAI);
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
	samplecd_io io = io_of(b);
	CHECK(samplecd_probe(&io) == SAMPLECD_ISO9660);
	samplecd *cd = samplecd_open(&io, nullptr, 0);
	CHECK(cd);
	if (!cd)
		return;
	CHECK(!strcmp(samplecd_label(cd), "LOOPS"));
	CHECK((names(cd, SAMPLECD_DIRECTORY) == std::vector<std::string> { "DRUMS" }));
	CHECK((names(cd, SAMPLECD_FILE) == std::vector<std::string> { "KICK.WAV", "SNARE.AIF" }));
	size_t kick = find(cd, "KICK.WAV");
	CHECK(samplecd_entry_at(cd, kick)->bytes == 1234 && samplecd_entry_at(cd, kick)->parent == find(cd, "DRUMS"));
	CHECK(samplecd_machines(cd) == SAMPLECD_XV5080);
	samplecd_close(cd);
}

static void test_others()
{
	Bytes b(4096, 0);
	samplecd_io io = io_of(b);
	samplecd *cd = samplecd_open(&io, nullptr, 0);
	CHECK(cd && samplecd_format_of(cd) == SAMPLECD_UNKNOWN && samplecd_count(cd) == 0 && samplecd_machines(cd) == 0);
	samplecd_close(cd);

	memcpy(&b[0], "* ROLAND S-550 *", 16);
	cd = samplecd_open(&io, nullptr, 0);
	CHECK(cd && samplecd_format_of(cd) == SAMPLECD_S550 && samplecd_machines(cd) == (SAMPLECD_S760 | SAMPLECD_S770));
	samplecd_close(cd);

	memcpy(&b[0], "EMU3", 4);
	CHECK(samplecd_probe(&io) == SAMPLECD_EMU3);

	Bytes tiny(100, 0);
	io = io_of(tiny);
	char err[64] = "";
	CHECK(!samplecd_open(&io, err, sizeof err) && err[0]);
	CHECK(!samplecd_open_file("/nonexistent/disc.iso", err, sizeof err));
}

int main()
{
	Bytes s7 = s7_image();
	test_s7(s7);
	test_s7(raw_of(s7));
	test_akai();
	test_iso();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
