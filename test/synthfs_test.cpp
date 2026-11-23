// Copyright 2026 Ian Karlsson
// Each format's reader on a small image made here: what is listed, what is left out, what entries use
#include "../src/akai/akai.hpp"
#include "../src/fat.hpp"
#include "../src/maps.hpp"
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
		// each conversion its exports name reads whole, the same in one piece as in several; none it does not name
		char exports[64] = "";
		syfs_get_str(fs, syfs_node(n), SYFS_PROP_EXPORTS, exports, sizeof exports);
		for (syfs_key as : { SYFS_AS_RAW, SYFS_AS_WAV, SYFS_AS_S16 }) {
			const bool named = n && (" " + std::string(exports) + " ").find(" " + std::string(syfs_name(as)) + " ") != std::string::npos;
			const size_t total = syfs_read(fs, syfs_node(n), as, 0, nullptr, 0);
			CHECK(named == (total != 0));
			if (!total || total > (8u << 20))
				continue;
			std::vector<uint8_t> whole(total + 16, 0xEE), parts(total, 0);
			const size_t got = syfs_read(fs, syfs_node(n), as, 0, whole.data(), whole.size());
			// a wave cut short in the image reads short as values, as does a file whose chain breaks; a WAV file is
			// whole whatever the wave
			CHECK(as == SYFS_AS_WAV ? got == total : got <= total);
			CHECK(whole[total] == 0xEE && !syfs_read(fs, syfs_node(n), as, total, whole.data(), 16));
			const size_t step = as == SYFS_AS_S16 ? 2 * size_t(num(fs, syfs_node(n), SYFS_PROP_CHANNELS, 1)) * 37 : 1001;
			size_t at = 0;
			for (size_t k; at < got && (k = syfs_read(fs, syfs_node(n), as, at, &parts[at], std::min(step, got - at))); at += k)
				;
			CHECK(at == got && !memcmp(parts.data(), whole.data(), got));
			if (as == SYFS_AS_WAV) {
				const uint32_t riff = uint32_t(whole[4] | whole[5] << 8 | whole[6] << 16 | uint32_t(whole[7]) << 24);
				const uint32_t data = uint32_t(whole[40] | whole[41] << 8 | whole[42] << 16 | uint32_t(whole[43]) << 24);
				CHECK(!memcmp(whole.data(), "RIFF", 4) && riff == total - 8 && !memcmp(&whole[36], "data", 4));
				CHECK(data == uint64_t(num(fs, syfs_node(n), SYFS_PROP_FRAMES)) * uint64_t(num(fs, syfs_node(n), SYFS_PROP_CHANNELS)) * 2);
				// its frames are the values read
				std::vector<int16_t> pcm(data / 2);
				const size_t values = syfs_read(fs, syfs_node(n), SYFS_AS_S16, 0, pcm.data(), data) / 2;
				bool same = true;
				for (size_t v = 0; v < values; v++)
					same = same && int16_t(whole[44 + 2 * v] | whole[45 + 2 * v] << 8) == pcm[v];
				CHECK(same);
			}
		}
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
	// a disk's name: twelve characters of ASCII after the ID, "Formatted" without one; a floppy holds none
	{
		Sparse named(8 << 20);
		syfs_io nw = writer_of(named), nr = io_of(named);
		uint8_t at[16];
		CHECK(syfs_create(&nw, SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, "My Sounds 12", err, sizeof err));
		named.get(0x100, at, 16);
		CHECK(!memcmp(at, "ID0:My Sounds 12", 16));
		syfs_image *fs = syfs_open(&nr, nullptr, 0);
		CHECK(fs && text_of(fs, SYFS_PROP_VOLUME) == "My Sounds 12" && !strcmp(label_of(fs), "S-760 System Disk    Ver. 2.24"));
		close_image(fs);
		CHECK(syfs_create(&nw, SYFS_FORMAT_S770, SYFS_MEDIUM_DISK, "Short", err, sizeof err));
		named.get(0x100, at, 16);
		CHECK(!memcmp(at, "ID0:Short       ", 16));
		CHECK(syfs_create(&nw, SYFS_FORMAT_S770, SYFS_MEDIUM_DISK, nullptr, err, sizeof err));
		fs = syfs_open(&nr, nullptr, 0);
		CHECK(fs && text_of(fs, SYFS_PROP_VOLUME) == "Formatted");
		close_image(fs);
		err[0] = 0;
		CHECK(!syfs_create(&nw, SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, "Thirteen long", err, sizeof err) && err[0]);
		CHECK(!syfs_create(&nw, SYFS_FORMAT_S760, SYFS_MEDIUM_DISK, "Bj\xC3\xB6rk", err, sizeof err));
		Sparse flop(1440 * 1024);
		syfs_io fw = writer_of(flop);
		CHECK(!syfs_create(&fw, SYFS_FORMAT_S760, SYFS_MEDIUM_FLOPPY, "Name", err, sizeof err));
	}

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
	{
		// the records are memory's: Data Sets of the first 256 bytes of the patch, 128 of the partial, 48 of the sample
		Bytes m(syfs_read(fs, syfs_node(find(fs, "PAD:Floppy Pch") + 1), SYFS_AS_SYX, 0, nullptr, 0));
		CHECK(m.size() == 4 * (128 + 11) && syfs_read(fs, syfs_node(find(fs, "PAD:Floppy Pch") + 1), SYFS_AS_SYX, 0, m.data(), m.size()) == m.size());
		CHECK(m[5] == 0 && m[6] == 4 && m[9] == 0x0 && m[10] == 0xF && m[9 + 2 * 0x20] == 0xF && m[139 + 9 + 2 * (0x20 + 39 - 64)] == 0);
		CHECK(syfs_read(fs, syfs_node(s + 1), SYFS_AS_SYX, 0, nullptr, 0) == 96 + 11);
		CHECK(!syfs_read(fs, syfs_node(find(fs, "PAD:Floppy Vol") + 1), SYFS_AS_SYX, 0, nullptr, 0));
	}
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
		// a rhythm set of the JV-1080's shape reads as Data Sets, at the user rhythm set of its number; a JV-880's
		// patch as that machine's, its common and four tones
		CHECK(syfs_read(fs, syfs_node(find(fs, "Card Drums") + 1), SYFS_AS_SYX, 0, nullptr, 0) == 12 + 64 * 58 + 65 * 11
			&& syfs_read(fs, syfs_node(find(fs, "Card Piano") + 1), SYFS_AS_SYX, 0, nullptr, 0) == 34 + 11 + 4 * (116 + 11));
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
	// a card formatted: its information block, and in its first logical blocks a FAT volume with its label and the
	// directory ROLAND, every page written with its block's address and the guards of its halves
	for (unsigned mb : { 8u, 16u, 32u, 64u, 128u }) {
		Sparse made(1024 + uint64_t(mb) * 1048576 / 32 * 33);
		w = writer_of(made);
		CHECK(syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_SMARTMEDIA, "xv-5080", err, sizeof err));
		io = io_of(made);
		CHECK(probe(&io, &medium) == SYFS_FORMAT_FAT && medium == SYFS_MEDIUM_SMARTMEDIA);
		fs = syfs_open(&io, nullptr, 0);
		CHECK(fs && !strcmp(label_of(fs), "XV-5080") && (names(fs, SYFS_TYPE_DIRECTORY) == std::vector<std::string> { "ROLAND" })
			&& count_of(fs) == 1);
		close_image(fs);
		const uint32_t per_block = mb == 8 ? 16 : 32, blocks = mb == 8 ? 4 : mb == 128 ? 5 : mb == 64 ? 4 : 3;
		uint8_t page[528], cis[4];
		made.get(1024, page, sizeof page);
		memcpy(cis, page, 4);
		// block 0: the information twice in its first page, no address, both halves' guards the same
		CHECK(cis[0] == 0x01 && cis[1] == 0x03 && cis[2] == 0xD9 && !memcmp(page, page + 256, 256) && !page[518] && !page[519]);
		CHECK(page[520] == 0x0C && page[521] == 0xCC && page[522] == 0xC3 && !memcmp(page + 520, page + 525, 3));
		made.get(1024 + 528, page, sizeof page);
		CHECK(std::all_of(page, page + 528, [](uint8_t v) { return v == 0xFF; }));
		// logical block n in block n + 1; the address 00010, the number and a bit making its ones even, twice
		for (uint32_t b = 1; b <= blocks + 1; b++)
			for (uint32_t p : { 0u, per_block - 1 }) {
				made.get(1024 + (uint64_t(b) * per_block + p) * 528, page, sizeof page);
				uint32_t address = 0x1000 | (b - 1) << 1, ones = 0;
				for (int k = 0; k < 16; k++)
					ones += address >> k & 1;
				address |= ones & 1;
				if (b > blocks)
					CHECK(std::all_of(page, page + 528, [](uint8_t v) { return v == 0xFF; }));
				else
					CHECK(page[518] == (address >> 8) && page[519] == (address & 0xFF) && page[523] == page[518] && page[524] == page[519]
						&& std::all_of(page + 512, page + 518, [](uint8_t v) { return v == 0xFF; }));
			}
		// a half of zeros is guarded by ones; the partition's sector's second half, which holds its entry, is not
		made.get(1024 + uint64_t(per_block) * 528, page, sizeof page);
		CHECK(page[525] == 0xFF && page[526] == 0xFF && page[527] == 0xFF && !(page[520] == 0xFF && page[521] == 0xFF));
		// the volume's own account: one cluster in use, the directory's
		Bytes volume(size_t(blocks) * per_block * 512);
		for (size_t s = 0; s < volume.size() / 512; s++)
			made.get(1024 + (uint64_t(per_block) + s) * 528, &volume[s * 512], 512);
		const uint32_t start = uint32_t(volume[454] | volume[455] << 8);
		const uint8_t *boot = &volume[size_t(start) * 512];
		CHECK(volume[510] == 0x55 && !memcmp(boot + 3, "RJA2.00 ", 8) && boot[13] == per_block && boot[16] == 2
			&& !memcmp(boot + 54, mb == 128 ? "FAT16   " : "FAT12   ", 8) && start + (boot[19] | boot[20] << 8 | boot[32] | boot[33] << 8 | boot[34] << 16) == mb * 2000u);
	}
	// without a label the root holds the directory alone; a size or a label it cannot have makes nothing
	{
		Sparse made(1024 + uint64_t(32) * 1048576 / 32 * 33), small(1024 + uint64_t(4) * 1048576 / 32 * 33);
		w = writer_of(made);
		CHECK(syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_SMARTMEDIA, nullptr, err, sizeof err));
		io = io_of(made);
		fs = syfs_open(&io, nullptr, 0);
		CHECK(fs && !*label_of(fs) && count_of(fs) == 1);
		close_image(fs);
		err[0] = 0;
		CHECK(!syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_SMARTMEDIA, "TWELVE CHARS", err, sizeof err) && err[0]);
		CHECK(!syfs_create(&w, SYFS_NONE, SYFS_MEDIUM_SMARTMEDIA, "NAME", err, sizeof err));
		w = writer_of(small);
		CHECK(!syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_SMARTMEDIA, nullptr, err, sizeof err));
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

// ---- a VHD written through: an empty disk made, formatted and written by a host through the image's own io
static size_t zeros_read(void *, uint64_t, void *buf, size_t n)
{
	memset(buf, 0, n);
	return n;
}

static size_t growing_write(void *user, uint64_t offset, const void *buf, size_t n)
{
	auto *s = static_cast<Sparse *>(user);
	s->size = std::max(s->size, offset + n);
	s->put(offset, buf, n);
	return n;
}

static void test_vhd_written()
{
	const uint64_t MB = 1 << 20, SIZE = 40 * MB;
	char err[128] = "";
	// an empty disk: a VHD of zeros, so of no blocks
	Sparse file(0), plain(SIZE);
	syfs_io zeros { nullptr, zeros_read, nullptr, SIZE }, grow { &file, sparse_read, growing_write, 0 };
	const uint64_t empty = syfs_convert(&zeros, &grow, SYFS_CONTAINER_VHD, err, sizeof err);
	CHECK(empty == 512 + 1024 + 512 + 512 && file.size == empty);
	grow.size = file.size;
	syfs_image *fs = syfs_open(&grow, nullptr, 0);
	syfs_io disk {};
	CHECK(fs && syfs_image_io(fs, &disk) && disk.size == SIZE && disk.write && disk.read);
	// zeros written to a block not there add nothing
	uint8_t buf[8192] = {};
	CHECK(disk.write(disk.user, 5 * MB, buf, sizeof buf) == sizeof buf && file.size == empty);
	// formatted through the io: one block for the volume's structures.  A raw image beside it is given the same
	// bytes from here on
	syfs_io raw = writer_of(plain);
	CHECK(syfs_create(&disk, SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, "WRITTEN", err, sizeof err));
	CHECK(file.size == empty + 512 + 2 * MB);
	for (uint64_t at = 0; at < 2 * MB; at += sizeof buf) {
		CHECK(disk.read(disk.user, at, buf, sizeof buf) == sizeof buf);
		plain.put(at, buf, sizeof buf);
	}
	// writes across a block's end, into blocks there and not, and at the disk's end
	for (size_t i = 0; i < sizeof buf; i++)
		buf[i] = uint8_t(i * 7 + 1);
	for (uint64_t at : { 2 * MB - 3000, 17 * MB + 5, 2 * MB - 3000 + 100, SIZE - sizeof buf }) {
		CHECK(disk.write(disk.user, at, buf, sizeof buf) == sizeof buf);
		plain.put(at, buf, sizeof buf);
	}
	// blocks 0 and 1, 8 and 19
	CHECK(file.size == empty + 4 * (512 + 2 * MB));
	// nothing past the disk's end, and nothing short
	CHECK(disk.write(disk.user, SIZE - 100, buf, 200) == 0 && disk.read(disk.user, SIZE - 100, buf, 200) == 100);
	Sparse got(SIZE);
	for (uint64_t at = 0; at < SIZE; at += sizeof buf) {
		CHECK(disk.read(disk.user, at, buf, sizeof buf) == sizeof buf);
		got.put(at, buf, sizeof buf);
	}
	CHECK(got.pages == plain.pages);
	syfs_close(fs);

	// opened again it is the disk written, and comes out as the raw image beside it; only read, it has no writer
	syfs_io again = io_of(file);
	syfs_key medium;
	CHECK(probe(&again, &medium) == SYFS_FORMAT_FAT && medium == SYFS_MEDIUM_DISK);
	fs = syfs_open(&again, nullptr, 0);
	CHECK(fs && !strcmp(label_of(fs), "WRITTEN") && syfs_image_io(fs, &disk) && !disk.write);
	close_image(fs);
	Sparse out(SIZE);
	syfs_io to = writer_of(out);
	CHECK(syfs_convert(&again, &to, SYFS_CONTAINER_RAW, err, sizeof err) == SIZE && out.pages == plain.pages);

	// a raw image's io is its bytes as they lie
	raw = { &plain, sparse_read, sparse_write, SIZE };
	fs = syfs_open(&raw, nullptr, 0);
	CHECK(fs && syfs_image_io(fs, &disk) && disk.size == SIZE);
	const uint8_t mark[3] = { 9, 8, 7 };
	uint8_t back[3];
	CHECK(disk.write(disk.user, 30 * MB, mark, 3) == 3);
	plain.get(30 * MB, back, 3);
	CHECK(!memcmp(mark, back, 3));
	syfs_close(fs);
}

