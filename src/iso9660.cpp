// Copyright 2026 Ian Karlsson
// A plain CD-ROM: its WAV and AIFF files (docs/formats.md, "ISO 9660")
#include "iso9660.hpp"

#include "audio.hpp"

#include <algorithm>

namespace syfs {

namespace {

constexpr uint64_t BLOCK = 2048;
constexpr int DEPTH = 8;
constexpr size_t ENTRIES = 1 << 16;
constexpr uint32_t DIR_BYTES = 1 << 22;

}

syfs_key Iso9660::probe(const Image &im)
{
	uint8_t pvd[6];
	return im.read(16 * BLOCK, pvd, sizeof pvd) && !memcmp(pvd, "\1CD001", 6) ? SYFS_FORMAT_ISO9660 : SYFS_NONE;
}

// a directory's audio files and the directories that hold some; whether it held any
bool Iso9660::walk(uint32_t extent, uint32_t bytes, size_t parent, int depth)
{
	if (depth > DEPTH || entries.size() >= ENTRIES)
		return false;
	auto dir = image.read(extent * BLOCK, std::min(bytes, DIR_BYTES));
	bool any = false;
	uint32_t number = 0;
	for (size_t at = 0; at < dir.size();) {
		uint8_t len = dir[at];
		if (!len) {
			at = (at / BLOCK + 1) * BLOCK;
			continue;
		}
		if (at + len > dir.size() || len < 34)
			break;
		const uint8_t *r = &dir[at];
		at += len;
		uint8_t nlen = r[32];
		if (33 + nlen > len || (nlen == 1 && r[33] <= 1))
			continue;
		std::string_view name(reinterpret_cast<const char *>(r + 33), nlen);
		if (size_t semi = name.find(';'); semi != std::string_view::npos)
			name = name.substr(0, semi);
		if (!name.empty() && name.back() == '.')
			name.remove_suffix(1);
		uint32_t ext = le32(r + 2), size = le32(r + 10);
		if (r[25] & 2) {
			size_t mark = entries.size();
			size_t di = add(SYFS_TYPE_DIRECTORY, SYFS_FORMAT_ISO9660, number++, parent, name, 0, ext * BLOCK);
			if (walk(ext, size, di, depth + 1))
				any = true;
			else {
				entries.resize(mark);
				where.resize(mark);
				number--;
			}
		} else if (audio_name(name)) {
			add(SYFS_TYPE_FILE, SYFS_FORMAT_ISO9660, number++, parent, name, size, ext * BLOCK);
			any = true;
		}
	}
	return any;
}

void Iso9660::list()
{
	auto pvd = image.read(16 * BLOCK, BLOCK);
	if (pvd.empty())
		return;
	label = trimmed(std::string_view(reinterpret_cast<const char *>(&pvd[40]), 32));
	const uint8_t *root = &pvd[156];
	walk(le32(root + 2), le32(root + 10), syfs::NONE, 0);
}

bool Iso9660::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind != SYFS_TYPE_FILE)
		return false;
	uint64_t at = where[i];
	return audio_sample([&](uint64_t offset, void *buf, size_t n) { return image.read(at + offset, buf, n); },
		entries[i].bytes, out);
}

size_t Iso9660::wave(size_t i, const syfs::Sample &, uint64_t first, int16_t *out, size_t frames) const
{
	uint64_t at = where[i];
	return audio_wave([&](uint64_t offset, void *buf, size_t n) { return image.read(at + offset, buf, n); },
		entries[i].bytes, first, out, frames);
}

std::unique_ptr<Medium> Iso9660::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Iso9660>(io, format);
}

}
