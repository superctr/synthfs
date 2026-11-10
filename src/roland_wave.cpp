// Copyright 2026 Ian Karlsson
// Roland's wave expansion boards and PCM cards as dumped: SR-JV80 boards and SO-PCM1 cards, SRX boards; their banks of
// patches and rhythm sets and their waves, by name (docs/formats.md, "Roland wave expansion boards and cards")
#include "internal.hpp"

#include <map>

namespace sfs::roland_wave {

namespace {

constexpr uint64_t MB = 1024 * 1024;

// the ROM's address lines as the socket wires them, within each block of 2^bits bytes, and its data lines
constexpr uint8_t SRJV80_LINES[19] = { 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14 };
constexpr uint8_t SRX_LINES[18] = { 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15 };
constexpr uint8_t DATA_LINES[8] = { 2, 0, 4, 5, 7, 6, 3, 1 };

// the ROM as the socket presents it, read from the image as dumped a block at a time
class Socket {
public:
	Socket(const Image &im, bool srx) : im_(im), lines_(srx ? SRX_LINES : SRJV80_LINES), bits_(srx ? 18 : 19)
	{
		for (int v = 0; v < 256; v++) {
			uint8_t d = 0;
			for (int b = 0; b < 8; b++)
				if (v >> DATA_LINES[b] & 1)
					d |= uint8_t(1 << b);
			data_[v] = d;
		}
	}

	bool read(uint64_t at, uint8_t *out, size_t n) const
	{
		const uint64_t mask = (uint64_t(1) << bits_) - 1;
		for (size_t k = 0; k < n; k++, at++) {
			const std::vector<uint8_t> *block = raw(at & ~mask);
			if (!block)
				return false;
			uint32_t a = 0;
			for (int b = 0; b < bits_; b++)
				if (at >> b & 1)
					a |= 1u << lines_[b];
			out[k] = data_[(*block)[a]];
		}
		return true;
	}

	uint32_t be(uint64_t at, int n) const
	{
		uint8_t b[4] = {};
		if (!read(at, b, size_t(n)))
			return 0;
		uint32_t v = 0;
		for (int k = 0; k < n; k++)
			v = v << 8 | b[k];
		return v;
	}

	uint64_t size() const { return im_.size(); }

private:
	const std::vector<uint8_t> *raw(uint64_t base) const
	{
		auto it = blocks_.find(base);
		if (it == blocks_.end()) {
			auto v = im_.read(base, size_t(1) << bits_);
			if (v.empty())
				return nullptr;
			it = blocks_.emplace(base, std::move(v)).first;
		}
		return &it->second;
	}