// ---- a VHD two hosts have open at once: what one writes the other reads, and both may add blocks
static int held = 0, holds = 0;

static uint64_t sparse_length(void *user)
{
	return static_cast<Sparse *>(user)->size;
}

static int counted_hold(void *, int on)
{
	held += on ? 1 : -1;
	holds += on;
	return held == (on ? 1 : 0);
}

static void test_vhd_shared()
{
	const uint64_t MB = 1 << 20, SIZE = 24 * MB;
	char err[128] = "";
	Sparse file(0), plain(SIZE);
	syfs_io zeros { nullptr, zeros_read, nullptr, SIZE }, make { &file, sparse_read, growing_write, 0 };
	CHECK(syfs_convert(&zeros, &make, SYFS_CONTAINER_VHD, err, sizeof err));
	// two hosts, each with its own io on the one file, and a third that only reads
	syfs_io shared { &file, sparse_read, growing_write, file.size, sparse_length, counted_hold };
	syfs_io reader { &file, sparse_read, nullptr, file.size, sparse_length, nullptr };
	syfs_image *a = syfs_open(&shared, nullptr, 0), *b = syfs_open(&shared, nullptr, 0), *c = syfs_open(&reader, nullptr, 0);
	syfs_io da {}, db {}, dc {};
	CHECK(a && b && c && syfs_image_io(a, &da) && syfs_image_io(b, &db) && syfs_image_io(c, &dc) && !dc.write);
	uint8_t buf[4096], got[4096];
	auto fill = [&](uint8_t seed) {
		for (size_t i = 0; i < sizeof buf; i++)
			buf[i] = uint8_t(i * 3 + seed);
	};
	auto same = [&](const syfs_io &d, uint64_t at) {
		plain.get(at, buf, sizeof buf);
		return d.read(d.user, at, got, sizeof got) == sizeof got && !memcmp(got, buf, sizeof buf);
	};
	// every host has read the blocks as absent before any is written
	for (const syfs_io *d : { &da, &db, &dc })
		for (uint64_t at : { 3 * MB, 9 * MB, 15 * MB })
			CHECK(same(*d, at));
	// one adds a block: the others read it, and write into it without adding it again
	fill(1);
	CHECK(da.write(da.user, 3 * MB, buf, sizeof buf) == sizeof buf);
	plain.put(3 * MB, buf, sizeof buf);
	CHECK(same(db, 3 * MB) && same(dc, 3 * MB));
	const uint64_t one = file.size;
	fill(2);
	CHECK(db.write(db.user, 3 * MB + 5000, buf, sizeof buf) == sizeof buf);
	plain.put(3 * MB + 5000, buf, sizeof buf);
	CHECK(file.size == one && same(da, 3 * MB + 5000) && same(da, 3 * MB));
	// the other adds one, after the first's: neither is written over
	fill(3);
	CHECK(db.write(db.user, 9 * MB, buf, sizeof buf) == sizeof buf);
	plain.put(9 * MB, buf, sizeof buf);
	fill(4);
	CHECK(da.write(da.user, 15 * MB, buf, sizeof buf) == sizeof buf);
	plain.put(15 * MB, buf, sizeof buf);
	CHECK(file.size == one + 2 * (512 + 2 * MB) && holds == 3 && held == 0);
	for (const syfs_io *d : { &da, &db, &dc })
		for (uint64_t at : { 3 * MB, 3 * MB + 5000, 9 * MB, 15 * MB, 20 * MB })
			CHECK(same(*d, at));
	syfs_close(a), syfs_close(b), syfs_close(c);
	// opened while another adds a block, the file's end is not where its size as counted put the footer: it is
	// still the disk, by the footer's copy at its start
	{
		Sparse grown = file;
		const uint8_t junk[512] = { 1, 2, 3 };
		grown.put(grown.size - 512, junk, 512);
		syfs_io late { &grown, sparse_read, growing_write, grown.size, sparse_length, counted_hold };
		syfs_image *d = syfs_open(&late, nullptr, 0);
		syfs_io dd {};
		CHECK(d && num(d, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_VHD && syfs_image_io(d, &dd) && dd.size == SIZE);
		CHECK(same(dd, 9 * MB));
		syfs_close(d);
	}
	// and the file is a VHD of the disk both wrote
	Sparse out(SIZE);
	syfs_io again = io_of(file), to = writer_of(out);
	CHECK(syfs_convert(&again, &to, SYFS_CONTAINER_RAW, err, sizeof err) == SIZE && out.pages == plain.pages);
}

// ---- a FAT volume written: files and directories made, named, moved and removed, and read back from the image
static Bytes bytes_of(size_t n, uint8_t seed)
{
	Bytes b(n);
	for (size_t i = 0; i < n; i++)
		b[i] = uint8_t(i * 13 + seed + (i >> 8));
	return b;
}

// a node by its path from the top, names parted by '/'
static syfs_node at_path(syfs_image *fs, const std::string &path)
{
	syfs_node n = SYFS_ROOT;
	for (size_t from = 0; from < path.size();) {
		size_t to = path.find('/', from);
		if (to == std::string::npos)
			to = path.size();
		const std::string name = path.substr(from, to - from);
		std::vector<syfs_node> kids(syfs_list(fs, n, SYFS_REL_CHILDREN, nullptr, 0));
		syfs_list(fs, n, SYFS_REL_CHILDREN, kids.data(), kids.size());
		syfs_node found = SYFS_NO_NODE;
		for (syfs_node k : kids) {
			syfs_info info;
			if (syfs_node_info(fs, k, &info) && name == info.name)
				found = k;
		}
		if (found == SYFS_NO_NODE)
			return found;
		n = found;
		from = to + 1;
	}
	return n;
}

static Bytes contents(syfs_image *fs, syfs_node n)
{
	Bytes b(syfs_read(fs, n, SYFS_AS_RAW, 0, nullptr, 0));
	b.resize(syfs_read(fs, n, SYFS_AS_RAW, 0, b.data(), b.size()));
	return b;
}

// every path of the image with its size, a directory's ending in '/', in order of name
static std::vector<std::string> tree(syfs_image *fs, syfs_node n = SYFS_ROOT, const std::string &prefix = "")
{
	std::vector<std::string> out;
	std::vector<syfs_node> kids(syfs_list(fs, n, SYFS_REL_CHILDREN, nullptr, 0));
	syfs_list(fs, n, SYFS_REL_CHILDREN, kids.data(), kids.size());
	for (syfs_node k : kids) {
		syfs_info info;
		syfs_node_info(fs, k, &info);
		if (info.type == SYFS_TYPE_DIRECTORY) {
			out.push_back(prefix + info.name + "/");
			auto sub = tree(fs, k, prefix + info.name + "/");
			out.insert(out.end(), sub.begin(), sub.end());
		} else
			out.push_back(prefix + info.name + " " + std::to_string(info.size));
	}
	std::sort(out.begin(), out.end());
	return out;
}

static syfs_node put_file(syfs_image *fs, syfs_node parent, const char *name, Bytes &b)
{
	syfs_io from = io_of(b);
	return syfs_import(fs, parent, SYFS_TYPE_FILE, SYFS_AS_RAW, name, &from);
}

// the FAT's own account checked against the directories': every cluster of a chain marked in use once, no other
static void check_fat_whole(Sparse &img, uint64_t base)
{
	syfs_io io = io_of(img);
	syfs::Image im(io);
	uint8_t bs[512];
	im.read(base, bs, sizeof bs);
	auto g = syfs::fat::geometry(bs, base, im.size());
	CHECK(g.has_value());
	if (!g)
		return;
	auto entry = [&](uint64_t fat, uint32_t c) -> uint32_t {
		uint8_t b[4] = {};
		const uint64_t at = base + g->fat_at + fat * g->fat_bytes;
		if (g->bits == 12) {
			im.read(at + c + c / 2, b, 2);
			const uint32_t v = b[0] | b[1] << 8;
			return c & 1 ? v >> 4 : v & 0xFFF;
		}
		im.read(at + uint64_t(c) * (g->bits / 8), b, g->bits / 8);
		return (b[0] | b[1] << 8 | b[2] << 16 | uint32_t(b[3]) << 24) & 0x0FFFFFFF;
	};
	const uint64_t fats = (g->root_at - g->fat_at) / g->fat_bytes;
	const uint32_t end = g->bits == 12 ? 0xFF8 : g->bits == 16 ? 0xFFF8 : 0x0FFFFFF8;
	std::vector<uint8_t> used(g->clusters + 2, 0);
	bool ok = true;
	// a directory's entries, each chain followed to its end; a file's is as long as its size asks
	std::function<void(uint32_t, bool, int)> walk = [&](uint32_t first, bool fixed, int depth) {
		Bytes dir;
		if (fixed) {
			dir.resize(size_t(g->root_entries) * 32);
			im.read(base + g->root_at, dir.data(), dir.size());
		} else
			for (uint32_t c = first; c >= 2 && c < end && dir.size() < (1u << 22); c = entry(0, c)) {
				dir.resize(dir.size() + g->cluster);
				im.read(base + g->data_at + uint64_t(c - 2) * g->cluster, &dir[dir.size() - g->cluster], g->cluster);
			}
		for (size_t at = 0; at + 32 <= dir.size() && dir[at]; at += 32) {
			const uint8_t *e = &dir[at];
			if (e[0] == 0xE5 || (e[11] & 0x3F) == 0x0F || (e[11] & 0x08) || e[0] == '.')
				continue;
			const uint32_t start = e[26] | e[27] << 8 | (g->bits == 32 ? uint32_t(e[20] | e[21] << 8) << 16 : 0);
			const uint32_t size = e[28] | e[29] << 8 | e[30] << 16 | uint32_t(e[31]) << 24;
			uint64_t count = 0;
			for (uint32_t c = start; c >= 2 && c < end; c = entry(0, c)) {
				ok = ok && c < g->clusters + 2 && !used[c]++;
				if (!ok)
					return;
				count++;
			}
			if (e[11] & 0x10) {
				ok = ok && count >= 1 && depth < 16;
				if (ok)
					walk(start, false, depth + 1);
			} else
				ok = ok && count == (uint64_t(size) + g->cluster - 1) / g->cluster;
		}
	};
	if (g->bits == 32) {
		for (uint32_t c = g->root_cluster; c >= 2 && c < end; c = entry(0, c))
			used[c]++;
		walk(g->root_cluster, false, 0);
	} else
		walk(0, true, 0);
	CHECK(ok);
	// and every copy of the FAT says the same, in use exactly where a chain goes
	uint32_t wrong = 0;
	for (uint32_t c = 2; c < g->clusters + 2; c++) {
		wrong += (entry(0, c) != 0) != (used[c] == 1);
		for (uint64_t f = 1; f < fats; f++)
			wrong += entry(f, c) != entry(0, c);
	}
	CHECK(wrong == 0);
}

static void test_fat_written()
{
	const uint64_t MB = 1 << 20;
	char err[128];
	struct Volume { uint64_t bytes; syfs_key medium; uint64_t base; };
	for (Volume v : { Volume { 1440 * 1024, SYFS_MEDIUM_FLOPPY, 0 }, Volume { 40 * MB, SYFS_MEDIUM_DISK, 63 * 512 },
		     Volume { 600 * MB, SYFS_MEDIUM_DISK, 63 * 512 } }) {
		Sparse img(v.bytes);
		syfs_io rw { &img, sparse_read, sparse_write, img.size };
		CHECK(syfs_create(&rw, SYFS_FORMAT_FAT, v.medium, "WRITTEN", err, sizeof err));
		syfs_image *fs = syfs_open(&rw, nullptr, 0);
		CHECK(fs && count_of(fs) == 0);
		// files: a short name as it stands, a long one, one that is not ASCII, an empty one, one of many clusters
		Bytes a = bytes_of(700, 1), b = bytes_of(100000, 2), c = bytes_of(5000, 3), none;
		const syfs_node fa = put_file(fs, SYFS_ROOT, "SHORT.BIN", a);
		const syfs_node fb = put_file(fs, SYFS_ROOT, "A longer name, with a comma.wav", b);
		const syfs_node fc = put_file(fs, SYFS_ROOT, "Ljud \xC3\xA5\xC3\xA4\xC3\xB6.bin", c);
		const syfs_node fe = put_file(fs, SYFS_ROOT, "empty", none);
		CHECK(fa != SYFS_NO_NODE && fb != SYFS_NO_NODE && fc != SYFS_NO_NODE && fe != SYFS_NO_NODE && !*syfs_error(fs));
		CHECK(contents(fs, fa) == a && contents(fs, fb) == b && contents(fs, fc) == c && contents(fs, fe).empty());
		// what the volume cannot take: a name twice (whatever its case), a name of no characters or of ones it
		// cannot hold, a parent that is a file
		CHECK(put_file(fs, SYFS_ROOT, "short.bin", a) == SYFS_NO_NODE && *syfs_error(fs));
		CHECK(put_file(fs, SYFS_ROOT, "", a) == SYFS_NO_NODE && put_file(fs, SYFS_ROOT, "a/b", a) == SYFS_NO_NODE);
		CHECK(put_file(fs, fa, "IN.BIN", a) == SYFS_NO_NODE && *syfs_error(fs));
		// directories, one in another, and files in them: enough in one that it needs more than a cluster
		const syfs_node d1 = syfs_import(fs, SYFS_ROOT, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, "ROLAND", nullptr);
		const syfs_node d2 = syfs_import(fs, d1, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, "Sound files", nullptr);
		CHECK(d1 != SYFS_NO_NODE && d2 != SYFS_NO_NODE);
		for (int k = 0; k < 150; k++) {
			Bytes small = bytes_of(size_t(k) * 7, uint8_t(k));
			const std::string name = "A file with a long name, number " + std::to_string(k) + ".dat";
			CHECK(put_file(fs, d2, name.c_str(), small) != SYFS_NO_NODE);
		}
		CHECK(syfs_list(fs, d2, SYFS_REL_CHILDREN, nullptr, 0) == 150 && syfs_list(fs, d1, SYFS_REL_ALL, nullptr, 0) == 151);
		// named anew, moved, removed: the nodes keep their numbers
		CHECK(syfs_set_str(fs, fb, SYFS_PROP_NAME, "RENAMED.WAV") && syfs_set_str(fs, fa, SYFS_PROP_NAME, "Now a long name.bin"));
		CHECK(!syfs_set_str(fs, fc, SYFS_PROP_NAME, "renamed.wav") && *syfs_error(fs));
		CHECK(syfs_move(fs, fc, d1) && syfs_move(fs, fb, d2) && !syfs_move(fs, d1, d2) && *syfs_error(fs));
		CHECK(syfs_move(fs, d2, SYFS_ROOT) && at_path(fs, "Sound files/RENAMED.WAV") == fb);
		CHECK(at_path(fs, "ROLAND/Ljud \xC3\xA5\xC3\xA4\xC3\xB6.bin") == fc && contents(fs, fc) == c && contents(fs, fb) == b);
		for (int k = 0; k < 150; k += 2)
			CHECK(syfs_remove(fs, at_path(fs, "Sound files/A file with a long name, number " + std::to_string(k) + ".dat")));
		syfs_info gone;
		CHECK(syfs_remove(fs, fe) && !syfs_node_info(fs, fe, &gone) && !syfs_remove(fs, fe));
		// a removed file's room is used again
		Bytes big = bytes_of(300000, 9);
		const syfs_node fg = put_file(fs, d1, "BIG.BIN", big);
		CHECK(fg != SYFS_NO_NODE && contents(fs, fg) == big && contents(fs, fa) == a);
		const auto before = tree(fs);
		syfs_close(fs);

		// read from the image again it is the same tree, the files' bytes theirs, the FAT whole
		check_fat_whole(img, v.base);
		syfs_io io = io_of(img);
		fs = syfs_open(&io, nullptr, 0);
		CHECK(fs && tree(fs) == before);
		CHECK(before.size() == 6 + 75);
		CHECK(!strcmp(label_of(fs), "WRITTEN"));
		CHECK(contents(fs, at_path(fs, "Now a long name.bin")) == a && contents(fs, at_path(fs, "Sound files/RENAMED.WAV")) == b);
		CHECK(contents(fs, at_path(fs, "ROLAND/BIG.BIN")) == big);
		CHECK(contents(fs, at_path(fs, "Sound files/A file with a long name, number 149.dat")) == bytes_of(149 * 7, 149));
		// opened without a writer nothing is changed
		CHECK(!syfs_remove(fs, at_path(fs, "ROLAND")) && *syfs_error(fs) && put_file(fs, SYFS_ROOT, "NO.BIN", a) == SYFS_NO_NODE);
		close_image(fs);

		// a directory removed goes with all it holds, and a volume emptied has every cluster free again
		fs = syfs_open(&rw, nullptr, 0);
		CHECK(syfs_remove(fs, at_path(fs, "Sound files")) && syfs_remove(fs, at_path(fs, "ROLAND"))
			&& syfs_remove(fs, at_path(fs, "Now a long name.bin")) && tree(fs).empty());
		syfs_close(fs);
		check_fat_whole(img, v.base);

		// a volume filled refuses the file that does not fit, and is left whole
		fs = syfs_open(&rw, nullptr, 0);
		Bytes chunk = bytes_of(size_t(v.bytes > 2 * MB ? 16 * MB : 600 * 1024), 5);
		int fitted = 0;
		if (v.bytes <= 40 * MB)
			for (; fitted < 10 && put_file(fs, SYFS_ROOT, ("FILL" + std::to_string(fitted) + ".BIN").c_str(), chunk) != SYFS_NO_NODE; fitted++)
				;
		CHECK(v.bytes > 40 * MB || (fitted == 2 && *syfs_error(fs) && contents(fs, at_path(fs, "FILL1.BIN")) == chunk));
		syfs_close(fs);
		check_fat_whole(img, v.base);
	}

	// a FAT12 volume's root is of a fixed size: when it is full, it is full
	{
		Sparse img(1440 * 1024);
		syfs_io rw { &img, sparse_read, sparse_write, img.size };
		CHECK(syfs_create(&rw, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, nullptr, err, sizeof err));
		syfs_image *fs = syfs_open(&rw, nullptr, 0);
		Bytes one = bytes_of(1, 1);
		int made = 0;
		while (made < 400 && put_file(fs, SYFS_ROOT, ("F" + std::to_string(made) + ".X").c_str(), one) != SYFS_NO_NODE)
			made++;
		CHECK(made == 224 && *syfs_error(fs));
		syfs_close(fs);
		check_fat_whole(img, 0);
	}

	// from one image into another: a directory with what it holds, a file; from a CD-ROM as from a FAT volume
	{
		Sparse src(4 * MB), dst(40 * MB);
		syfs_io rs { &src, sparse_read, sparse_write, src.size }, rd { &dst, sparse_read, sparse_write, dst.size };
		CHECK(syfs_create(&rs, SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, nullptr, err, sizeof err) == 0);
		Sparse flop(1440 * 1024);
		rs = { &flop, sparse_read, sparse_write, flop.size };
		CHECK(syfs_create(&rs, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, nullptr, err, sizeof err));
		CHECK(syfs_create(&rd, SYFS_FORMAT_FAT, SYFS_MEDIUM_DISK, nullptr, err, sizeof err));
		syfs_image *from = syfs_open(&rs, nullptr, 0), *to = syfs_open(&rd, nullptr, 0);
		Bytes a = bytes_of(9000, 4), b = bytes_of(123, 6);
		const syfs_node dir = syfs_import(from, SYFS_ROOT, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, "Set", nullptr);
		const syfs_node sub = syfs_import(from, dir, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, "Inner", nullptr);
		put_file(from, dir, "one.bin", a);
		put_file(from, sub, "two.bin", b);
		const syfs_node top = put_file(from, SYFS_ROOT, "TOP.BIN", b);
		const syfs_node copied = syfs_transfer(to, SYFS_ROOT, from, dir);
		CHECK(copied != SYFS_NO_NODE && syfs_transfer(to, copied, from, top) != SYFS_NO_NODE);
		CHECK((tree(to) == std::vector<std::string> { "Set/", "Set/Inner/", "Set/Inner/two.bin 123", "Set/TOP.BIN 123", "Set/one.bin 9000" }));
		CHECK(contents(to, at_path(to, "Set/one.bin")) == a && contents(to, at_path(to, "Set/Inner/two.bin")) == b);
		// a name already there is not written over, and an image's own node is not transferred into it
		CHECK(syfs_transfer(to, SYFS_ROOT, from, dir) == SYFS_NO_NODE && *syfs_error(to));
		CHECK(syfs_transfer(to, SYFS_ROOT, to, copied) == SYFS_NO_NODE);
		syfs_close(from), syfs_close(to);
		check_fat_whole(dst, 63 * 512);
	}
}

// ---- an S-700 disk's entries taken into another: a volume with all it uses, what is there already not made again
static void test_s700_taken()
{
	char err[128];
	Bytes src = s700_image(" SYS-772 HardDisk Sys Ver. 2.25");
	syfs_io from_io = io_of(src);
	syfs_image *from = syfs_open(&from_io, nullptr, 0);
	CHECK(from != nullptr);
	for (syfs_key format : { SYFS_FORMAT_S760, SYFS_FORMAT_S770 }) {
		Sparse disk(16 << 20);
		syfs_io rw { &disk, sparse_read, sparse_write, disk.size };
		CHECK(syfs_create(&rw, format, SYFS_MEDIUM_DISK, nullptr, err, sizeof err));
		syfs_image *to = syfs_open(&rw, nullptr, 0);
		CHECK(to && count_of(to) == 0);
		// the patch first: with it its partials and their samples
		const syfs_node patch = syfs_transfer(to, SYFS_ROOT, from, syfs_node(find(from, "STR:Strings") + 1));
		CHECK(patch != SYFS_NO_NODE && !*syfs_error(to));
		listed.erase(to);
		CHECK((names(to, SYFS_TYPE_PATCH) == std::vector<std::string> { "STR:Strings" }));
		CHECK((names(to, SYFS_TYPE_PARTIAL) == std::vector<std::string> { "PRT:High", "PRT:Low" }));
		CHECK((names(to, SYFS_TYPE_SAMPLE) == std::vector<std::string> { "SMP:High", "SMP:Low" }));
		CHECK(names(to, SYFS_TYPE_VOLUME).empty() && names(to, SYFS_TYPE_PERFORMANCE).empty());
		// then the volume: its performance is made, the patch and what it uses are the ones there
		const syfs_node volume = syfs_transfer(to, SYFS_ROOT, from, syfs_node(find(from, "VOL:One") + 1));
		CHECK(volume != SYFS_NO_NODE);
		listed.erase(to);
		CHECK(count_of(to) == 7 && find(to, "STR:Strings") + 1 == patch);
		// and either again makes nothing
		CHECK(syfs_transfer(to, SYFS_ROOT, from, syfs_node(find(from, "VOL:One") + 1)) == volume);
		CHECK(syfs_transfer(to, SYFS_ROOT, from, syfs_node(find(from, "SMP:Low") + 1)) == syfs_node(find(to, "SMP:Low") + 1));
		listed.erase(to);
		CHECK(count_of(to) == 7);
		// each uses here what it uses there, and each sample's wave and header are the other's
		for (const char *name : { "VOL:One", "PRF:One", "STR:Strings", "PRT:Low", "PRT:High" })
			CHECK(used(to, find(to, name)) == used(from, find(from, name)));
		for (const char *name : { "SMP:Low", "SMP:High" }) {
			syfs::Sample a, b;
			CHECK(sample_info(from, find(from, name), &a) && sample_info(to, find(to, name), &b));
			CHECK(a.rate == b.rate && a.frames == b.frames && a.key == b.key && a.loop_start == b.loop_start && a.mode == b.mode);
			CHECK(wave(to, find(to, name), 0, size_t(b.frames)) == wave(from, find(from, name), 0, size_t(a.frames)));
		}
		for (int key : { 21, 22, 60, 108 }) {
			const size_t a = sample_at(from, find(from, "STR:Strings"), key), b = sample_at(to, find(to, "STR:Strings"), key);
			CHECK((a == NONE) == (b == NONE) && (a == NONE || !strcmp(entry_at(from, a)->name, entry_at(to, b)->name)));
		}
		// nothing is held under an entry, and an image opened without a writer takes nothing
		CHECK(syfs_transfer(to, patch, from, syfs_node(find(from, "SMP:Low") + 1)) == SYFS_NO_NODE && *syfs_error(to));
		syfs_prop prop;
		CHECK(syfs_prop_at(to, patch, 0, &prop) && prop.key == SYFS_PROP_NAME && !prop.writable);
		close_image(to);

		// on the disk: each list one chain from slot 0 in the order made, each entry its list's type, its place and
		// the segments it sounds; the samples' segments chained in the table and counted off the free ones
		syfs_io io = io_of(disk);
		syfs::Image im(io);
		uint8_t s0[512], e[32], table[512];
		im.read(0, s0, sizeof s0);
		const uint16_t expect[5] = { 1, 1, 1, 2, 2 };
		const uint32_t dirs[5] = { 0x504, 0x50C, 0x52C, 0x56C, 0x66C };
		const uint16_t sizes[5][2] = { { 3, 0 }, { 3, 0 }, { 3, 0 }, { 1, 2 }, { 1, 2 } };
		for (int l = 0; l < 5; l++) {
			CHECK(s0[0x114 + 2 * l] == expect[l] && !s0[0x115 + 2 * l]);
			for (uint16_t n = 0; n < expect[l]; n++) {
				im.read(uint64_t(dirs[l]) * 512 + n * 32, e, sizeof e);
				const uint16_t next = uint16_t(e[18] | e[19] << 8), prev = uint16_t(e[20] | e[21] << 8);
				CHECK(e[16] == 0x40 + l && !e[17] && e[22] == n && !e[23] && (e[30] | e[31] << 8) == sizes[l][n]);
				CHECK(next == (n + 1 < expect[l] ? 0x8000 | (n + 1) : 0xFFFF) && prev == (n ? 0x8000 | (n - 1) : 0xFFFF));
			}
		}
		im.read(0x404 * 512, table, sizeof table);
		const uint32_t first = format == SYFS_FORMAT_S760 ? 2 + 114 : 2;
		auto entry = [&](uint32_t n) { return uint32_t(table[2 * n] | table[2 * n + 1] << 8); };
		CHECK(entry(first) == 0xFFF8 && entry(first + 1) == first + 2 && entry(first + 2) == 0xFFF8 && entry(first + 3) == 0);
		im.read(uint64_t(0x66C) * 512, e, sizeof e);
		CHECK((e[28] | e[29] << 8) == int(first));
		syfs_image *again = syfs_open(&io, nullptr, 0);
		CHECK(again && count_of(again) == 7 && format_of(again) == format);
		close_image(again);
	}
	// an S-700 disk takes only another's entries: a file is refused, and a FAT volume takes none of its entries
	{
		Sparse disk(16 << 20), fat(4 << 20);
		syfs_io rw { &disk, sparse_read, sparse_write, disk.size }, rf { &fat, sparse_read, sparse_write, fat.size };
		CHECK(syfs_create(&rw, SYFS_FORMAT_S770, SYFS_MEDIUM_DISK, nullptr, err, sizeof err));
		Sparse flop(1440 * 1024);
		rf = { &flop, sparse_read, sparse_write, flop.size };
		CHECK(syfs_create(&rf, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, nullptr, err, sizeof err));
		syfs_image *to = syfs_open(&rw, nullptr, 0), *files = syfs_open(&rf, nullptr, 0);
		Bytes b = bytes_of(100, 1);
		const syfs_node file = put_file(files, SYFS_ROOT, "A.BIN", b);
		CHECK(syfs_transfer(to, SYFS_ROOT, files, file) == SYFS_NO_NODE && *syfs_error(to));
		CHECK(put_file(to, SYFS_ROOT, "A.BIN", b) == SYFS_NO_NODE && *syfs_error(to));
		CHECK(syfs_transfer(files, SYFS_ROOT, from, syfs_node(find(from, "STR:Strings") + 1)) == SYFS_NO_NODE && *syfs_error(files));
		syfs_close(to), syfs_close(files);
	}
	close_image(from);
}

// ---- DATA cards: a JV-80's, its names as they are; a JV-1080's, its card's name and its names packed, one half or two
static void test_data_cards()
{
	auto pack = [](Bytes &b, size_t at, const char *name) {
		for (int i = 0; i < 12; i++)
			for (int bit = 0; bit < 7; bit++)
				if ((i < int(strlen(name)) ? name[i] : ' ') >> bit & 1)
					b[at + size_t(7 * i + bit) / 8] |= uint8_t(1 << (7 * i + bit) % 8);
	};
	Bytes jv80(0x8000, 0);
	memcpy(&jv80[0], "Roland JV-80D\xBBP\xAF", 16);
	memcpy(&jv80[0x10], "Use M-256E only!", 16);
	put_name(jv80, 0x20, "Syn Lead", 12);
	put_name(jv80, 0x20 + 15 * 204, "Last Perf", 12);
	put_name(jv80, 0xCE0, "JV Strings", 12);
	put_name(jv80, 0xCE0 + 63 * 362, "Last Patch", 12);
	syfs_io io = io_of(jv80);
	syfs_key medium;
	CHECK(probe(&io, &medium) == SYFS_FORMAT_JV80 && medium == SYFS_MEDIUM_CARD && not_fat(jv80));
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && !*label_of(fs) && count_of(fs) == 16 + 64 + 1);
	auto perfs = names(fs, SYFS_TYPE_PERFORMANCE), patches = names(fs, SYFS_TYPE_PATCH);
	CHECK(perfs.size() == 16 && perfs[0] == "Syn Lead" && perfs[15] == "Last Perf" && patches.size() == 64
		&& patches[0] == "JV Strings" && patches[63] == "Last Patch" && names(fs, SYFS_TYPE_RHYTHM_SET).size() == 1);
	CHECK(entry_at(fs, find(fs, "Last Patch"))->number == 63 && entry_at(fs, find(fs, "Last Patch"))->bytes == 362);
	close_image(fs);

	Bytes jv(0x10000, 0);
	memcpy(&jv[0], "RolandJV-990D\xBBP\xAF", 16);
	memcpy(&jv[0x10], "My Card         ", 16);
	pack(jv, 0x20, "Tekno Loop 1"), pack(jv, 0xF70, "Symphonique"), pack(jv, 0xF70 + 63 * 401, "Tortured"), pack(jv, 0x73B0, "HouseDrumSet");
	pack(jv, 0x8020, "Kicks Attack"), pack(jv, 0x8F70, "Bass Marimba"), pack(jv, 0xF3B0, "JazzDrumSet1");
	io = io_of(jv);
	// its upper half without its key holds nothing
	CHECK(probe(&io, &medium) == SYFS_FORMAT_JV1080 && medium == SYFS_MEDIUM_CARD);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && !strcmp(label_of(fs), "My Card") && count_of(fs) == 16 + 64 + 1 && find(fs, "Bass Marimba") == NONE);
	close_image(fs);
	jv[0x8000] = 0xAF;
	memcpy(&jv[0x8001], "RolandJV-990D", 13);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && count_of(fs) == 32 + 128 + 2);
	CHECK(entry_at(fs, find(fs, "Tekno Loop 1"))->number == 0 && entry_at(fs, find(fs, "Kicks Attack"))->number == 16);
	CHECK(entry_at(fs, find(fs, "Tortured"))->number == 63 && entry_at(fs, find(fs, "Bass Marimba"))->number == 64);
	CHECK((names(fs, SYFS_TYPE_RHYTHM_SET) == std::vector<std::string> { "HouseDrumSet", "JazzDrumSet1" }));
	close_image(fs);
	// a 32 KB card is the lower half alone; another size, or another key, is no card
	jv.resize(0x8000);
	io = io_of(jv);
	fs = syfs_open(&io, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_FORMAT_JV1080 && count_of(fs) == 16 + 64 + 1);
	close_image(fs);
	jv[12] = 'N';
	CHECK(probe(&io, &medium) == SYFS_NONE);
	jv[12] = 'D';
	jv.resize(0x9000);
	io = io_of(jv);
	CHECK(probe(&io, &medium) == SYFS_NONE);
}

