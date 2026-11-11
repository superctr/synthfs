// Copyright 2026 Ian Karlsson
// The AKAI S900 and S950 floppy: its files, programs' samples and samples' headers (docs/formats.md, "AKAI S900")
#include "akai.hpp"

namespace syfs::akai {

namespace {

constexpr uint64_t BLOCK = 1024, FAT = 0x600, IMAGE_MIN = 700 * 1024, IMAGE_MAX = 1600 * 1024;
constexpr uint32_t FILES = 64, ENTRY = 24, NAME = 10, SYSTEM_BLOCKS = 4;
constexpr uint32_t HEADER = 0x3C, PROGRAM_HEADER = 0x26, KEYGROUP = 70;

std::string name_of(const uint8_t *p)
{
	return trimmed(std::string_view(reinterpret_cast<const char *>(p), NAME));
}

syfs_key kind_of(uint8_t type)
{
	return type == 'S' ? SYFS_TYPE_SAMPLE : type == 'P' ? SYFS_TYPE_PROGRAM : SYFS_TYPE_OTHER;
}

}

void S900::list()
{
	auto dir = image.read(0, ENTRY * FILES);
	for (uint32_t k = 0; k < FILES && !dir.empty(); k++) {
		const uint8_t *f = &dir[ENTRY * k];
		if (!f[16])
			continue;
		size_t i = add(kind_of(f[16]), format, k, syfs::NONE, name_of(f), le24(f + 17), le16(f + 20) * BLOCK);
		tag(i, std::string(1, char(f[16])));
	}
}

// file i's first max bytes along the FAT: a halfword a block, 0x8000 and above the end
std::vector<uint8_t> S900::read_file(size_t i, uint64_t max) const
{
	uint64_t blocks = (image.size() + BLOCK - 1) / BLOCK, left = std::min(max, entries[i].bytes), b = where[i] / BLOCK;
	std::vector<uint8_t> out;
	while (left && b >= SYSTEM_BLOCKS && b < blocks) {
		auto chunk = image.read(b * BLOCK, size_t(std::min(left, BLOCK)));
		if (chunk.empty())
			break;
		out.insert(out.end(), chunk.begin(), chunk.end());
		left -= chunk.size();
		uint8_t next[2];
		if (!left || !image.read(FAT + 2 * b, next, 2))
			break;
		b = le16(next);
	}
	return out;
}

size_t S900::named(syfs_key kind, const std::string &name) const
{
	for (size_t j = 0; j < entries.size(); j++)
		if (entries[j].kind == kind && name == entries[j].name)
			return j;
	return syfs::NONE;
}

// a program: its keygroups' count at +0x17, the keygroups from +0x26, 70 bytes each, each naming two samples, at +0x18
// and +0x2E
std::vector<size_t> S900::refs(size_t i) const
{
	if (entries[i].kind != SYFS_TYPE_PROGRAM)
		return {};
	auto p = read_file(i, PROGRAM_HEADER + 255 * KEYGROUP);
	if (p.size() < PROGRAM_HEADER)
		return {};
	std::vector<size_t> out;
	for (size_t k = 0; k < p[0x17] && PROGRAM_HEADER + (k + 1) * KEYGROUP <= p.size(); k++)
		for (size_t at : { 0x18, 0x2E }) {
			std::string name = name_of(&p[PROGRAM_HEADER + k * KEYGROUP + at]);
			size_t j = name.empty() ? syfs::NONE : named(SYFS_TYPE_SAMPLE, name);
			if (j != syfs::NONE && std::find(out.begin(), out.end(), j) == out.end())
				out.push_back(j);
		}
	return out;
}

// A program's key: the first keygroup whose keys (highest at +0, lowest at +1) hold it, and its first sample found
size_t S900::sample_at(size_t i, int key) const
{
	if (entries[i].kind != SYFS_TYPE_PROGRAM)
		return Medium::sample_at(i, key);
	auto p = read_file(i, PROGRAM_HEADER + 255 * KEYGROUP);
	for (size_t k = 0; p.size() >= PROGRAM_HEADER && k < p[0x17] && PROGRAM_HEADER + (k + 1) * KEYGROUP <= p.size(); k++) {
		const uint8_t *kg = &p[PROGRAM_HEADER + k * KEYGROUP];
		if (key < kg[1] || key > kg[0])
			continue;
		for (size_t at : { 0x18, 0x2E }) {
			std::string name = name_of(kg + at);
			size_t s = name.empty() ? syfs::NONE : named(SYFS_TYPE_SAMPLE, name);
			if (s != syfs::NONE)
				return s;
		}
	}
	return syfs::NONE;
}

// a sample's header (60 bytes, its 12-bit wave packed after it): the frames at +0x10, the rate at +0x14, the pitch in
// sixteenths of a semitone at +0x16, the loop mode at +0x1A (O one shot, L loop, A alternating), the end at +0x1C, the
// start at +0x20, the loop's length at +0x24, the loop ending at the end
bool S900::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind != SYFS_TYPE_SAMPLE)
		return false;
	auto h = read_file(i, HEADER);
	if (h.size() < HEADER)
		return false;
	out.frames = le32(&h[0x10]);
	out.rate = le16(&h[0x14]);
	out.bits = 12;
	uint32_t key = (le16(&h[0x16]) + 8) / 16;
	out.key = key <= 127 ? int32_t(key) : -1;
	out.mode = h[0x1A];
	out.end = le32(&h[0x1C]);
	out.start = le32(&h[0x20]);
	uint32_t length = le32(&h[0x24]);
	if ((out.mode == 'L' || out.mode == 'A') && length && length <= out.end) {
		out.loop = out.mode == 'L' ? SYFS_LOOP_FORWARD : SYFS_LOOP_ALTERNATE;
		out.loop_start = out.end - length;
		out.loop_end = out.end;
	}
	return true;
}

