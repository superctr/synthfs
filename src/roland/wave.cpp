// Copyright 2026 Ian Karlsson
// Roland's wave expansion boards and PCM cards as dumped: SR-JV80 boards and SO-PCM1 cards, SRX boards; their banks of
// patches and rhythm sets, their waves and each wave's samples by key (docs/formats.md, "Roland wave expansion boards
// and cards")
#include "roland.hpp"

#include <cmath>

namespace sfs::roland {

namespace {

constexpr uint64_t MB = 1024 * 1024;

// the ROM's address lines as the socket wires them, within each block of 2^bits bytes, and its data lines
constexpr uint8_t SRJV80_LINES[19] = { 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14 };
constexpr uint8_t SRX_LINES[18] = { 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15 };
constexpr uint8_t DATA_LINES[8] = { 2, 0, 4, 5, 7, 6, 3, 1 };

}

// the ROM as the socket presents it, read from the image as dumped a block at a time
class Socket {
public:
	Socket(const Image &im, bool srx) : im_(im), bits_(srx ? 18 : 19)
	{
		const uint8_t *lines = srx ? SRX_LINES : SRJV80_LINES;
		for (uint32_t v = 0; v < 1024; v++)
			for (int b = 0; b < 10; b++) {
				if (v >> b & 1)
					low_[v] |= 1u << lines[b];
				if (b + 10 < bits_ && v >> b & 1)
					high_[v] |= 1u << lines[b + 10];
			}
		for (int v = 0; v < 256; v++)
			for (int b = 0; b < 8; b++)
				if (v >> DATA_LINES[b] & 1)
					data_[v] |= uint8_t(1 << b);
	}

	bool read(uint64_t at, uint8_t *out, size_t n) const
	{
		const uint64_t mask = (uint64_t(1) << bits_) - 1;
		for (size_t k = 0; k < n;) {
			const std::vector<uint8_t> *block = raw(at & ~mask);
			if (!block)
				return false;
			size_t run = size_t(std::min<uint64_t>(n - k, mask + 1 - (at & mask)));
			for (size_t r = 0; r < run; r++, k++, at++)
				out[k] = data_[(*block)[low_[at & 1023] | high_[(at & mask) >> 10]]];
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
			if (blocks_.size() >= 16)
				blocks_.clear();
			it = blocks_.emplace(base, std::move(v)).first;
		}
		return &it->second;
	}