// ---- a cue sheet: the disc's tracks out of the files it names, the data track's volume read through
static void write_file(const std::string &path, const void *data, size_t n)
{
	FILE *f = fopen(path.c_str(), "wb");
	CHECK(f && fwrite(data, 1, n, f) == n);
	if (f)
		fclose(f);
}

// a track's sectors of audio: each value its place in the track, plus a base
static Bytes audio_of(uint32_t sectors, int base)
{
	Bytes b(size_t(sectors) * 2352);
	for (size_t k = 0; k < b.size() / 2; k++) {
		const uint16_t v = uint16_t(base + int(k));
		b[2 * k] = uint8_t(v);
		b[2 * k + 1] = uint8_t(v >> 8);
	}
	return b;
}

static void test_cue()
{
	char dir[] = "/tmp/synthfs_cue_XXXXXX";
	if (!mkdtemp(dir)) {
		CHECK(!"a folder for the sheets");
		return;
	}
	const std::string d(dir);
	char err[200] = "";

	// an audio disc in one file: three tracks, the second with two seconds of silence the file does not hold and
	// pre-emphasis, the third with a gap the file holds
	Bytes bin = audio_of(10, 0);
	const Bytes two = audio_of(20, 1000), gap = audio_of(5, 2000), three = audio_of(8, 3000);
	bin.insert(bin.end(), two.begin(), two.end());
	bin.insert(bin.end(), gap.begin(), gap.end());
	bin.insert(bin.end(), three.begin(), three.end());
	write_file(d + "/audio.bin", bin.data(), bin.size());
	const std::string sheet = "REM a disc\r\nTITLE \"Three Tracks\"\r\nFILE \"C:\\rips\\audio.bin\" BINARY\r\n"
		"  TRACK 01 AUDIO\r\n    TITLE \"First One\"\r\n    INDEX 01 00:00:00\r\n"
		"  TRACK 02 AUDIO\r\n    FLAGS DCP PRE\r\n    PREGAP 00:02:00\r\n    INDEX 01 00:00:10\r\n"
		"  TRACK 03 AUDIO\r\n    INDEX 00 00:00:30\r\n    INDEX 01 00:00:35\r\n";
	write_file(d + "/audio.cue", sheet.data(), sheet.size());
	syfs_image *fs = syfs_open_path((d + "/audio.cue").c_str(), err, sizeof err);
	CHECK(fs);
	if (fs) {
		CHECK(format_of(fs) == SYFS_FORMAT_CDDA && medium_of(fs) == SYFS_MEDIUM_DISK);
		CHECK(num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_CUE);
		CHECK(!strcmp(label_of(fs), "Three Tracks") && num(fs, SYFS_ROOT, SYFS_PROP_SECTORS) == 193);
		CHECK(count_of(fs) == 3);
		const uint32_t start[3] = { 0, 160, 185 }, sectors[3] = { 160, 25, 8 }, pregap[3] = { 0, 150, 5 };
		for (syfs_node n = 1; n <= 3 && count_of(fs) == 3; n++) {
			const syfs::Entry *e = entry_at(fs, n - 1);
			CHECK(e->kind == SYFS_TYPE_TRACK && e->format == SYFS_FORMAT_CDDA && e->number == n - 1 && e->parent == NONE);
			CHECK(!strcmp(e->name, n == 1 ? "First One" : n == 2 ? "Track 02" : "Track 03"));
			CHECK(num(fs, n, SYFS_PROP_SECTOR, -1) == start[n - 1] && num(fs, n, SYFS_PROP_SECTORS, -1) == sectors[n - 1]);
			CHECK(num(fs, n, SYFS_PROP_PREGAP, -1) == pregap[n - 1] && num(fs, n, SYFS_PROP_EMPHASIS, -1) == (n == 2));
			CHECK(num(fs, n, SYFS_PROP_RATE) == 44100 && num(fs, n, SYFS_PROP_CHANNELS) == 2);
			CHECK(num(fs, n, SYFS_PROP_FRAMES) == int64_t(sectors[n - 1]) * 588 && e->bytes == uint64_t(sectors[n - 1]) * 2352);
			char exports[64] = "";
			CHECK(syfs_get_str(fs, n, SYFS_PROP_EXPORTS, exports, sizeof exports) && !strcmp(exports, "raw wav s16"));
		}
		// the first track runs on into the second's silence; the second's values are its file's, and it runs on
		// into the third's gap
		int16_t pcm[8];
		CHECK(syfs_read(fs, 1, SYFS_AS_S16, 0, pcm, sizeof pcm) == sizeof pcm && pcm[0] == 0 && pcm[7] == 7);
		CHECK(syfs_read(fs, 1, SYFS_AS_S16, uint64_t(10) * 2352 - 4, pcm, sizeof pcm) == sizeof pcm);
		CHECK(pcm[0] == 10 * 1176 - 2 && pcm[1] == 10 * 1176 - 1 && pcm[2] == 0 && pcm[7] == 0);
		CHECK(syfs_read(fs, 2, SYFS_AS_S16, 0, pcm, sizeof pcm) == sizeof pcm && pcm[0] == 1000 && pcm[7] == 1007);
		CHECK(syfs_read(fs, 2, SYFS_AS_S16, uint64_t(20) * 2352, pcm, sizeof pcm) == sizeof pcm && pcm[0] == 2000);
		uint8_t raw[4];
		CHECK(syfs_read(fs, 3, SYFS_AS_RAW, 2, raw, sizeof raw) == sizeof raw && syfs::le16(raw) == 3001 && syfs::le16(raw + 2) == 3002);
		CHECK(syfs_read(fs, 3, SYFS_AS_S16, uint64_t(8) * 2352 - 4, pcm, sizeof pcm) == 4);
		close_image(fs);
	}

	// a sampler's CD-ROM with audio after it, a file each: the volume as from its own image, the tracks after its
	// entries; the audio kept big-endian
	const Bytes s760 = s700_image(" S-760 System Disk    Ver. 2.24"), data = raw_of(s760);
	Bytes big = audio_of(12, 500);
	for (size_t k = 0; k + 1 < big.size(); k += 2)
		std::swap(big[k], big[k + 1]);
	write_file(d + "/data.bin", data.data(), data.size());
	write_file(d + "/two.raw", big.data(), big.size());
	const std::string mixed = "FILE \"data.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
		"FILE \"two.raw\" MOTOROLA\n  TRACK 02 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 00:00:00\n";
	write_file(d + "/mixed.cue", mixed.data(), mixed.size());
	Bytes plain = s760;
	syfs_io io = io_of(plain);
	syfs_image *alone = syfs_open(&io, nullptr, 0);
	fs = syfs_open_path((d + "/mixed.cue").c_str(), err, sizeof err);
	CHECK(fs && alone);
	if (fs && alone) {
		const size_t held = count_of(alone), sectors = data.size() / 2352;
		CHECK(format_of(fs) == SYFS_FORMAT_S760 && num(fs, SYFS_ROOT, SYFS_PROP_CONTAINER) == SYFS_CONTAINER_CUE);
		CHECK(!strcmp(label_of(fs), label_of(alone)) && held > 0 && count_of(fs) == held + 2);
		for (size_t i = 0; i < held && count_of(fs) == held + 2; i++) {
			const syfs::Entry *a = entry_at(alone, i), *b = entry_at(fs, i);
			CHECK(a->kind == b->kind && a->parent == b->parent && !strcmp(a->name, b->name) && a->bytes == b->bytes);
			CHECK(num(fs, syfs_node(i + 1), SYFS_PROP_FRAMES, -1) == num(alone, syfs_node(i + 1), SYFS_PROP_FRAMES, -1));
		}
		const syfs_node one = syfs_node(held + 1), two_ = syfs_node(held + 2);
		const syfs::Entry *e = entry_at(fs, held);
		CHECK(e && e->kind == SYFS_TYPE_TRACK && e->format == SYFS_FORMAT_S760 && e->bytes == s760.size());
		CHECK(num(fs, one, SYFS_PROP_SECTOR, -1) == 0 && num(fs, one, SYFS_PROP_SECTORS, -1) == int64_t(sectors) + 150);
		CHECK(num(fs, one, SYFS_PROP_RATE, -1) == -1 && num(fs, one, SYFS_PROP_EMPHASIS, -1) == -1);
		Bytes back(s760.size());
		CHECK(syfs_read(fs, one, SYFS_AS_RAW, 0, back.data(), back.size()) == back.size() && back == s760);
		CHECK(num(fs, two_, SYFS_PROP_SECTOR, -1) == int64_t(sectors) + 150 && num(fs, two_, SYFS_PROP_SECTORS) == 12);
		CHECK(num(fs, two_, SYFS_PROP_PREGAP) == 150 && num(fs, SYFS_ROOT, SYFS_PROP_SECTORS) == int64_t(sectors) + 162);
		int16_t pcm[4];
		CHECK(syfs_read(fs, two_, SYFS_AS_S16, 8, pcm, sizeof pcm) == sizeof pcm && pcm[0] == 504 && pcm[3] == 507);
	}
	if (fs)
		close_image(fs);
	if (alone)
		close_image(alone);

	// a sheet whose file is missing, a mode that is not read, and text that is no sheet
	const std::string lost = "FILE \"gone.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n";
	write_file(d + "/lost.cue", lost.data(), lost.size());
	CHECK(!syfs_open_path((d + "/lost.cue").c_str(), err, sizeof err) && strstr(err, "gone.bin"));
	const std::string odd = "FILE \"audio.bin\" BINARY\n  TRACK 01 MODE2/2336\n    INDEX 01 00:00:00\n";
	write_file(d + "/odd.cue", odd.data(), odd.size());
	CHECK(!syfs_open_path((d + "/odd.cue").c_str(), err, sizeof err) && strstr(err, "MODE2/2336"));
	const std::string past = "FILE \"audio.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:01:00\n";
	write_file(d + "/past.cue", past.data(), past.size());
	CHECK(!syfs_open_path((d + "/past.cue").c_str(), err, sizeof err));
	std::string text = "TRACK and FILE are words in a note that is no sheet at all.\n";
	while (text.size() < 600)
		text += "It goes on for longer than a sector, so that it is looked at as an image is.\n";
	write_file(d + "/note.cue", text.data(), text.size());
	fs = syfs_open_path((d + "/note.cue").c_str(), err, sizeof err);
	CHECK(fs && format_of(fs) == SYFS_NONE);
	if (fs)
		close_image(fs);

	for (const char *name : { "audio.bin", "audio.cue", "data.bin", "two.raw", "mixed.cue", "lost.cue", "odd.cue", "past.cue",
		     "note.cue" })
		remove((d + "/" + name).c_str());
	remove(dir);
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
	CHECK(formats == 23 && syfs_format_at(0) == SYFS_FORMAT_S550 && syfs_format_at(formats - 1) == SYFS_FORMAT_FANTOM
		&& syfs_format_at(formats) == SYFS_NONE);
	CHECK(!strcmp(syfs_name(SYFS_NONE), "none") && !syfs_name(syfs_key_of("no such name")));
	CHECK(syfs_key_of("rhythm-set") == SYFS_TYPE_RHYTHM_SET && !strcmp(syfs_title(SYFS_TYPE_RHYTHM_SET), "rhythm set"));
}