// the wave after the header in two parts: the first half's values as words, each its value's top eight bits over the
// low four bits of it (the byte's high nibble) and of its partner in the second half (the low nibble); then the second
// half's top eight bits, a byte each
size_t S900::wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const
{
	uint64_t half = (s.frames + 1) / 2;
	auto f = read_file(i, HEADER + half + s.frames);
	if (f.size() <= HEADER)
		return 0;
	const uint8_t *d = &f[HEADER];
	uint64_t have = f.size() - HEADER;
	size_t done = 0;
	for (uint64_t j = first; done < frames; j++, done++) {
		bool second = j >= half;
		uint64_t word = 2 * (second ? j - half : j), top = second ? 2 * half + (j - half) : word + 1;
		if (top >= have)
			break;
		uint8_t low = second ? d[word] & 0x0F : d[word] >> 4;
		out[done] = int16_t(d[top] << 8 | low << 4);
	}
	return done;
}

// whole 1024-byte blocks, the first four's FAT halfwords zero, and a directory of printable names, upper-case types
// and starts past the system's blocks, at least one file
syfs_key S900::probe(const Image &im)
{
	if (im.size() < IMAGE_MIN || im.size() > IMAGE_MAX || im.size() % BLOCK)
		return SYFS_NONE;
	uint8_t fat[2 * SYSTEM_BLOCKS], dir[ENTRY * FILES];
	if (!im.read(FAT, fat, sizeof fat) || !im.read(0, dir, sizeof dir))
		return SYFS_NONE;
	for (uint8_t b : fat)
		if (b)
			return SYFS_NONE;
	uint32_t files = 0;
	for (uint32_t k = 0; k < FILES; k++) {
		const uint8_t *f = &dir[ENTRY * k];
		if (!f[16])
			continue;
		if (f[16] < 'A' || f[16] > 'Z' || le16(f + 20) < SYSTEM_BLOCKS || le16(f + 20) >= im.size() / BLOCK)
			return SYFS_NONE;
		for (uint32_t c = 0; c < NAME; c++)
			if (f[c] < 32 || f[c] > 126)
				return SYFS_NONE;
		files++;
	}
	return files ? SYFS_FORMAT_AKAI_S900 : SYFS_NONE;
}

std::unique_ptr<Medium> S900::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<S900>(io, format);
}

}