	const Image &im_;
	int bits_;
	uint32_t low_[1024] = {}, high_[1024] = {};
	uint8_t data_[256] = {};
	mutable std::map<uint64_t, std::vector<uint8_t>> blocks_;
};

namespace {

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

// a sample as a wave's split points the tone generator at: byte addresses on the board, the loop's mode, the
// original key, the fine tune in thousandths of a semitone from 1024 and the rate the sample was recorded at
struct Record {
	uint32_t start, loop, end;
	uint8_t flags, key;
	uint16_t fine;
	uint32_t rate;
};

constexpr uint32_t SRJV80_SAMPLE = 18, SRX_SAMPLE = 24, SRJV80_RATE = 32000;
// a sample record's flags: the loop played back and forth, played once, played from the end
constexpr uint8_t ALTERNATE = 1, ONCE = 2, REVERSE = 4;
// a split's sample number for keys that sound nothing
constexpr uint32_t SILENT = 0xFFFF;
constexpr uint64_t NOWHERE = ~uint64_t(0);

bool record_of(const Socket &s, bool srx, uint64_t at, Record &r)
{
	uint8_t b[SRX_SAMPLE];
	if (!s.read(at, b, srx ? SRX_SAMPLE : SRJV80_SAMPLE))
		return false;
	if (srx)
		r = { be32(b + 12), be32(b + 16), be32(b + 20), b[1], b[2], be16(b + 6), be16(b + 10) * 100u };
	else
		r = { be24(b + 1), be24(b + 4), be24(b + 7), b[12], b[13], be16(b + 14), SRJV80_RATE };
	if (!r.rate)
		r.rate = SRJV80_RATE;
	return r.start <= r.end && r.loop <= r.end && std::min(r.start, r.loop) >> 20 == r.end >> 20 && r.end < s.size()
		&& r.key < 128;
}

}

WaveRom::WaveRom(const samplerfs_io &io, samplerfs_format format) : Medium(io, format) {}

WaveRom::~WaveRom() = default;

size_t WaveRom::bank(std::string_view shape)
{
	size_t b = add(SAMPLERFS_BANK, format, banks_++, SAMPLERFS_NONE, "", 0, 0);
	tag(b, shape);
	return b;
}

void WaveRom::split(size_t wave, uint32_t number, uint8_t below, uint8_t top, uint64_t record)
{
	if (record == NOWHERE) {
		splits_[wave].emplace_back(top, SAMPLERFS_NONE);
		return;
	}
	Record r;
	uint64_t bytes = record_of(*socket_, format == SAMPLERFS_SRX, record, r) ? r.end - std::min(r.start, r.loop) + 1 : 0;
	size_t c = add(SAMPLERFS_SAMPLE, format, number, wave, entries[wave].name, bytes, record);
	tag(c, std::to_string(below) + "-" + std::to_string(top));
	splits_[wave].emplace_back(top, c);
}

size_t WaveRom::nearest(size_t wave, int key) const
{
	auto it = splits_.find(wave);
	if (it == splits_.end())
		return SAMPLERFS_NONE;
	size_t best = SAMPLERFS_NONE;
	int below = 0, far = 256;
	for (const auto &[top, c] : it->second) {
		int d = key < below ? below - key : key > top ? key - top : 0;
		if (c != SAMPLERFS_NONE && d < far)
			best = c, far = d;
		below = top + 1;
	}
	return best;
}

void WaveRom::list()
{
	uint8_t h[32];
	if (!image.read(0, h, sizeof h))
		return;
	label = trimmed(plain(h + 0x10, 16));
	socket_ = std::make_unique<Socket>(image, format == SAMPLERFS_SRX);
	if (format == SAMPLERFS_SRX)
		list_srx();
	else
		list_srjv80();
}

// Each 1 MB block's header: per slot n a type byte at +0x50 + n, a count at +0x60 + 2n and an address at +0x80 + 4n
// (big-endian); slot 0 the samples (18 bytes), slot 1 the waves (60 bytes: the name, sixteen top keys and sixteen
// sample numbers), slot 3 the patches, slot 4 the rhythm sets, each in the shape the type gives
void WaveRom::list_srjv80()
{
	const Socket &s = *socket_;
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
		uint32_t samples = h[0x50] == 2 ? be16(h + 0x60) : 0, samples_at = be32(h + 0x80);
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
				uint8_t rec[60] = {};
				uint64_t r = at + uint64_t(k) * sh.size;
				if (sh.name && !s.read(r, rec, sh.kind == SAMPLERFS_WAVE ? 60 : sh.name == 1 ? 12 : 11))
					return;
				std::string name = sh.name == 1 ? plain(rec, 12) : sh.name == 2 ? packed(rec, false) : "";
				size_t e = add(sh.kind, format, sh.kind == SAMPLERFS_WAVE ? waves++ : k, parent, name, 0, r);
				if (sh.kind != SAMPLERFS_WAVE)
					continue;
				uint8_t below = 0;
				for (uint32_t j = 0; j < 16; j++) {
					uint8_t top = rec[12 + j];
					uint32_t n = be16(rec + 28 + 2 * j);
					if ((n >= samples && n != SILENT) || top < below)
						break;
					split(e, j, below, top, n == SILENT ? NOWHERE : samples_at + uint64_t(n) * SRJV80_SAMPLE);
					if (top >= 127)
						break;
					below = uint8_t(top + 1);
				}
			}
		}
	}
}