// the address maps as tables: an S-700 block is its disk record a nibble a unit, at the manuals' addresses
static void test_maps()
{
	namespace map = syfs::map;
	size_t count = 0;
	const map::Map *maps = map::all(count);
	CHECK(count == 9 && maps == map::find("fantom") && !map::find("s7"));
	const std::tuple<const char *, const char *, uint32_t> models[] = { { "s750", "S-750", 5 }, { "s760", "S-760", 8 },
		{ "s770", "S-770", 5 }, { "sp700", "SP-700", 7 } };
	for (auto [id, model, blocks] : models)
	{
		const map::Map *m = map::find(id);
		CHECK(m && !strcmp(m->model, model) && m->model_id_size == 1 && m->model_id[0] == 0x34 && m->addr_size == 4
			&& m->block_count == blocks);
		const std::tuple<const char *, uint32_t, uint32_t> records[] = { { "volume", 16, 0 }, { "performance", 256, 1 << 14 },
			{ "patch", 256, 4 << 14 }, { "partial", 128, 8 << 14 }, { "sample", 48, 12 << 14 } };
		for (auto [name, bytes, addr] : records)
		{
			const map::Block *b = m->block(name);
			CHECK(b && b->size == bytes * 2 && m->at(b) && m->at(b)->addr == addr && m->entry_of(addr + b->size - 1) == m->at(b)
				&& b->name_size == 16 && b->name_units == 2 && !b->name_at);
			for (uint32_t n = 0; b && n < b->count; n++)
				CHECK(b->params[n].bits == 4 && b->params[n].offset + b->params[n].size <= b->size
					&& b->params[n].min <= b->params[n].max);
		}
		CHECK(!m->block("top") && !m->block("ext-partial") == !!strcmp(id, "s760"));
		for (uint32_t n = 1; n < m->entry_count; n++)
			CHECK(m->entries[n - 1].addr < m->entries[n].addr && m->entries[n].block < m->block_count);
	}
	// a grouped map: an area is a group's blocks, each at its place within it; a bank's areas are numbered
	const map::Map *jv = map::find("jv1080");
	CHECK(jv && jv->model_id_size == 1 && jv->model_id[0] == 0x6A && jv->addr_size == 4 && jv->area_count == 343);
	const map::Area *user = jv->area("User Patch", 5), *drums = jv->area("User Rhythm", 1);
	CHECK(jv->areas_of("User Patch") == 128 && jv->areas_of("Card Rhythm") == 2 && !jv->area("User Patch", 128));
	CHECK(user && !strcmp(user->group, "1-3") && user->addr == (0x11u << 21 | 5 << 14) && user->count == 5
		&& jv->area_in("1-3", 5) != user && drums && drums->count == 65 && drums->addr == (0x10u << 21 | 0x41 << 14));
	if (user && drums) {
		const map::Entry *tone = &jv->entries[user->first + 2];
		const map::Block *common = &jv->blocks[jv->entries[user->first].block], *tb = &jv->blocks[tone->block];
		CHECK(tone->addr == user->addr + (0x12 << 7) && jv->entry_of(tone->addr + 128) == tone && !jv->entry_of(tone->addr + 129)
			&& &jv->areas[tone->area] == user);
		CHECK(common->size == 72 && common->packed == 49 && common->name_size == 12 && common->name_units == 1
			&& tb->size == 129 && tb->packed == 88 && jv->blocks[jv->entries[drums->first + 1].block].packed == 42);
		// a packed record: every parameter in its bits from bit 0 up, a pair of nibbles its low one first, each block
		// from a byte; packed again it is the record, but for what the map has no parameter for
		std::vector<std::vector<uint8_t>> units, again;
		std::vector<uint8_t> rec(401), back;
		for (size_t k = 0; k < rec.size(); k++)
			rec[k] = uint8_t(k * 37 + 11);
		CHECK(map::unpack(*jv, *user, rec.data(), rec.size(), units) && units.size() == 5 && units[0].size() == 72
			&& units[4].size() == 129 && !map::unpack(*jv, *user, rec.data(), 400, again));
		CHECK(units[0][0] == (rec[0] & 127) && units[0][1] == ((rec[0] >> 7 | rec[1] << 1) & 127)
			&& units[0][12] == ((rec[10] >> 4 | rec[11] << 4) & 63));
		// the tone's wave number, two nibbles after 1 + 2 + 7 bits: the low one first in the record
		CHECK(units[1][0] == (rec[49] & 1) && units[1][4] == ((rec[50] >> 2) & 15) && units[1][3] == ((rec[50] >> 6 | rec[51] << 2) & 15));
		CHECK(map::pack(*jv, *user, units, back) && back.size() == 401 && map::unpack(*jv, *user, back.data(), back.size(), again)
			&& again == units);
		size_t same = 0;
		for (size_t k = 0; k < 401; k++)
			same += back[k] == rec[k];
		CHECK(same >= 401 - 12 && !memcmp(back.data(), rec.data(), 46) && !memcmp(&back[49], &rec[49], 87));
		units.clear();
		std::vector<uint8_t> set(2699, 0x5A);
		CHECK(map::unpack(*jv, *drums, set.data(), set.size(), units) && units.size() == 65 && units[64].size() == 58
			&& map::pack(*jv, *drums, units, back) && back.size() == 2699);
		CHECK(!map::unpack(*jv, *jv->area("System", 0), set.data(), set.size(), units));
	}
	const map::Block *sample = map::find("s760")->block("sample");
	const map::Param *start = nullptr, *select = nullptr;
	for (uint32_t n = 0; n < sample->count; n++)
		if (sample->params[n].offset == 32)
			start = &sample->params[n];
	CHECK(start && start->size == 8 && start->flags == map::LOW_FIRST && start->frac == 8 && start->max == 0xFFFFFFFF);
	const map::Block *perf = map::find("s760")->block("performance");
	for (uint32_t n = 0; n < perf->count; n++)
		if (perf->params[n].offset == 32)
			select = &perf->params[n];
	CHECK(select && select->flags == map::SIGNED && select->min == -1 && select->max == 127);
}

