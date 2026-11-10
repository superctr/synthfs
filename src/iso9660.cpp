// Copyright 2026 Ian Karlsson
// A plain CD-ROM: its WAV and AIFF files (docs/formats.md, "ISO 9660")
#include "internal.hpp"

#include <algorithm>
#include <cctype>

namespace sfs::iso9660 {

constexpr uint64_t BLOCK = 2048;
constexpr int DEPTH = 8;
constexpr size_t ENTRIES = 1 << 16;
constexpr uint32_t DIR_BYTES = 1 << 22;

samplerfs_format probe(const Image &im)
{
	uint8_t pvd[6];
	return im.read(16 * BLOCK, pvd, sizeof pvd) && !memcmp(pvd, "\1CD001", 6) ? SAMPLERFS_ISO9660 : SAMPLERFS_UNKNOWN;
}

// a plain CD-ROM
class IsoMedium final : public Medium {
public:
	using Medium::Medium;
	void list() override;

private:
	bool walk(uint32_t extent, uint32_t bytes, size_t parent, int depth);
};

static bool audio(std::string_view name)
{
	size_t dot = name.rfind('.');
	if (dot == std::string_view::npos)
		return false;
	std::string ext;
	for (char c : name.substr(dot + 1))
		ext += char(tolower(uint8_t(c)));
	return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "aifc";
}

// a directory's audio files and the directories that hold some; whether it held any
bool IsoMedium::walk(uint32_t extent, uint32_t bytes, size_t parent, int depth)
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
			size_t di = add(SAMPLERFS_DIRECTORY, SAMPLERFS_ISO9660, number++, parent, name, 0, ext * BLOCK);
			if (walk(ext, size, di, depth + 1))
				any = true;
			else {
				entries.resize(mark);
				where.resize(mark);
				number--;
			}
		} else if (audio(name)) {
			add(SAMPLERFS_FILE, SAMPLERFS_ISO9660, number++, parent, name, size, ext * BLOCK);
			any = true;
		}
	}
	return any;
}

void IsoMedium::list()
{
	auto pvd = image.read(16 * BLOCK, BLOCK);
	if (pvd.empty())
		return;
	label = trimmed(std::string_view(reinterpret_cast<const char *>(&pvd[40]), 32));
	const uint8_t *root = &pvd[156];
	walk(le32(root + 2), le32(root + 10), SAMPLERFS_NONE, 0);
}

std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<IsoMedium>(io, format);
}

}