	const Image &im_;
	const uint8_t *lines_;
	int bits_;
	uint8_t data_[256];
	mutable std::map<uint64_t, std::vector<uint8_t>> blocks_;
};

std::string plain(const uint8_t *p, size_t n)
{
	std::string s;
	for (size_t i = 0; i < n && p[i]; i++)
		s += p[i] >= 32 && p[i] < 127 ? char(p[i]) : ' ';
	return s;
}

// twelve characters of seven bits in the first eleven bytes: from bit 0 up (an SR-JV80's later shapes), or from the
// first byte's top bit down (an SRX's)
std::string packed(const uint8_t *p, bool from_top)
{
	uint8_t c[12];
	for (int i = 0; i < 12; i++) {
		uint32_t v = 0;
		for (int b = 0; b < 7; b++) {
			int n = 7 * i + b;
			if (from_top)
				v = v << 1 | (p[n / 8] >> (7 - n % 8) & 1);
			else
				v |= uint32_t(p[n / 8] >> (n % 8) & 1) << b;
		}
		c[i] = uint8_t(v);
	}
	return plain(c, 12);
}

bool srjv80(const uint8_t *h, uint64_t size)
{
	return !memcmp(h, "Roland JV80", 11) && h[0x0C] == 'O' && h[0x0E] == 'S'
		&& (size == 1 * MB || size == 2 * MB || size == 4 * MB || size == 8 * MB);
}

bool srx(const uint8_t *h, uint64_t size)
{
	return !memcmp(h, "Roland SRX", 10) && size == 32 * MB;
}

class RolandWaveMedium final : public Medium {
public:
	using Medium::Medium;
	void list() override;

private:
	void list_srjv80(const Socket &s);
	void list_srx(const Socket &s);
	size_t bank(std::string_view shape);
	uint32_t banks_ = 0;
};

size_t RolandWaveMedium::bank(std::string_view shape)
{
	size_t b = add(SAMPLERFS_BANK, format, banks_++, SAMPLERFS_NONE, "", 0, 0);
	tag(b, shape);
	return b;
}

void RolandWaveMedium::list()
{
	uint8_t h[32];
	if (!image.read(0, h, sizeof h))
		return;
	label = trimmed(plain(h + 0x10, 16));
	Socket s(image, format == SAMPLERFS_SRX);
	if (format == SAMPLERFS_SRX)
		list_srx(s);
	else
		list_srjv80(s);
}

// Each 1 MB block's header: per slot n a type byte at +0x50 + n, a count at +0x60 + 2n and an address at +0x80 + 4n
// (big-endian); slot 1 the waves (60 bytes, the name first), slot 3 the patches, slot 4 the rhythm sets, each in the
// shape the type gives
void RolandWaveMedium::list_srjv80(const Socket &s)
{
	struct Shape { int slot; uint8_t type; samplerfs_kind kind; uint32_t size; int name; };  // name: 0 none, 1 plain, 2 packed
	static constexpr Shape shapes[] = {
		{ 1, 1, SAMPLERFS_WAVE, 60, 1 },
		{ 3, 2, SAMPLERFS_PATCH, 362, 1 },
		{ 3, 5, SAMPLERFS_PATCH, 401, 2 },
		{ 4, 2, SAMPLERFS_RHYTHM_SET, 2684, 0 },
		{ 4, 4, SAMPLERFS_RHYTHM_SET, 2699, 2 },
	};
	std::vector<std::pair<int, uint32_t>> seen;
	uint32_t waves = 0;
	for (uint64_t base = 0; base + 0x100 <= s.size(); base += MB) {
		uint8_t h[0xA0];
		if (!s.read(base, h, sizeof h))
			return;
		for (const Shape &sh : shapes) {
			if (h[0x50 + sh.slot] != sh.type)
				continue;
			uint32_t count = be16(h + 0x60 + 2 * sh.slot), at = be32(h + 0x80 + 4 * sh.slot);
			if (!count || at + uint64_t(count) * sh.size > s.size())
				continue;
			if (std::find(seen.begin(), seen.end(), std::pair(sh.slot, at)) != seen.end())
				continue;
			seen.emplace_back(sh.slot, at);
			size_t parent = sh.kind == SAMPLERFS_WAVE ? SAMPLERFS_NONE : bank(std::to_string(sh.type));
			for (uint32_t k = 0; k < count; k++) {
				uint8_t rec[12] = {};
				uint64_t r = at + uint64_t(k) * sh.size;
				if (sh.name && !s.read(r, rec, sh.name == 1 ? 12 : 11))
					return;
				std::string name = sh.name == 1 ? plain(rec, 12) : sh.name == 2 ? packed(rec, false) : "";
				add(sh.kind, format, sh.kind == SAMPLERFS_WAVE ? waves++ : k, parent, name, 0, r);
			}
		}
	}
}

// The directory: its count at +0x42, sixteen-byte entries from +0x80, each a class and a type (halfwords), the offset
// of its table and its length; a table's head a count, the offset of its records from the head and their stride.
// Class 2 the waves, each record the offset of its definition from the head (a long) and its length, the definition
// starting with the name; class 4 a bank of patches, class 5 of rhythm sets, each record starting with its name
void RolandWaveMedium::list_srx(const Socket &s)
{
	uint32_t entries_n = std::min<uint32_t>(s.be(0x42, 2), 64);
	for (uint32_t e = 0; e < entries_n; e++) {
		uint64_t d = 0x80 + 16 * e;
		uint32_t cls = s.be(d, 2), type = s.be(d + 2, 2), at = s.be(d + 4, 4), len = s.be(d + 8, 4);
		if (uint64_t(at) + len > s.size() || len < 12)
			continue;
		uint32_t count = s.be(at, 2), records = s.be(at + 4, 4), stride = s.be(at + 8, 4);
		if (uint64_t(records) + uint64_t(count) * stride > len)
			continue;
		if (cls == 2 && stride == 8) {
			for (uint32_t k = 0; k < count; k++) {
				uint64_t r = uint64_t(at) + records + 8 * k;
				uint32_t def = s.be(r, 4);
				uint8_t name[12];
				if (uint64_t(def) + sizeof name > len || !s.read(at + def, name, sizeof name))
					continue;
				add(SAMPLERFS_WAVE, format, k, SAMPLERFS_NONE, plain(name, sizeof name), 0, at + def);
			}
		} else if ((cls == 4 || cls == 5) && stride >= 11) {
			samplerfs_kind kind = cls == 4 ? SAMPLERFS_PATCH : SAMPLERFS_RHYTHM_SET;
			size_t parent = bank(std::to_string(cls) + ":" + std::to_string(type));
			for (uint32_t k = 0; k < count; k++) {
				uint64_t r = uint64_t(at) + records + uint64_t(k) * stride;
				uint8_t rec[11];
				if (!s.read(r, rec, sizeof rec))
					return;
				add(kind, format, k, parent, packed(rec, true), 0, r);
			}
		}
	}
}

}

samplerfs_format probe(const Image &im)
{
	uint8_t h[16];
	if (!im.read(0, h, sizeof h))
		return SAMPLERFS_UNKNOWN;
	if (srjv80(h, im.size()))
		return SAMPLERFS_SRJV80;
	if (srx(h, im.size()))
		return SAMPLERFS_SRX;
	return SAMPLERFS_UNKNOWN;
}

// an SR-JV80-format ROM of 1 MB, or of 2 MB named SO-PCM1 or Card, is a card; any other a board
samplerfs_medium medium(const Image &im)
{
	uint8_t h[32];
	if (!im.read(0, h, sizeof h))
		return SAMPLERFS_MEDIUM_UNKNOWN;
	if (srx(h, im.size()))
		return SAMPLERFS_MEDIUM_BOARD;
	if (!srjv80(h, im.size()))
		return SAMPLERFS_MEDIUM_UNKNOWN;
	if (im.size() == 1 * MB || (im.size() == 2 * MB && (!memcmp(h + 0x10, "SO-PCM1", 7) || !memcmp(h + 0x10, "Card", 4))))
		return SAMPLERFS_MEDIUM_CARD;
	return SAMPLERFS_MEDIUM_BOARD;
}

std::unique_ptr<Medium> open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<RolandWaveMedium>(io, format);
}

}