// ---- exclusive messages: an S-700 record as Data Sets, and a file of them read back
static Bytes syx_of(syfs_image *fs, size_t i)
{
	Bytes b(syfs_read(fs, syfs_node(i + 1), SYFS_AS_SYX, 0, nullptr, 0));
	CHECK(syfs_read(fs, syfs_node(i + 1), SYFS_AS_SYX, 0, b.data(), b.size()) == b.size());
	return b;
}

static bool exports(syfs_image *fs, size_t i, const char *as)
{
	char text[64] = "";
	syfs_get_str(fs, syfs_node(i + 1), SYFS_PROP_EXPORTS, text, sizeof text);
	return (std::string(" ") + text + " ").find(std::string(" ") + as + " ") != std::string::npos;
}

// the units a file's Data Sets hold by address, read apart from the library: Roland's, model 34, device 0, each
// within a row of 128 addresses and its sum right
static std::map<uint32_t, uint8_t> units_of(const Bytes &b, size_t *messages = nullptr)
{
	std::map<uint32_t, uint8_t> out;
	size_t at = 0, count = 0;
	while (at < b.size()) {
		size_t end = at + 1;
		while (end < b.size() && b[end] != 0xF7)
			end++;
		CHECK(end < b.size() && end - at >= 11 && b[at] == 0xF0 && b[at + 1] == 0x41 && b[at + 2] == 0 && b[at + 3] == 0x34
			&& b[at + 4] == 0x12);
		if (end >= b.size() || end - at < 11)
			break;
		unsigned sum = 0;
		for (size_t k = at + 5; k < end; k++)
			sum += b[k];
		const uint32_t addr = uint32_t(b[at + 5]) << 21 | b[at + 6] << 14 | b[at + 7] << 7 | b[at + 8];
		const size_t n = end - 1 - (at + 9);
		CHECK(sum % 128 == 0 && n >= 1 && n <= 128 && addr / 128 == (addr + n - 1) / 128);
		for (size_t k = 0; k < n; k++) {
			CHECK(b[at + 9 + k] < 16 && !out.count(addr + uint32_t(k)));
			out[addr + uint32_t(k)] = b[at + 9 + k];
		}
		count++;
		at = end + 1;
	}
	if (messages)
		*messages = count;
	return out;
}

// whether the units from an address on are the bytes, a nibble each with the high one first
static bool units_hold(const std::map<uint32_t, uint8_t> &units, uint32_t addr, const uint8_t *bytes, size_t n)
{
	for (size_t k = 0; k < 2 * n; k++) {
		auto u = units.find(addr + uint32_t(k));
		if (u == units.end() || u->second != (k & 1 ? bytes[k / 2] & 15 : bytes[k / 2] >> 4))
			return false;
	}
	return true;
}

static void test_syx()
{
	// the disk of test_s700 with its records as a machine keeps them: the names, the numbers memory has, and an
	// S-760's extended records
	Bytes b = s700_image(" S-760 System Disk    Ver. 2.24");
	b.resize((0x19AE + 2) * 512, 0);
	const size_t perf = 0x8AC * 512, patch = 0xAAC * 512, partial = 0xEAC * 512, sample = 0x12AC * 512, ext = 0x19AE * 512;
	for (size_t k = 16; k < 256; k++) {
		b[perf + k] = uint8_t(k * 7);
		b[patch + k] = uint8_t(k * 5 + 1);
	}
	for (size_t k = 0; k < 128; k++) {
		b[perf + 0x180 + k] = uint8_t(0xE0 ^ k);
		b[ext + k] = uint8_t(0x30 + k);
		b[ext + 128 + k] = uint8_t(0xFF - k);
		if (k >= 80)
			b[partial + k] = b[partial + 128 + k] = uint8_t(k * 3);
	}
	put_name(b, perf, "PRF:One", 16);
	b[perf + 3] = 0x87;  // a number where the colon is, as a performance's record may have: read as the colon
	put_name(b, patch, "STR:Strings", 16);
	put_name(b, partial, "PRT:Low", 16);
	put_name(b, partial + 128, "PRT:High", 16);
	put_name(b, sample, "SMP:Low", 16);
	put_name(b, sample + 96, "SMP:High", 16);
	memset(&b[perf + 0x10], 0xFF, 32);
	b[perf + 0x10] = b[perf + 0x11] = 0;
	for (int k = 0; k < 88; k++)
		b[patch + 0x20 + k] = k == 87 ? 255 : k < 44 ? 1 : 0;
	syfs_io io = io_of(b);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	const size_t one = find(fs, "PRF:One"), strings = find(fs, "STR:Strings"), low = find(fs, "PRT:Low"),
		high = find(fs, "PRT:High"), slow = find(fs, "SMP:Low"), shigh = find(fs, "SMP:High");
	CHECK(exports(fs, strings, "syx") && exports(fs, shigh, "syx") && exports(fs, shigh, "wav"));
	CHECK(!exports(fs, find(fs, "VOL:One"), "syx") && syx_of(fs, find(fs, "VOL:One")).empty());
	// each record's first half at its number's place, the extended records after their own
	size_t messages = 0;
	auto units = units_of(syx_of(fs, one), &messages);
	CHECK(messages == 6 && units.size() == 768 && units_hold(units, 1 << 14, &b[perf], 256) && units_hold(units, 3 << 14, &b[perf + 0x180], 128));
	units = units_of(syx_of(fs, strings), &messages);
	CHECK(messages == 4 && units.size() == 512 && units_hold(units, 4 << 14, &b[patch], 256));
	units = units_of(syx_of(fs, high), &messages);
	CHECK(messages == 4 && units.size() == 512 && units_hold(units, (8 << 14) + 256, &b[partial + 128], 128)
		&& units_hold(units, (1 << 21) + (2 << 14) + 256, &b[ext + 128], 128));
	units = units_of(syx_of(fs, shigh), &messages);
	// the third sample's 96 units run over a row's end: two messages
	CHECK(messages == 2 && units.size() == 96 && units_hold(units, (12 << 14) + 2 * 96, &b[sample + 96], 48));
	// parts of it read
	Bytes whole = syx_of(fs, strings), part(100);
	CHECK(syfs_read(fs, syfs_node(strings + 1), SYFS_AS_SYX, 300, part.data(), 100) == 100 && !memcmp(part.data(), &whole[300], 100));
	CHECK(syfs_read(fs, syfs_node(strings + 1), SYFS_AS_SYX, whole.size() - 10, part.data(), 100) == 10);

	// the file of all of them: the same entries, each giving the same messages
	Bytes file;
	const size_t order[] = { shigh, low, one, high, strings, slow };
	for (size_t i : order) {
		Bytes m = syx_of(fs, i);
		file.insert(file.end(), m.begin(), m.end());
	}
	syfs_io fio = io_of(file);
	syfs_key medium = SYFS_NONE;
	CHECK(probe(&fio, &medium) == SYFS_FORMAT_SYX && medium == SYFS_MEDIUM_FILE);
	syfs_image *sx = syfs_open(&fio, nullptr, 0);
	CHECK(sx);
	if (sx) {
		CHECK(format_of(sx) == SYFS_FORMAT_SYX && medium_of(sx) == SYFS_MEDIUM_FILE && count_of(sx) == 6);
		CHECK((names(sx, SYFS_TYPE_PERFORMANCE) == std::vector<std::string> { "PRF:One" }));
		CHECK((names(sx, SYFS_TYPE_PARTIAL) == std::vector<std::string> { "PRT:Low", "PRT:High" }));
		CHECK((names(sx, SYFS_TYPE_SAMPLE) == std::vector<std::string> { "SMP:Low", "SMP:High" }));
		for (size_t i : order) {
			const syfs::Entry *e = entry_at(fs, i);
			const size_t j = find(sx, e->name, e->kind);
			CHECK(j != NONE && entry_at(sx, j)->number == e->number && entry_at(sx, j)->format == SYFS_FORMAT_S760
				&& !strcmp(entry_at(sx, j)->tag, e->tag) && syx_of(sx, j) == syx_of(fs, i) && exports(sx, j, "syx")
				&& !exports(sx, j, "wav"));
		}
		CHECK(!strcmp(entry_at(sx, find(sx, "STR:Strings"))->tag, "STR"));
		CHECK((used(sx, find(sx, "PRF:One")) == std::vector<std::string> { "STR:Strings" }));
		CHECK((used(sx, find(sx, "STR:Strings")) == std::vector<std::string> { "PRT:High", "PRT:Low" }));
		CHECK((used(sx, find(sx, "PRT:Low")) == std::vector<std::string> { "SMP:Low" }));
		CHECK((used(sx, find(sx, "PRT:High")) == std::vector<std::string> { "SMP:High" }));
		close_image(sx);
	}

	// a file shorter than a sector; a block with a message missing or its sum wrong is no entry; other messages and
	// bytes between them are passed over
	Bytes small = syx_of(fs, shigh);
	CHECK(small.size() < 512);
	syfs_io sio = io_of(small);
	sx = syfs_open(&sio, nullptr, 0);
	CHECK(sx && count_of(sx) == 1 && !strcmp(entry_at(sx, 0)->name, "SMP:High") && entry_at(sx, 0)->number == 2);
	if (sx)
		close_image(sx);
	Bytes cut = syx_of(fs, slow), patch_syx = syx_of(fs, strings);
	const uint8_t other[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0xFE };
	cut.insert(cut.end(), other, other + sizeof other);
	cut.insert(cut.end(), patch_syx.begin(), patch_syx.begin() + 3 * (patch_syx.size() / 4));
	Bytes wrong = syx_of(fs, low);
	wrong[20] ^= 1;
	cut.insert(cut.end(), wrong.begin(), wrong.end());
	syfs_io cio = io_of(cut);
	sx = syfs_open(&cio, nullptr, 0);
	CHECK(sx && count_of(sx) == 1 && !strcmp(entry_at(sx, 0)->name, "SMP:Low"));
	if (sx)
		close_image(sx);
	close_image(fs);

	// an S-770's disk: no extended records, by the S-770's map
	Bytes c = s700_image(" SYS-772 HardDisk Sys Ver. 2.25");
	syfs_io io770 = io_of(c);
	fs = syfs_open(&io770, nullptr, 0);
	CHECK(fs);
	if (fs) {
		units = units_of(syx_of(fs, find(fs, "PRF:One")), &messages);
		CHECK(messages == 4 && units.size() == 512 && units_hold(units, 1 << 14, &c[perf], 256));
		units = units_of(syx_of(fs, find(fs, "PRT:High")), &messages);
		CHECK(messages == 2 && units_hold(units, (8 << 14) + 256, &c[partial + 128], 128));
		close_image(fs);
	}
	// what is no Data Set of a known model is not a file of them
	Bytes none = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x00, 0x00, 0x00, 0x00, 0xF7 };
	syfs_io nio = io_of(none);
	CHECK(probe(&nio, nullptr) == SYFS_NONE && !syfs_open(&nio, nullptr, 0));
}