// The directory: its count at +0x42, sixteen-byte entries from +0x80, each a class and a type (halfwords), the offset
// of its table and its length; a table's head a count, the offset of its records from the head and their stride.
// Class 1 the samples (24 bytes); class 2 the waves, each record the offset of its definition from the head (a long)
// and its length, the definition the name, four bytes, then a split in four bytes (its top key, a byte, the sample's
// number) to its end; class 4 a bank of patches, class 5 of rhythm sets, each record starting with its name
void WaveRom::list_srx()
{
	const Socket &s = *socket_;
	struct Table { uint32_t cls, type, at, len, count, records, stride; };
	std::vector<Table> tables;
	uint32_t entries_n = std::min<uint32_t>(s.be(0x42, 2), 64);
	for (uint32_t e = 0; e < entries_n; e++) {
		uint64_t d = 0x80 + 16 * e;
		Table t { s.be(d, 2), s.be(d + 2, 2), s.be(d + 4, 4), s.be(d + 8, 4), 0, 0, 0 };
		if (uint64_t(t.at) + t.len > s.size() || t.len < 12)
			continue;
		t.count = s.be(t.at, 2), t.records = s.be(t.at + 4, 4), t.stride = s.be(t.at + 8, 4);
		if (uint64_t(t.records) + uint64_t(t.count) * t.stride <= t.len)
			tables.push_back(t);
	}
	const Table *samples = nullptr;
	for (const Table &t : tables)
		if (t.cls == 1 && t.stride == SRX_SAMPLE)
			samples = &t;
	for (const Table &t : tables) {
		if (t.cls == 2 && t.stride == 8) {
			for (uint32_t k = 0; k < t.count; k++) {
				uint64_t r = uint64_t(t.at) + t.records + 8 * k;
				uint32_t def = s.be(r, 4), len = s.be(r + 4, 4);
				uint8_t name[12];
				if (uint64_t(def) + sizeof name > t.len || !s.read(t.at + def, name, sizeof name))
					continue;
				size_t e = add(SAMPLERFS_WAVE, format, k, SAMPLERFS_NONE, plain(name, sizeof name), 0, t.at + def);
				if (!samples || len < 16 || uint64_t(def) + len > t.len)
					continue;
				std::vector<uint8_t> sp((len - 16) & ~3u);
				if (!s.read(t.at + def + 16, sp.data(), sp.size()))
					continue;
				uint8_t below = 0;
				for (uint32_t j = 0; j < sp.size() / 4; j++) {
					uint8_t top = sp[4 * j];
					uint32_t n = be16(&sp[4 * j + 2]);
					if ((n >= samples->count && n != SILENT) || top < below)
						break;
					split(e, j, below, top, n == SILENT ? NOWHERE : uint64_t(samples->at) + samples->records + uint64_t(n) * SRX_SAMPLE);
					if (top >= 127)
						break;
					below = uint8_t(top + 1);
				}
			}
		} else if ((t.cls == 4 || t.cls == 5) && t.stride >= 11) {
			samplerfs_kind kind = t.cls == 4 ? SAMPLERFS_PATCH : SAMPLERFS_RHYTHM_SET;
			size_t parent = bank(std::to_string(t.cls) + ":" + std::to_string(t.type));
			for (uint32_t k = 0; k < t.count; k++) {
				uint64_t r = uint64_t(t.at) + t.records + uint64_t(k) * t.stride;
				uint8_t rec[11];
				if (!s.read(r, rec, sizeof rec))
					return;
				add(kind, format, k, parent, packed(rec, true), 0, r);
			}
		}
	}
}

std::vector<size_t> WaveRom::refs(size_t i) const
{
	std::vector<size_t> out;
	auto it = splits_.find(i);
	if (it != splits_.end())
		for (const auto &sp : it->second)
			if (sp.second != SAMPLERFS_NONE && std::find(out.begin(), out.end(), sp.second) == out.end())
				out.push_back(sp.second);
	return out;
}