// ---- the JV-1080's packed records as Data Sets: a card's and a board's, and files of them
// Roland Data Sets of a model and device as (address, data), each within a row of 128 and its sum right
static std::vector<std::pair<uint32_t, Bytes>> data_sets_of(const Bytes &b, uint8_t device, uint8_t model)
{
	std::vector<std::pair<uint32_t, Bytes>> out;
	for (size_t at = 0; at < b.size();) {
		size_t end = at + 1;
		while (end < b.size() && b[end] != 0xF7)
			end++;
		CHECK(end < b.size() && end - at >= 11 && b[at] == 0xF0 && b[at + 1] == 0x41 && b[at + 2] == device
			&& b[at + 3] == model && b[at + 4] == 0x12);
		if (end >= b.size() || end - at < 11)
			break;
		unsigned sum = 0;
		for (size_t k = at + 5; k < end; k++)
			sum += b[k];
		const uint32_t addr = uint32_t(b[at + 5]) << 21 | b[at + 6] << 14 | b[at + 7] << 7 | b[at + 8];
		Bytes data(b.begin() + at + 9, b.begin() + end - 1);
		CHECK(sum % 128 == 0 && !data.empty() && data.size() <= 128 && addr / 128 == (addr + data.size() - 1) / 128);
		out.emplace_back(addr, data);
		at = end + 1;
	}
	return out;
}

static void test_syx_jv()
{
	// a card of two halves: a patch in each, the second half's last, and a rhythm set; fields put in their bits
	auto put = [](Bytes &b, size_t at, size_t bit, unsigned bits, unsigned v) {
		for (unsigned k = 0; k < bits; k++)
			if (v >> k & 1)
				b[at + (bit + k) / 8] |= uint8_t(1 << (bit + k) % 8);
	};
	auto name = [&](Bytes &b, size_t at, const char *s) {
		for (size_t i = 0; i < 12; i++)
			put(b, at, 7 * i, 7, i < strlen(s) ? uint8_t(s[i]) : ' ');
	};
	Bytes jv(0x10000, 0);
	memcpy(&jv[0], "RolandJV-990D\xBBP\xAF", 16);
	memcpy(&jv[0x10], "My Card         ", 16);
	jv[0x8000] = 0xAF;
	memcpy(&jv[0x8001], "RolandJV-990D", 13);
	const size_t first = 0xF70, last = 0x8F70 + 63 * 401, set = 0x73B0;
	name(jv, first, "Symphonique"), name(jv, last, "Tortured"), name(jv, set, "HouseDrumSet");
	name(jv, 0x20, "Tekno Loop 1");
	put(jv, 0x20, 8 * (53 + 15 * 12), 1, 1);      // the first performance's last part: its receive switch
	put(jv, first, 84, 6, 39);                    // the EFX type, after the name
	put(jv, first, 8 * 49 + 3, 7, 89);            // tone 1's wave group ID, after its switch and group
	put(jv, first, 8 * 49 + 10, 4, 0xE);          // its wave number's low nibble, then the high one
	put(jv, first, 8 * 49 + 14, 4, 0xF);
	put(jv, first, 8 * (49 + 3 * 88), 1, 1);      // tone 4's switch
	put(jv, first, 8 * (49 + 3 * 88) + 689, 7, 100);  // and its last parameter
	put(jv, set, 8 * (11 + 42 * 63), 1, 1);       // the rhythm set's last key: its switch
	put(jv, set, 8 * (11 + 42 * 63) + 326, 7, 77);  // and its last parameter
	syfs_io io = io_of(jv);
	syfs_image *fs = syfs_open(&io, nullptr, 0);
	CHECK(fs);
	if (!fs)
		return;
	const size_t symph = find(fs, "Symphonique"), tortured = find(fs, "Tortured"), house = find(fs, "HouseDrumSet");
	CHECK(exports(fs, symph, "syx") && exports(fs, house, "syx"));
	// a performance: its common in 53 bytes and sixteen parts in 12 each, at the card's performance of its number
	{
		auto perf = data_sets_of(syx_of(fs, find(fs, "Tekno Loop 1")), 0x10, 0x6A);
		CHECK(perf.size() == 17 && perf[0].first == 0x20u << 21 && perf[0].second.size() == 64
			&& !memcmp(perf[0].second.data(), "Tekno Loop 1", 12) && perf[1].first == (0x20u << 21) + (0x10 << 7)
			&& perf[16].second.size() == 19 && perf[16].second[0] == 1 && perf[15].second[0] == 0);
	}
	// the common and four tones at the card's patch of its number: a tone's 129 bytes are two messages
	const uint32_t card = 0x21u << 21;
	auto sets = data_sets_of(syx_of(fs, symph), 0x10, 0x6A);
	CHECK(sets.size() == 9 && sets[0].first == card && sets[0].second.size() == 72 && sets[1].first == card + (0x10 << 7)
		&& sets[1].second.size() == 128 && sets[2].first == card + (0x11 << 7) && sets[2].second.size() == 1
		&& sets[7].first == card + (0x16 << 7));
	if (sets.size() == 9) {
		CHECK(!memcmp(sets[0].second.data(), "Symphonique ", 12) && sets[0].second[12] == 39 && sets[0].second[13] == 0);
		CHECK(sets[1].second[2] == 89 && sets[1].second[3] == 0xF && sets[1].second[4] == 0xE && sets[1].second[0] == 0);
		CHECK(sets[7].second[0] == 1 && sets[8].second[0] == 100);
	}
	sets = data_sets_of(syx_of(fs, tortured), 0x10, 0x6A);
	CHECK(sets.size() == 9 && sets[0].first == card + (127 << 14) && !memcmp(sets[0].second.data(), "Tortured    ", 12));
	// a rhythm set: its common and keys 35 to 98
	sets = data_sets_of(syx_of(fs, house), 0x10, 0x6A);
	CHECK(sets.size() == 65 && sets[0].first == (0x20u << 21 | 0x40 << 14) && sets[0].second.size() == 12
		&& sets[1].first == sets[0].first + (35 << 7) && sets[64].first == sets[0].first + (98 << 7)
		&& sets[64].second.size() == 58 && sets[64].second[0] == 1 && sets[64].second[57] == 77 && sets[63].second[0] == 0);

	// ---- a JV-1080's battery memory, opened as one: the user performances, patches and rhythm sets whose names
	// are printable, each at its own place
	{
		Bytes mem(0x10000, 0);
		name(mem, 0x100 + 31 * 245, "Power JV"), name(mem, 0x2000 + 5 * 401, "Fantasia JV"), name(mem, 0xE900 + 2699, "JazzDrumSet1");
		memcpy(&mem[0x2000 + 6 * 401], "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 11);
		put(mem, 0x2000 + 5 * 401, 84, 6, 17);
		syfs_io mio = io_of(mem);
		char err[64] = "";
		CHECK(!syfs_open_as(&mio, SYFS_FORMAT_JV80, SYFS_MEDIUM_MEMORY, err, sizeof err) && *err);
		CHECK(!syfs_open_as(&mio, SYFS_FORMAT_JV1080, SYFS_MEDIUM_CARD, err, sizeof err));
		syfs_image *m = syfs_open_as(&mio, SYFS_FORMAT_JV1080, SYFS_MEDIUM_MEMORY, err, sizeof err);
		CHECK(m && format_of(m) == SYFS_FORMAT_JV1080 && medium_of(m) == SYFS_MEDIUM_MEMORY && count_of(m) == 3);
		if (m) {
			const size_t p = find(m, "Fantasia JV"), f = find(m, "Power JV"), r = find(m, "JazzDrumSet1");
			CHECK(entry_at(m, p)->number == 5 && entry_at(m, p)->kind == SYFS_TYPE_PATCH && entry_at(m, f)->number == 31
				&& entry_at(m, r)->number == 1 && entry_at(m, r)->kind == SYFS_TYPE_RHYTHM_SET);
			auto got = data_sets_of(syx_of(m, p), 0x10, 0x6A);
			CHECK(got.size() == 9 && got[0].first == (0x11u << 21 | 5 << 14) && got[0].second[12] == 17);
			got = data_sets_of(syx_of(m, f), 0x10, 0x6A);
			CHECK(got.size() == 17 && got[0].first == (0x10u << 21 | 31 << 14));
			got = data_sets_of(syx_of(m, r), 0x10, 0x6A);
			CHECK(got.size() == 65 && got[0].first == (0x10u << 21 | 0x41 << 14));
			close_image(m);
		}
		// a memory never written lists nothing; another size is none
		Bytes blank(0x10000, 0), small(0x8000, 0);
		syfs_io bio = io_of(blank), smio = io_of(small);
		m = syfs_open_as(&bio, SYFS_FORMAT_JV1080, SYFS_MEDIUM_MEMORY, nullptr, 0);
		CHECK(m && count_of(m) == 0);
		if (m)
			close_image(m);
		CHECK(!syfs_open_as(&smio, SYFS_FORMAT_JV1080, SYFS_MEDIUM_MEMORY, nullptr, 0));
	}
	// a file of the three: each under its bank, numbered as the card has them, giving the same messages
	Bytes file = syx_of(fs, tortured), more = syx_of(fs, house), third = syx_of(fs, symph);
	file.insert(file.end(), more.begin(), more.end());
	file.insert(file.end(), third.begin(), third.end());
	syfs_io fio = io_of(file);
	syfs_key medium = SYFS_NONE;
	CHECK(probe(&fio, &medium) == SYFS_FORMAT_SYX && medium == SYFS_MEDIUM_FILE);
	syfs_image *sx = syfs_open(&fio, nullptr, 0);
	CHECK(sx && count_of(sx) == 5);
	if (sx) {
		CHECK((names(sx, SYFS_TYPE_BANK) == std::vector<std::string> { "Card Rhythm", "Card Patch" }));
		CHECK((names(sx, SYFS_TYPE_PATCH) == std::vector<std::string> { "Symphonique", "Tortured" }));
		const size_t a = find(sx, "Symphonique"), z = find(sx, "Tortured"), h = find(sx, "HouseDrumSet");
		CHECK(entry_at(sx, a)->number == 0 && entry_at(sx, z)->number == 127 && entry_at(sx, h)->number == 0
			&& entry_at(sx, h)->kind == SYFS_TYPE_RHYTHM_SET && entry_at(sx, a)->format == SYFS_FORMAT_JV1080);
		CHECK(entry_at(sx, a)->parent == find(sx, "Card Patch") && entry_at(sx, h)->parent == find(sx, "Card Rhythm"));
		CHECK(syx_of(sx, a) == syx_of(fs, symph) && syx_of(sx, z) == syx_of(fs, tortured) && syx_of(sx, h) == syx_of(fs, house));
		CHECK(!exports(sx, find(sx, "Card Patch"), "syx") && used(sx, a).empty());
		close_image(sx);
	}
	// a patch short of a tone is none; a common two bytes longer, as a JV-2080 sends it, is the same patch; a
	// system's blocks are an entry of their own
	Bytes whole = syx_of(fs, symph);
	Bytes shorter(whole.begin(), whole.end() - 12);
	syfs_io hio = io_of(shorter);
	sx = syfs_open(&hio, nullptr, 0);
	CHECK(sx && count_of(sx) == 0);
	if (sx)
		close_image(sx);
	Bytes longer = { 0xF0, 0x41, 0x10, 0x6A, 0x12, 0x21, 0x00, 0x00, 0x00 };
	unsigned sum = 0x21;
	for (int k = 0; k < 74; k++) {
		longer.push_back(k < 72 ? whole[9 + k] : 1);
		sum += longer.back();
	}
	longer.push_back(uint8_t((128 - sum % 128) % 128));
	longer.push_back(0xF7);
	longer.insert(longer.end(), whole.begin() + 9 + 72 + 2, whole.end());
	syfs_io lio = io_of(longer);
	sx = syfs_open(&lio, nullptr, 0);
	CHECK(sx && count_of(sx) == 2 && syx_of(sx, 1) == whole);
	if (sx)
		close_image(sx);
	close_image(fs);
}

// the XV-5080's and XV-3080's records: packed from each byte's top bit, a block from a byte
static std::vector<std::pair<uint32_t, Bytes>> xv_sets_of(const Bytes &b)
{
	std::vector<std::pair<uint32_t, Bytes>> out;
	for (size_t at = 0; at < b.size();) {
		size_t end = at + 1;
		while (end < b.size() && b[end] != 0xF7)
			end++;
		CHECK(end < b.size() && end - at >= 12 && b[at] == 0xF0 && b[at + 1] == 0x41 && b[at + 2] == 0x10 && b[at + 3] == 0
			&& b[at + 4] == 0x10 && b[at + 5] == 0x12);
		if (end >= b.size() || end - at < 12)
			break;
		unsigned sum = 0;
		for (size_t k = at + 6; k < end; k++)
			sum += b[k];
		const uint32_t addr = uint32_t(b[at + 6]) << 21 | b[at + 7] << 14 | b[at + 8] << 7 | b[at + 9];
		Bytes data(b.begin() + at + 10, b.begin() + end - 1);
		CHECK(sum % 128 == 0 && !data.empty() && data.size() <= 128 && addr / 128 == (addr + data.size() - 1) / 128);
		out.emplace_back(addr, data);
		at = end + 1;
	}
	return out;
}

static void test_syx_xv()
{
	namespace map = syfs::map;
	auto put = [](Bytes &b, size_t at, size_t bit, unsigned bits, unsigned v) {
		for (unsigned k = 0; k < bits; k++)
			if (v >> (bits - 1 - k) & 1)
				b[at + (bit + k) / 8] |= uint8_t(0x80 >> (bit + k) % 8);
	};
	auto name = [&](Bytes &b, size_t at, const char *s) {
		for (size_t i = 0; i < 12; i++)
			put(b, at, 7 * i, 7, i < strlen(s) ? uint8_t(s[i]) : ' ');
	};
	// the maps: the XV-3080's is the XV-5080's less the system's equalizer, the parts past sixteen and the split keys;
	// a part's temporary patch and rhythm set are areas apart
	const map::Map *xv = map::find("xv5080"), *lesser = map::find("xv3080");
	CHECK(xv && lesser && xv->high_first && xv->model_id_size == 2 && xv->model_id[1] == 0x10 && !map::find("jv1080")->high_first);
	CHECK(xv->areas_of("User Patch") == 128 && xv->areas_of("User Rhythm") == 4 && xv->areas_of("User Performance") == 64
		&& xv->areas_of("Temporary Patch") == 33 && lesser->areas_of("Temporary Rhythm") == 17);
	const map::Area *user = xv->area("User Patch", 3), *small = lesser->area("User Patch", 3);
	CHECK(user && small && user->addr == (0x30u << 21 | 3 << 14) && user->count == 9 + 88 && small->count == 9
		&& !strcmp(user->group, "1-3-1") && xv->area("Temporary Rhythm", 32)->addr == (0x1Fu << 21 | 0x10 << 14));
	{
		// a patch: 574 bytes, the split keys in none; a value about 64 in fewer bits than hold it is kept about their
		// middle (the octave shift, three bits after 121)
		Bytes rec(574, 0);
		std::vector<std::vector<uint8_t>> units, again;
		name(rec, 0, "Octave Up");
		put(rec, 0, 121, 3, 5);
		put(rec, 56, 106, 16, 0x8123);            // the MFX's first parameter: four nibbles, the high one first
		put(rec, 234 + 3 * 85, 0, 7, 99);         // tone 4's level
		CHECK(map::unpack(*xv, *user, rec.data(), rec.size(), units) && units.size() == 97 && units[0].size() == 79
			&& units[9].empty() && units[96].empty() && !map::unpack(*xv, *user, rec.data(), 573, again));
		CHECK(!memcmp(units[0].data(), "Octave Up   ", 12) && units[0][19] == 65 && units[1][0x11] == 8 && units[1][0x12] == 1
			&& units[1][0x14] == 3 && units[8][0] == 99 && units[5][0] == 0);
		Bytes back;
		CHECK(map::pack(*xv, *user, units, back) && back == rec);
	}
	// the battery memories: an XV-5080's lists from 0x1A8, an XV-3080's four bytes lower and its patches shorter
	for (const bool five : { true, false }) {
		Bytes mem(0x40000, 0);
		const size_t perfs = five ? 0x1A8 : 0x1A4, patches = perfs + 64 * 1064, sets = patches + 128 * (five ? 938 : 838);
		name(mem, perfs + 63 * 1064, "Seq:Template"), name(mem, patches + 3 * (five ? 938 : 838), "Y2K Concerto");
		name(mem, sets + 3 * 10808, "XV Rust Kit");
		put(mem, patches + 3 * (five ? 938 : 838), 92, 7, 111);           // the patch's level
		put(mem, perfs + 63 * 1064 + 222 + 22, 2, 4, 9);                   // part 2's receive channel, after two bits
		put(mem, sets + 3 * 10808 + 160 + 87 * 121, 0, 7, 'Z');            // the last key's tone's name
		const syfs_key format = five ? SYFS_FORMAT_XV5080 : SYFS_FORMAT_XV3080;
		syfs_io mio = io_of(mem);
		syfs_image *m = syfs_open_as(&mio, format, SYFS_MEDIUM_MEMORY, nullptr, 0);
		CHECK(m && format_of(m) == format && medium_of(m) == SYFS_MEDIUM_MEMORY && count_of(m) == 3);
		if (!m)
			continue;
		const size_t p = find(m, "Y2K Concerto"), f = find(m, "Seq:Template"), r = find(m, "XV Rust Kit");
		CHECK(entry_at(m, p)->number == 3 && entry_at(m, f)->number == 63 && entry_at(m, r)->number == 3 && exports(m, p, "syx"));
		auto got = xv_sets_of(syx_of(m, p));
		// the common, the MFX in two messages, the chorus, the reverb, the TMT and four tones in two each
		CHECK(got.size() == 14 && got[0].first == (0x30u << 21 | 3 << 14) && got[0].second.size() == 79
			&& got[0].second[14] == 111 && got[1].first == got[0].first + (2 << 7) && got[2].second.size() == 17
			&& got[13].first == got[0].first + (0x27 << 7) && got[13].second.size() == 9);
		got = xv_sets_of(syx_of(m, f));
		const size_t parts = five ? 32 : 16;
		CHECK(got.size() == 1 + 2 + 1 + 1 + 16 + parts && got[0].first == (0x20u << 21 | 63 << 14)
			&& got[21].first == got[0].first + (0x20 << 7) && got[22].second[0] == 9 && got[21].second[0] == 0);
		got = xv_sets_of(syx_of(m, r));
		CHECK(got.size() == 1 + 2 + 1 + 1 + 2 * 88 && got[0].first == (0x40u << 21 | 0x30 << 14)
			&& got[got.size() - 2].first == got[0].first + (0x10 << 7) + (2 * 87 << 7) && got[got.size() - 2].second[0] == 'Z');
		// a file of them is read by the lesser map where nothing in it needs the greater: a performance of 32 parts does
		Bytes file = syx_of(m, p), perf = syx_of(m, f);
		syfs_io fio = io_of(file);
		syfs_image *sx = syfs_open(&fio, nullptr, 0);
		CHECK(sx && count_of(sx) == 2 && entry_at(sx, find(sx, "Y2K Concerto"))->format == SYFS_FORMAT_XV3080
			&& syx_of(sx, find(sx, "Y2K Concerto")) == file && (names(sx, SYFS_TYPE_BANK) == std::vector<std::string> { "User Patch" }));
		if (sx)
			close_image(sx);
		file.insert(file.end(), perf.begin(), perf.end());
		syfs_io bio = io_of(file);
		sx = syfs_open(&bio, nullptr, 0);
		CHECK(sx && count_of(sx) == 4 && entry_at(sx, find(sx, "Seq:Template"))->format == format
			&& syx_of(sx, find(sx, "Seq:Template")) == perf);
		if (sx)
			close_image(sx);
		// the machine's .SVD file: the same records in chunks, a patch's in 938 bytes of an XV-5080's and 574 of an
		// XV-3080's, which tells whose it is
		{
			Bytes svd(0x70, 0);
			svd[1] = 0x6E;
			memcpy(&svd[2], "SVD1", 4);
			const std::tuple<const char *, size_t, uint32_t, uint32_t, uint32_t> chunks[] = { { "PRFa", perfs, 1064, 926, 64 },
				{ "PATa", patches, five ? 938u : 838u, five ? 938u : 574u, 128 }, { "RHYa", sets, 10808, 10808, 4 } };
			size_t entry = 16;
			for (auto [tag, at, stride, size, count] : chunks) {
				memcpy(&svd[entry], tag, 4), memcpy(&svd[entry + 4], "XV88", 4);
				const uint32_t head[4] = { uint32_t(svd.size()), 16 + count * size, count, size };
				for (int w = 0; w < 2; w++)
					for (int k = 0; k < 4; k++)
						svd[entry + 8 + 4 * w + k] = uint8_t(head[w] >> (24 - 8 * k));
				for (int w = 2; w < 4; w++)
					for (int k = 0; k < 4; k++)
						svd.push_back(uint8_t(head[w] >> (24 - 8 * k)));
				svd.insert(svd.end(), { 0, 0, 0, 16, 0, 0, 0, 0 });
				for (uint32_t k = 0; k < count; k++)
					svd.insert(svd.end(), mem.begin() + at + k * stride, mem.begin() + at + k * stride + size);
				entry += 16;
			}
			syfs_io sio = io_of(svd);
			syfs_key medium = SYFS_NONE;
			CHECK(probe(&sio, &medium) == format && medium == SYFS_MEDIUM_FILE);
			syfs_image *sv = syfs_open(&sio, nullptr, 0);
			CHECK(sv && count_of(sv) == 3);
			if (sv) {
				CHECK(syx_of(sv, find(sv, "Y2K Concerto")) == syx_of(m, p) && syx_of(sv, find(sv, "Seq:Template")) == syx_of(m, f)
					&& syx_of(sv, find(sv, "XV Rust Kit")) == syx_of(m, r) && entry_at(sv, find(sv, "XV Rust Kit"))->number == 3);
				close_image(sv);
			}
			// the file on a FAT volume: its sounds are under it, read as from the file alone, and go with it
			Sparse disk(1440 * 1024);
			syfs_io w { &disk, sparse_read, sparse_write, disk.size };
			char err[128];
			CHECK(syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, nullptr, err, sizeof err));
			syfs_image *fat = syfs_open(&w, nullptr, 0);
			CHECK(fat && put_file(fat, SYFS_ROOT, "SOUND_00.SVD", svd) != SYFS_NO_NODE);
			if (fat)
				syfs_close(fat);
			syfs_io ro = io_of(disk);
			fat = syfs_open(&ro, nullptr, 0);
			CHECK(fat && count_of(fat) == 4);
			if (fat) {
				const size_t file = find(fat, "SOUND_00.SVD"), in = find(fat, "Y2K Concerto");
				CHECK(file == 0 && entry_at(fat, in)->parent == file && entry_at(fat, in)->format == format
					&& entry_at(fat, in)->number == 3 && syx_of(fat, in) == syx_of(m, p)
					&& syx_of(fat, find(fat, "XV Rust Kit")) == syx_of(m, r) && exports(fat, file, "raw") && !exports(fat, file, "syx"));
				close_image(fat);
			}
			// a sound within the file is not removed or named by itself
			fat = syfs_open(&w, nullptr, 0);
			syfs_node all[8];
			CHECK(fat && syfs_list(fat, SYFS_ROOT, SYFS_REL_ALL, all, 8) == 4);
			if (fat) {
				CHECK(!syfs_remove(fat, 2) && !syfs_set_str(fat, 2, SYFS_PROP_NAME, "Other") && syfs_remove(fat, 1)
					&& syfs_list(fat, SYFS_ROOT, SYFS_REL_ALL, all, 8) == 0);
				syfs_close(fat);
			}
		}
		close_image(m);
	}
	CHECK(!syfs_open_as(nullptr, SYFS_FORMAT_XV5080, SYFS_MEDIUM_MEMORY, nullptr, 0));
	Bytes wrong(0x10000, 0);
	syfs_io wio = io_of(wrong);
	CHECK(!syfs_open_as(&wio, SYFS_FORMAT_XV5080, SYFS_MEDIUM_MEMORY, nullptr, 0));

	// an SRX board: an XV-5080 bank's patch as it is, an XV-3080 bank's rhythm set made of its common and the tones
	// it points at from the bank's start; a bank for neither gives none
	static const uint8_t srx_lines[18] = { 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15 };
	Rom srx { Bytes(32 << 20, 0), srx_lines, 18 };
	memcpy(&srx.b[0], "Roland SRX", 10);
	srx.be(0x42, 3, 2);
	const uint32_t patches = 0x1F80000, sets = 0x1F90000, global = 0x1FA0000;
	srx.be(0x80, 4, 2), srx.be(0x82, 7, 2), srx.be(0x84, patches, 4), srx.be(0x88, 0x1000, 4);
	srx.be(0x90, 5, 2), srx.be(0x92, 5, 2), srx.be(0x94, sets, 4), srx.be(0x98, 0x1000, 4);
	srx.be(0xA0, 4, 2), srx.be(0xA2, 8, 2), srx.be(0xA4, global, 4), srx.be(0xA8, 0x1000, 4);
	Bytes patch(574, 0), set(512, 0), tone(121, 0);
	name(patch, 0, "Test Patch"), name(set, 0, "Test Kit"), name(tone, 0, "Rim Shot");
	put(patch, 0, 92, 7, 80);
	for (uint32_t k = 0; k < 88; k++)
		set[160 + 4 * k + 2] = 0x08, set[160 + 4 * k + 3] = 0;       // every key the tone at +0x800
	set[160 + 4 * 87 + 3] = 0x80;                                        // but the last, a tone of zeros
	srx.be(patches, 130, 2), srx.be(patches + 4, 0x0E, 4), srx.be(patches + 8, 574, 4);
	srx.be(global, 1, 2), srx.be(global + 4, 0x0E, 4), srx.be(global + 8, 574, 4);
	srx.be(sets, 1, 2), srx.be(sets + 4, 20, 4), srx.be(sets + 8, 512, 4);
	// 130 patches do not fit the table's 0x1000 bytes: its length is what the directory says, so make it so
	srx.be(0x88, 0x20000 - 0x10000, 4), srx.be(patches, 2, 2);
	for (size_t k = 0; k < 574; k++)
		srx.put(patches + 0x0E + 574 + k, patch[k]), srx.put(global + 0x0E + k, patch[k]);
	for (size_t k = 0; k < 512; k++)
		srx.put(sets + 20 + k, set[k]);
	for (size_t k = 0; k < 121; k++)
		srx.put(sets + 0x800 + k, tone[k]);
	syfs_io rio = io_of(srx.b);
	syfs_image *fs = syfs_open(&rio, nullptr, 0);
	CHECK(fs);
	if (fs) {
		size_t p = NONE, g = NONE;
		const size_t kit = find(fs, "Test Kit");
		for (size_t i = 0; i < count_of(fs); i++)
			if (!strcmp(entry_at(fs, i)->name, "Test Patch"))
				(!strcmp(entry_at(fs, entry_at(fs, i)->parent)->tag, "4:7") ? p : g) = i;
		CHECK(p != NONE && g != NONE && exports(fs, p, "syx") && !exports(fs, g, "syx") && exports(fs, kit, "syx"));
		auto got = xv_sets_of(syx_of(fs, p));
		CHECK(got.size() == 14 && got[0].first == (0x30u << 21 | 1 << 14) && got[0].second[14] == 80
			&& !memcmp(got[0].second.data(), "Test Patch  ", 12));
		got = xv_sets_of(syx_of(fs, kit));
		CHECK(got.size() == 5 + 2 * 88 && got[0].first == 0x40u << 21 && !memcmp(got[0].second.data(), "Test Kit    ", 12)
			&& !memcmp(got[5].second.data(), "Rim Shot    ", 12) && got[5 + 2 * 87].second[0] == 0);
		close_image(fs);
	}
}