// A split's sample: the rate the fine tune moves (the rate it sounds right at as its original key), the loop from
// its record; a wave answers with the split it plays at middle C, or the nearest that sounds
bool WaveRom::sample(size_t i, samplerfs_sample &out) const
{
	if (entries[i].kind == SAMPLERFS_WAVE)
		i = nearest(i, 60);
	if (i == SAMPLERFS_NONE || entries[i].kind != SAMPLERFS_SAMPLE || !socket_)
		return false;
	Record r;
	if (!record_of(*socket_, format == SAMPLERFS_SRX, where[i], r))
		return false;
	out.rate = uint32_t(std::lround(r.rate * std::exp2((int(r.fine) - 1024) / 12000.0)));
	out.key = r.key;
	const bool early = r.loop < r.start && !(r.flags & ONCE);
	out.frames = uint64_t(r.end) - r.start + 1 + (early ? r.end - r.loop + 1 : 0);
	out.end = out.frames;
	if (r.loop < r.end && !(r.flags & ONCE)) {
		out.loop = r.flags & ALTERNATE ? SAMPLERFS_LOOP_ALTERNATE : SAMPLERFS_LOOP_FORWARD;
		out.loop_start = early ? r.end - r.start + 1 : r.loop - r.start;
		out.loop_end = out.frames;
	}
	out.reverse = (r.flags & REVERSE) != 0;
	out.mode = r.flags;
	return true;
}

// The wave in the tone generators' code: one signed byte a sample, the delta to the next, scaled by a power of two
// from a table of four-bit shifts at the head of the sample's megabyte, summed from zero at the start into an 18-bit
// value whose top 16 bits are given.  A loop starting before the start is summed from there, the same zero kept, and
// given after the frames from the start, as it plays
size_t WaveRom::wave(size_t i, const samplerfs_sample &, uint64_t first, int16_t *out, size_t frames) const
{
	if (entries[i].kind == SAMPLERFS_WAVE)
		i = nearest(i, 60);
	if (i == SAMPLERFS_NONE)
		return 0;
	if (decoded_ != i) {
		Record r;
		decoded_ = SAMPLERFS_NONE;
		frames_.clear();
		if (!record_of(*socket_, format == SAMPLERFS_SRX, where[i], r))
			return 0;
		const uint64_t region = r.end & ~uint64_t(MB - 1);
		const uint32_t from = std::min(r.start, r.loop) & (MB - 1), start = r.start & (MB - 1), to = r.end & (MB - 1);
		std::vector<uint8_t> deltas(to - from + 1), shifts((to >> 5) - (from >> 5) + 1);
		if (!socket_->read(region + from, deltas.data(), deltas.size())
			|| !socket_->read(region + (from >> 5), shifts.data(), shifts.size()))
			return 0;
		std::vector<int32_t> sums(deltas.size());
		int32_t sum = 0;
		for (uint32_t a = from; a <= to; a++) {
			uint8_t t = shifts[(a >> 5) - (from >> 5)];
			int shift = a & 16 ? t >> 4 : t & 15;
			sum += shift > 10 ? 0 : int32_t(int8_t(deltas[a - from])) * (1 << shift);
			sums[a - from] = sum;
		}
		const int32_t zero = start > from ? sums[start - 1 - from] : 0;
		auto put = [&](uint32_t a, uint32_t b) {
			for (uint32_t k = a; k <= b; k++)
				frames_.push_back(int16_t((int32_t(uint32_t(sums[k - from] - zero) << 14) >> 14) >> 2));
		};
		put(start, to);
		if (r.loop < r.start && !(r.flags & ONCE))
			put(from, to);
		decoded_ = i;
	}
	if (first >= frames_.size())
		return 0;
	size_t n = size_t(std::min<uint64_t>(frames, frames_.size() - first));
	std::copy_n(frames_.begin() + ptrdiff_t(first), n, out);
	return n;
}

samplerfs_format WaveRom::probe(const Image &im)
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
samplerfs_medium WaveRom::medium_of(const Image &im)
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

std::unique_ptr<Medium> WaveRom::open(const samplerfs_io &io, samplerfs_format format)
{
	return std::make_unique<WaveRom>(io, format);
}

}