// the Fantom's records: the XVs' packing, with a group's blocks in an order of their own and parameters that start
// on a byte
static void test_syx_fantom()
{
	namespace map = syfs::map;
	auto put = [](Bytes &b, size_t at, size_t bit, unsigned bits, unsigned v) {
		for (unsigned k = 0; k < bits; k++)
			if (v >> (bits - 1 - k) & 1)
				b[at + (bit + k) / 8] |= uint8_t(0x80 >> (bit + k) % 8);
	};
	auto name = [&](Bytes &b, size_t at, const char *s) {
		for (size_t i = 0; i < 12; i++)
			put(b, at, 7 * i, 7, i < strlen(s) ? uint8_t(s[i]) : ' ');
	};
	auto at = [](const std::vector<std::pair<uint32_t, Bytes>> &sets, uint32_t addr) -> const Bytes * {
		for (const auto &[a, data] : sets)
			if (a == addr)
				return &data;
		return nullptr;
	};
	const map::Map *fa = map::find("fantom");
	CHECK(fa && fa->high_first && fa->areas_of("User Multitimbre") == 16 && fa->areas_of("User Rhythm") == 16
		&& fa->areas_of("Temporary Patch") == 33);
	const map::Area *perf = fa ? fa->area("User Performance", 63) : nullptr;
	CHECK(perf && perf->count == 6 + 16 * 3 + 1);
	if (!perf)
		return;
	// the three MFX are together in the record, the second two after the reverb in the map
	CHECK(fa->entries[perf->first + 4].addr == perf->addr + (8 << 7) && fa->entries[perf->first + 4].order == 2
		&& fa->entries[perf->first + 2].order == 4);
	const size_t user = 0x1A0400, perfs = user + 0x190, multis = user + 0x11B10, patches = user + 0x156B0, sets = user + 0x27EB0;
	Bytes mem(0x200400, 0xFF);
	const size_t p = perfs + 63 * 1126, m = multis + 15 * 954, pa = patches + 2 * 592, rs = sets + 15 * 10824;
	std::fill(mem.begin() + p, mem.begin() + p + 1126, 0), std::fill(mem.begin() + m, mem.begin() + m + 954, 0);
	std::fill(mem.begin() + pa, mem.begin() + pa + 592, 0), std::fill(mem.begin() + rs, mem.begin() + rs + 10824, 0);
	name(mem, p, "Piano & Str"), name(mem, m, "GM2 Template"), name(mem, pa, "GlobalWarmup"), name(mem, rs, "OrchestraKit");
	put(mem, p + 122, 0, 7, 5);                      // the second MFX's type, after the first's 78 bytes
	put(mem, p + 378 + 36 + 22, 0, 7, 70);           // part 2's decay time offset, on the byte after its XV part
	put(mem, p + 954 + 15 * 9 + 8, 0, 1, 1);         // zone 16's first control switch, on its last byte
	put(mem, p + 1098, 1, 7, 99);                    // the controller's second parameter
	put(mem, m + 378, 2, 4, 11);                      // multitimbre part 1's channel, after two bits
	put(mem, pa + 574, 1, 7, 33);                    // the patch's controller, after its four tones
	put(mem, rs + 10808, 1, 7, 44);                  // the rhythm set's, after its 88 keys
	syfs_io io = io_of(mem);
	syfs_image *fs = syfs_open_as(&io, SYFS_FORMAT_FANTOM, SYFS_MEDIUM_MEMORY, nullptr, 0);
	CHECK(fs && format_of(fs) == SYFS_FORMAT_FANTOM && count_of(fs) == 4);
	if (!fs)
		return;
	const size_t ip = find(fs, "Piano & Str"), im = find(fs, "GM2 Template"), ia = find(fs, "GlobalWarmup"), ir = find(fs, "OrchestraKit");
	CHECK(entry_at(fs, ip)->number == 63 && entry_at(fs, im)->number == 15 && entry_at(fs, im)->kind == SYFS_TYPE_MULTI
		&& entry_at(fs, ia)->number == 2 && entry_at(fs, ir)->number == 15);
	auto got = xv_sets_of(syx_of(fs, ip));
	const uint32_t base = 0x20u << 21 | 63 << 14;
	const Bytes *second = at(got, base + (8 << 7)), *part = at(got, base + (33 << 7)), *zone = at(got, base + (95 << 7)),
		*ctl = at(got, base + (96 << 7));
	CHECK(second && (*second)[0] == 5 && at(got, base + (2 << 7)) && (*at(got, base + (2 << 7)))[0] == 0);
	CHECK(part && part->size() == 49 && (*part)[33] == 70 && (*part)[34] == 0 && zone && (*zone)[14] == 1 && (*zone)[15] == 0
		&& ctl && (*ctl)[1] == 99);
	got = xv_sets_of(syx_of(fs, im));
	CHECK(at(got, (0x50u << 21 | 15 << 14) + (32 << 7)) && (*at(got, (0x50u << 21 | 15 << 14) + (32 << 7)))[0] == 11);
	got = xv_sets_of(syx_of(fs, ia));
	CHECK(got.size() == 15 && got[0].first == (0x30u << 21 | 2 << 14) && got[14].first == got[0].first + (96 << 7)
		&& got[14].second.size() == 28 && got[14].second[1] == 33);
	got = xv_sets_of(syx_of(fs, ir));
	CHECK(!got.empty() && got.back().second.size() == 26 && got.back().second[1] == 44);

	// its .SVD: chunks of its own model and of the XV-5080's, a patch in the XV-5080's 938 bytes with its controller
	// after; the same Data Sets as the memory's
	Bytes svd(0x10 + 4 * 16, 0);
	svd[1] = uint8_t(svd.size() - 2);
	memcpy(&svd[2], "SVD1", 4);
	const std::tuple<const char *, const char *, size_t, uint32_t, uint32_t> chunks[] = { { "PRFa", "NEXY", perfs, 1126, 64 },
		{ "MLTa", "NEXY", multis, 954, 16 }, { "PATa", "XV88", patches, 592, 128 }, { "RHYa", "XV88", sets, 10824, 16 } };
	size_t entry = 16;
	for (auto [tag, model, from, stride, count] : chunks) {
		const bool patch = stride == 592;
		const uint32_t size = patch ? 956 : stride;
		memcpy(&svd[entry], tag, 4), memcpy(&svd[entry + 4], model, 4);
		const uint32_t head[4] = { uint32_t(svd.size()), 16 + count * size, count, size };
		for (int w = 0; w < 2; w++)
			for (int k = 0; k < 4; k++)
				svd[entry + 8 + 4 * w + k] = uint8_t(head[w] >> (24 - 8 * k));
		for (int w = 2; w < 4; w++)
			for (int k = 0; k < 4; k++)
				svd.push_back(uint8_t(head[w] >> (24 - 8 * k)));
		svd.insert(svd.end(), { 0, 0, 0, 16, 0, 0, 0, 0 });
		for (uint32_t k = 0; k < count; k++) {
			const auto rec = mem.begin() + from + k * stride;
			svd.insert(svd.end(), rec, rec + (patch ? 574 : stride));
			if (patch) {
				svd.insert(svd.end(), 364, 0);
				svd.insert(svd.end(), rec + 574, rec + 592);
			}
		}
		entry += 16;
	}
	syfs_io sio = io_of(svd);
	syfs_key medium = SYFS_NONE;
	CHECK(probe(&sio, &medium) == SYFS_FORMAT_FANTOM && medium == SYFS_MEDIUM_FILE);
	syfs_image *sv = syfs_open(&sio, nullptr, 0);
	CHECK(sv && count_of(sv) == 4);
	if (sv) {
		CHECK(syx_of(sv, find(sv, "Piano & Str")) == syx_of(fs, ip) && syx_of(sv, find(sv, "GM2 Template")) == syx_of(fs, im)
			&& syx_of(sv, find(sv, "GlobalWarmup")) == syx_of(fs, ia) && syx_of(sv, find(sv, "OrchestraKit")) == syx_of(fs, ir));
		close_image(sv);
	}
	// a file of its messages: the Fantom's where it holds what the XVs' maps have not (the patch's controller, a
	// multitimbre), an XV's where it does not
	Bytes file = syx_of(fs, ia), multi = syx_of(fs, im);
	syfs_io fio = io_of(file);
	syfs_image *sx = syfs_open(&fio, nullptr, 0);
	CHECK(sx && count_of(sx) == 2 && entry_at(sx, find(sx, "GlobalWarmup"))->format == SYFS_FORMAT_FANTOM
		&& syx_of(sx, find(sx, "GlobalWarmup")) == file);
	if (sx)
		close_image(sx);
	syfs_io mio = io_of(multi);
	sx = syfs_open(&mio, nullptr, 0);
	CHECK(sx && count_of(sx) == 2 && entry_at(sx, find(sx, "GM2 Template"))->kind == SYFS_TYPE_MULTI
		&& (names(sx, SYFS_TYPE_BANK) == std::vector<std::string> { "User Multitimbre" }));
	if (sx)
		close_image(sx);
	close_image(fs);
}

// the JV-880's records: each parameter in a field of a byte of its own block, a signed value kept less 64
static void test_syx_jv880()
{
	namespace map = syfs::map;
	const map::Map *jv = map::find("jv880");
	CHECK(jv && jv->model_id_size == 1 && jv->model_id[0] == 0x46 && jv->areas_of("User Patch") == 64
		&& jv->areas_of("Card Performance") == 16 && jv->areas_of("User Rhythm") == 1);
	const map::Area *user = jv ? jv->area("User Patch", 5) : nullptr;
	CHECK(user && user->count == 5 && jv->blocks[jv->entries[user->first].block].packed == 26
		&& jv->blocks[jv->entries[user->first + 1].block].packed == 84 && jv->blocks[jv->entries[user->first].block].places);
	if (!user)
		return;
	{
		// a patch: the common's 26 bytes and four tones of 84; packed again it is the record's fields
		Bytes rec(362, 0);
		memcpy(&rec[0], "JV Strings  ", 12);
		rec[12] = 0x80 | 2 << 3 | 5;                 // velocity switch, chorus type, reverb type in one byte
		rec[23] = uint8_t(-12) & 0x7F;               // bend range down, signed
		rec[26 + 84 + 1] = 200, rec[26 + 84] = 0x80 | 1;   // tone 2's wave number, in a byte; its switch and group
		rec[26 + 3 * 84 + 5] = 0xA3;                 // tone 4's first two modulation destinations, a nibble each
		rec[26 + 3 * 84 + 7] = uint8_t(-1) & 0x7F;   // and the first's sense
		std::vector<std::vector<uint8_t>> units;
		CHECK(map::unpack(*jv, *user, rec.data(), rec.size(), units) && units.size() == 5 && units[0].size() == 34
			&& units[4].size() == 116 && !map::unpack(*jv, *user, rec.data(), 361, units));
		CHECK(!memcmp(units[0].data(), "JV Strings  ", 12) && units[0][12] == 1 && units[0][13] == 5 && units[0][17] == 2
			&& units[0][26] == 52 && units[2][0] == 1 && units[2][1] == 12 && units[2][2] == 8 && units[2][3] == 1
			&& units[4][10] == 3 && units[4][12] == 10 && units[4][11] == 63);
		Bytes back;
		CHECK(map::pack(*jv, *user, units, back) && back == rec);
	}
	// a DATA card and a battery memory: the same records, a card's lists 0x90 lower; the rhythm set has no name
	Bytes card(0x8000, 0), mem(0x8000, 0);
	memcpy(&card[0], "Roland JV-80D\xBBP\xAF", 16);
	for (Bytes *b : { &card, &mem }) {
		const size_t shift = b == &mem ? 0x90 : 0;
		memcpy(&(*b)[0x20 + shift + 15 * 204], "TUTORIAL    ", 12);
		(*b)[0x20 + shift + 15 * 204 + 28 + 7 * 22 + 16] = 77;          // the last part's patch number
		memcpy(&(*b)[0xCE0 + shift + 63 * 362], "REVERSE MAD ", 12);
		(*b)[0xCE0 + shift + 63 * 362 + 21] = 101;                     // the patch's level
		(*b)[0x6760 + shift + 60 * 44 + 30] = 99;                      // the last key's level
	}
	syfs_io cio = io_of(card), mio = io_of(mem);
	syfs_image *c = syfs_open(&cio, nullptr, 0), *m = syfs_open_as(&mio, SYFS_FORMAT_JV80, SYFS_MEDIUM_MEMORY, nullptr, 0);
	CHECK(c && m && format_of(m) == SYFS_FORMAT_JV80 && count_of(m) == 3);
	if (c && m) {
		const uint32_t places[2][3] = { { 2u << 21 | 15 << 14 | 0x10 << 7, 2u << 21 | 127 << 14 | 0x20 << 7, 2u << 21 | 127 << 14 | 0x40 << 7 },
			{ 1u << 21 | 15 << 14 | 0x10 << 7, 1u << 21 | 127 << 14 | 0x20 << 7, 1u << 21 | 127 << 14 | 0x40 << 7 } };
		int n = 0;
		for (syfs_image *im : { c, m }) {
			const size_t perf = find(im, "TUTORIAL"), patch = find(im, "REVERSE MAD");
			size_t set = NONE;
			for (size_t i = 0; i < count_of(im); i++)
				if (entry_at(im, i)->kind == SYFS_TYPE_RHYTHM_SET)
					set = i;
			CHECK(perf != NONE && patch != NONE && set != NONE && entry_at(im, patch)->number == 63);
			auto got = data_sets_of(syx_of(im, perf), 0x10, 0x46);
			CHECK(got.size() == 9 && got[0].first == places[n][0] && got[0].second.size() == 31 && got[8].second.size() == 35
				&& got[8].second[23] == 4 && got[8].second[24] == 13);
			got = data_sets_of(syx_of(im, patch), 0x10, 0x46);
			CHECK(got.size() == 5 && got[0].first == places[n][1] && got[0].second[24] == 101 && got[4].second.size() == 116);
			got = data_sets_of(syx_of(im, set), 0x10, 0x46);
			CHECK(got.size() == 61 && got[0].first == places[n][2] && got[60].second.size() == 52 && got[60].second[36] == 99);
			n++;
		}
		// a file of its messages: under banks, the rhythm set named as its bank
		Bytes file = syx_of(m, find(m, "REVERSE MAD")), set = syx_of(m, 80 - 80 + count_of(m) - 1);
		file.insert(file.end(), set.begin(), set.end());
		syfs_io fio = io_of(file);
		syfs_image *sx = syfs_open(&fio, nullptr, 0);
		CHECK(sx && count_of(sx) == 4 && entry_at(sx, find(sx, "REVERSE MAD"))->format == SYFS_FORMAT_JV80
			&& find(sx, "User Rhythm", SYFS_TYPE_RHYTHM_SET) != NONE && syx_of(sx, find(sx, "REVERSE MAD")) == syx_of(m, find(m, "REVERSE MAD")));
		if (sx)
			close_image(sx);
	}
	if (c)
		close_image(c);
	if (m)
		close_image(m);
}

int main()
{
	test_keys();
	test_maps();
	Bytes s760 = s700_image(" S-760 System Disk    Ver. 2.24");
	test_s700(s760, SYFS_FORMAT_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(raw_of(s760), SYFS_FORMAT_S760, "S-760 System Disk    Ver. 2.24");
	test_s700(s700_image(" SYS-772 HardDisk Sys Ver. 2.25"), SYFS_FORMAT_S770, "SYS-772 HardDisk Sys Ver. 2.25");
	test_akai();
	test_s550();
	test_s700_floppy();
	test_syx();
	test_syx_jv();
	test_syx_xv();
	test_syx_fantom();
	test_syx_jv880();
	test_s550_floppy();
	test_boards();
	test_data_cards();
	test_akai_floppy();
	test_s900();
	test_iso();
	test_cue();
	test_encodings();
	test_fat();
	test_format_fat();
	test_format_s700();
	test_fat_written();
	test_s700_taken();
	test_smartmedia();
	test_vhd();
	test_vhd_written();
	test_vhd_shared();
	test_others();
	if (failures)
		fprintf(stderr, "%d failed\n", failures);
	else
		printf("all passed\n");
	return failures != 0;
}
