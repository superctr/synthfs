// Copyright 2026 Ian Karlsson
// Roland's wave expansion boards and PCM cards as dumped: SR-JV80 boards and SO-PCM1 cards, SRX boards; their banks of
// patches and rhythm sets, their waves and each wave's samples by key (docs/formats.md, "Roland wave expansion boards
// and cards")
#include "roland.hpp"

#include <cmath>

namespace syfs::roland {

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
// an XV's packed records: a patch, a rhythm set of a common and 88 keys' tones
constexpr uint32_t XV_PATCH = 574, XV_RHYTHM_COMMON = 160, XV_TONE = 121, XV_KEYS = 88, XV_RHYTHM = XV_RHYTHM_COMMON + XV_KEYS * XV_TONE;
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

WaveRom::WaveRom(const syfs_io &io, syfs_key format) : Medium(io, format) {}

WaveRom::~WaveRom() = default;

size_t WaveRom::bank(std::string_view shape, std::string_view name)
{
	size_t b = add(SYFS_TYPE_BANK, format, banks_++, syfs::NONE, std::string(name), 0, 0);
	tag(b, shape);
	return b;
}

void WaveRom::split(size_t wave, uint32_t number, uint8_t below, uint8_t top, uint64_t record)
{
	if (record == NOWHERE) {
		splits_[wave].emplace_back(top, syfs::NONE);
		return;
	}
	Record r;
	uint64_t bytes = record_of(*socket_, format == SYFS_FORMAT_SRX, record, r) ? r.end - std::min(r.start, r.loop) + 1 : 0;
	size_t c = add(SYFS_TYPE_SAMPLE, format, number, wave, entries[wave].name, bytes, record);
	tag(c, std::to_string(below) + "-" + std::to_string(top));
	splits_[wave].emplace_back(top, c);
}

// the first split whose top key is at or above key, else the last
size_t WaveRom::sample_at(size_t i, int key) const
{
	auto it = splits_.find(i);
	if (entries[i].kind != SYFS_TYPE_WAVE)
		return Medium::sample_at(i, key);
	if (it == splits_.end() || it->second.empty())
		return syfs::NONE;
	for (const auto &[top, c] : it->second)
		if (key <= top)
			return c;
	return it->second.back().second;
}

size_t WaveRom::at_middle_c(size_t i) const
{
	size_t c = sample_at(i, 60);
	return c != syfs::NONE ? c : nearest(i, 60);
}

size_t WaveRom::nearest(size_t wave, int key) const
{
	auto it = splits_.find(wave);
	if (it == splits_.end())
		return syfs::NONE;
	size_t best = syfs::NONE;
	int below = 0, far = 256;
	for (const auto &[top, c] : it->second) {
		int d = key < below ? below - key : key > top ? key - top : 0;
		if (c != syfs::NONE && d < far)
			best = c, far = d;
		below = top + 1;
	}
	return best;
}

namespace {

// Roland's boards and cards as sold (docs/formats.md, "Roland wave expansion boards and cards"): an SR-JV80 board by
// the name in its header, a PCM card by the identity and the date it carries, an SRX board by its number
struct Sold {
	const char *key, *date, *model, *title;
};

constexpr Sold SRJV80_BOARDS[] = {
	{ "POP Exp  Ver1.00", nullptr, "SR-JV80-01", "Pop" },
	{ "ORC Exp  Ver1.00", nullptr, "SR-JV80-02", "Orchestral" },
	{ "Pno Exp  Ver1.00", nullptr, "SR-JV80-03", "Piano" },
	{ "SR-JV80-04 Synth", nullptr, "SR-JV80-04", "Vintage Synth" },
	{ "SR-JV80-05 World", nullptr, "SR-JV80-05", "World" },
	{ "SR-JV80-06 Dance", nullptr, "SR-JV80-06", "Dance" },
	{ "SR-JV80-07 Super", nullptr, "SR-JV80-07", "Super Sound Set" },
	{ "SR-JV80-08 60&70", nullptr, "SR-JV80-08", "Keyboards of the 60s and 70s" },
	{ "SR-JV80-09 Sessn", nullptr, "SR-JV80-09", "Session" },
	{ "SR-JV80-10 BS&DR", nullptr, "SR-JV80-10", "Bass & Drum" },
	{ "SR-JV80-11 Tekno", nullptr, "SR-JV80-11", "Techno" },
	{ "SR-JV80-12 Hphop", nullptr, "SR-JV80-12", "HipHop" },
	{ "SR-JV80-13 Vocal", nullptr, "SR-JV80-13", "Vocal" },
	{ "SR-JV80-14 Asia", nullptr, "SR-JV80-14", "Asia" },
	{ "SR-JV80-15 SFX", nullptr, "SR-JV80-15", "Special FX" },
	{ "SR-JV80-16 Orch2", nullptr, "SR-JV80-16", "Orchestral II" },
	{ "SR-JV80-17 Cntry", nullptr, "SR-JV80-17", "Country" },
	{ "SR-JV80-18 Latin", nullptr, "SR-JV80-18", "Latin" },
	{ "SR-JV80-19 House", nullptr, "SR-JV80-19", "House" },
	{ "SR-JV80-97 EXPr3", nullptr, "SR-JV80-97", "Experience III" },
	{ "SR-JV80-98 EXPr2", nullptr, "SR-JV80-98", "Experience II" },
	{ "SR-JV80-99 EXPrn", nullptr, "SR-JV80-99", "Experience I" },
};

constexpr Sold PCM_CARDS[] = {
	{ "CRD01010", "1991-11-12", "SO-PCM1-01", "Piano" },
	{ "CRD02008", "1991-11-13", "SO-PCM1-02", "Guitar" },
	{ "CRD05100", "1992-03-26", "SO-PCM1-03", "Rock" },
	{ "CRD04100", "1992-04-16", "SO-PCM1-04", "Grand" },
	{ "CRD05100", "1992-11-10", "SO-PCM1-05", "Accordion" },
	{ "CRD06100", "1992-12-29", "SO-PCM1-06", "Baroque" },
	{ "OFX100", "1993-04-09", "SO-PCM1-07", nullptr },
	{ "CTR100", "1993-05-19", "SO-PCM1-08", nullptr },
};

constexpr const char *SRX_TITLES[] = { "Dynamic Drum Kits", "Concert Piano", "Studio SRX", "Symphonique Strings",
	"Supreme Dance", "Complete Orchestra", "Ultimate Keys", "Platinum Trax", "World Collection", "Big Brass Ensemble",
	"Complete Piano", "Classic EPs" };

// a header's text up to its first zero byte
std::string text(const uint8_t *p, size_t n)
{
	return std::string(reinterpret_cast<const char *>(p), strnlen(reinterpret_cast<const char *>(p), n));
}

}

void WaveRom::identify()
{
	uint8_t h[32];
	if (!image.read(0, h, sizeof h))
		return;
	label = trimmed(plain(h + 0x10, 16));
	label_known = true;
	socket_ = std::make_unique<Socket>(image, format == SYFS_FORMAT_SRX);
	const Sold *sold = nullptr;
	if (format == SYFS_FORMAT_SRX) {
		uint32_t number = socket_->be(0x40, 2);
		set(SYFS_PROP_NUMBER, int64_t(number));
		uint8_t brief[6];
		if (socket_->read(0x3A, brief, sizeof brief)) {
			std::string shown = trimmed(plain(brief, sizeof brief));
			if (!shown.empty())
				set(SYFS_PROP_SHORT_NAME, shown);
		}
		if (number >= 1 && number <= std::size(SRX_TITLES)) {
			char model[16];
			snprintf(model, sizeof model, "SRX-%02u", unsigned(number));
			set(SYFS_PROP_MODEL, model);
			set(SYFS_PROP_TITLE, SRX_TITLES[number - 1]);
		}
	} else if (medium == SYFS_MEDIUM_CARD) {
		uint8_t id[32] = {};
		socket_->read(0x20, id, sizeof id);
		std::string name = text(id, 16), date = text(id + 16, 10);
		set(SYFS_PROP_ID, name);
		set(SYFS_PROP_DATE, date);
		for (const Sold &c : PCM_CARDS)
			if (name == c.key && date == c.date)
				sold = &c;
	} else {
		for (const Sold &b : SRJV80_BOARDS)
			if (label == b.key)
				sold = &b;
	}
	if (sold) {
		set(SYFS_PROP_MODEL, sold->model);
		if (sold->title)
			set(SYFS_PROP_TITLE, sold->title);
	}
}

void WaveRom::list()
{
	own_.clear();
	if (!socket_)
		return;
	if (format == SYFS_FORMAT_SRX)
		list_srx();
	else
		list_srjv80();
}

// Each 1 MB block's header: per slot n a type byte at +0x50 + n, a count at +0x60 + 2n and an address at +0x80 + 4n
// (big-endian); slot 0 the samples (18 bytes), slot 1 the waves (60 bytes: the name, sixteen top keys and sixteen
// sample numbers), slot 3 the patches, slot 4 the rhythm sets, each in the shape the type gives, the JV-880's or the
// JV-1080's
void WaveRom::list_srjv80()
{
	const Socket &s = *socket_;
	struct Shape { int slot; uint8_t type; syfs_key kind; uint32_t size; int name; const char *bank; };  // name: 0 none, 1 plain, 2 packed, 3 plain after a byte, 16 characters
	static constexpr Shape shapes[] = {
		{ 1, 1, SYFS_TYPE_WAVE, 60, 1, "" },
		{ 3, 2, SYFS_TYPE_PATCH, 362, 1, "JV-880 Patches" },
		{ 3, 3, SYFS_TYPE_PATCH, 379, 3, "JD-990 Patches" },
		{ 3, 5, SYFS_TYPE_PATCH, 401, 2, "JV-1080 Patches" },
		{ 4, 2, SYFS_TYPE_RHYTHM_SET, 2684, 0, "JV-880 Rhythm Sets" },
		{ 4, 4, SYFS_TYPE_RHYTHM_SET, 2699, 2, "JV-1080 Rhythm Sets" },
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
			size_t parent = sh.kind == SYFS_TYPE_WAVE ? syfs::NONE : bank(std::to_string(sh.type), sh.bank);
			for (uint32_t k = 0; k < count; k++) {
				uint8_t rec[60] = {};
				uint64_t r = at + uint64_t(k) * sh.size;
				if (sh.name && !s.read(r, rec, sh.kind == SYFS_TYPE_WAVE ? 60 : sh.name == 1 ? 12 : sh.name == 3 ? 17 : 11))
					return;
				std::string name = sh.name == 1 ? plain(rec, 12) : sh.name == 2 ? packed(rec, false) : sh.name == 3 ? trimmed(plain(rec + 1, 16)) : "";
				size_t e = add(sh.kind, format, sh.kind == SYFS_TYPE_WAVE ? waves++ : k, parent, name, 0, r);
				if (sh.kind != SYFS_TYPE_WAVE)
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
// of its table and its length; a table's head a count, the offset of its records from the head and their stride, a
// bank's the bank select it is chosen by at +12 (MSB, LSB).
// Class 1 the samples (24 bytes); class 2 the waves, each record the offset of its definition from the head (a long)
// and its length, the definition the name, four bytes, then a split in four bytes (its top key, a byte, the sample's
// number) to its end; class 4 a bank of patches, class 5 of rhythm sets, each record starting with its name (a patch's
// category in the seven bits after it), the type the machines it is for.  The board's short name, as a machine shows
// it after the board's number, is six characters at +0x3A
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
				size_t e = add(SYFS_TYPE_WAVE, format, k, syfs::NONE, plain(name, sizeof name), 0, t.at + def);
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
			syfs_key kind = t.cls == 4 ? SYFS_TYPE_PATCH : SYFS_TYPE_RHYTHM_SET;
			static const char *const families[] = { "XV-3080", "XV-5080", "Global", "Fantom-S" };
			const uint32_t family = t.type - (t.cls == 4 ? 6 : 5);
			std::string name = family < 4 ? std::string(families[family]) + (t.cls == 4 ? " Patches" : " Rhythm Sets") : "";
			size_t parent = bank(std::to_string(t.cls) + ":" + std::to_string(t.type), name);
			uint8_t select[2];
			if (s.read(uint64_t(t.at) + 12, select, sizeof select))
				own_[parent] = { SYFS_PROP_BANK_SELECT, select[0] * 128 + select[1] };
			for (uint32_t k = 0; k < t.count; k++) {
				uint64_t r = uint64_t(t.at) + t.records + uint64_t(k) * t.stride;
				uint8_t rec[12];
				if (!s.read(r, rec, t.stride >= 12 ? 12 : 11))
					return;
				size_t e = add(kind, format, k, parent, packed(rec, true), 0, r);
				if (kind == SYFS_TYPE_PATCH && t.stride >= 12)
					own_[e] = { SYFS_PROP_CATEGORY, (rec[10] << 8 | rec[11]) >> 5 & 127 };
			}
		}
	}
}

bool WaveRom::number(size_t i, syfs_key key, int64_t &value) const
{
	auto it = own_.find(i);
	if (it == own_.end() || it->second.first != key)
		return false;
	value = it->second.second;
	return true;
}

std::vector<size_t> WaveRom::refs(size_t i) const
{
	std::vector<size_t> out;
	auto it = splits_.find(i);
	if (it != splits_.end())
		for (const auto &sp : it->second)
			if (sp.second != syfs::NONE && std::find(out.begin(), out.end(), sp.second) == out.end())
				out.push_back(sp.second);
	return out;
}

// A split's sample: the rate the fine tune moves (the rate it sounds right at as its original key), the loop from
// its record; a wave answers with the split it plays at middle C, or the nearest that sounds
bool WaveRom::sample(size_t i, syfs::Sample &out) const
{
	if (entries[i].kind == SYFS_TYPE_WAVE)
		i = at_middle_c(i);
	if (i == syfs::NONE || entries[i].kind != SYFS_TYPE_SAMPLE || !socket_)
		return false;
	Record r;
	if (!record_of(*socket_, format == SYFS_FORMAT_SRX, where[i], r))
		return false;
	out.rate = uint32_t(std::lround(r.rate * std::exp2((int(r.fine) - 1024) / 12000.0)));
	out.key = r.key;
	const bool early = r.loop < r.start && !(r.flags & ONCE);
	out.frames = uint64_t(r.end) - r.start + 1 + (early ? r.end - r.loop + 1 : 0);
	out.end = out.frames;
	if (r.loop < r.end && !(r.flags & ONCE)) {
		out.loop = r.flags & ALTERNATE ? SYFS_LOOP_ALTERNATE : SYFS_LOOP_FORWARD;
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
size_t WaveRom::wave(size_t i, const syfs::Sample &, uint64_t first, int16_t *out, size_t frames) const
{
	if (entries[i].kind == SYFS_TYPE_WAVE)
		i = at_middle_c(i);
	if (i == syfs::NONE)
		return 0;
	if (decoded_ != i) {
		Record r;
		decoded_ = syfs::NONE;
		frames_.clear();
		if (!record_of(*socket_, format == SYFS_FORMAT_SRX, where[i], r))
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

syfs_key WaveRom::probe(const Image &im)
{
	uint8_t h[16];
	if (!im.read(0, h, sizeof h))
		return SYFS_NONE;
	if (srjv80(h, im.size()))
		return SYFS_FORMAT_SRJV80;
	if (srx(h, im.size()))
		return SYFS_FORMAT_SRX;
	return SYFS_NONE;
}

// an SR-JV80-format ROM of 1 MB, or of 2 MB named SO-PCM1 or Card, is a card; any other a board
syfs_key WaveRom::medium_of(const Image &im)
{
	uint8_t h[32];
	if (!im.read(0, h, sizeof h))
		return SYFS_NONE;
	if (srx(h, im.size()))
		return SYFS_MEDIUM_BOARD;
	if (!srjv80(h, im.size()))
		return SYFS_NONE;
	if (im.size() == 1 * MB || (im.size() == 2 * MB && (!memcmp(h + 0x10, "SO-PCM1", 7) || !memcmp(h + 0x10, "Card", 4))))
		return SYFS_MEDIUM_CARD;
	return SYFS_MEDIUM_BOARD;
}

// a patch or rhythm set of the JV-1080's shape (its bank's type 5 or 4): the packed record by the JV-1080's map, at
// the user patch or rhythm set its number has.  An SRX board's of an XV-3080's or XV-5080's bank, by that machine's
// map: a patch's record as it is, a rhythm set's made whole of its common and the 88 tones it points at, each a long
// from the bank's start
bool WaveRom::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	const syfs::Entry &e = entries[i];
	if (e.parent == syfs::NONE || (e.kind != SYFS_TYPE_PATCH && e.kind != SYFS_TYPE_RHYTHM_SET))
		return false;
	const bool patch = e.kind == SYFS_TYPE_PATCH;
	const char *shape = entries[e.parent].tag;
	if (format == SYFS_FORMAT_SRX) {
		const char *map = !strcmp(shape, patch ? "4:6" : "5:5") ? "xv3080" : !strcmp(shape, patch ? "4:7" : "5:6") ? "xv5080" : nullptr;
		if (!map)
			return false;
		std::vector<uint8_t> rec(patch ? XV_PATCH : XV_RHYTHM);
		if (patch)
			return socket_->read(where[i], rec.data(), rec.size()) && syx::packed(map, "User Patch", e.number, 0x10, rec, out);
		// the bank's start: the table of the directory this record is in
		const Socket &s = *socket_;
		const uint32_t tables = std::min<uint32_t>(s.be(0x42, 2), 64);
		uint8_t set[XV_RHYTHM_COMMON + 4 * XV_KEYS];
		for (uint32_t t = 0; t < tables; t++) {
			const uint64_t at = s.be(0x80 + 16 * t + 4, 4), len = s.be(0x80 + 16 * t + 8, 4);
			if (where[i] < at || where[i] + sizeof set > at + len || !s.read(where[i], set, sizeof set))
				continue;
			memcpy(rec.data(), set, XV_RHYTHM_COMMON);
			for (uint32_t k = 0; k < XV_KEYS; k++) {
				const uint64_t tone = be32(&set[XV_RHYTHM_COMMON + 4 * k]);
				if (tone + XV_TONE > len || !s.read(at + tone, &rec[XV_RHYTHM_COMMON + k * XV_TONE], XV_TONE))
					return false;
			}
			return syx::packed(map, "User Rhythm", e.number, 0x10, rec, out);
		}
		return false;
	}
	if (format != SYFS_FORMAT_SRJV80 || strcmp(shape, patch ? "5" : "4"))
		return false;
	std::vector<uint8_t> rec(patch ? 401 : 2699);
	return socket_->read(where[i], rec.data(), rec.size())
		&& syx::packed("jv1080", patch ? "User Patch" : "User Rhythm", e.number, 0x10, rec, out);
}

std::unique_ptr<Medium> WaveRom::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<WaveRom>(io, format);
}

// A DATA card: sixteen bytes of key, sixteen of text, then the card's memory as the machine keeps it
namespace {
constexpr uint64_t CARD_32K = 0x8000, CARD_64K = 0x10000;
constexpr uint8_t JV80_KEY[] = { 'R', 'o', 'l', 'a', 'n', 'd', ' ', 'J', 'V', '-', '8', '0', 'D', 0xBB, 'P', 0xAF };
constexpr uint8_t JV1080_KEY[] = { 'R', 'o', 'l', 'a', 'n', 'd', 'J', 'V', '-', '9', '9', '0', 'D', 0xBB, 'P', 0xAF };
}

syfs_key DataCard::probe(const Image &im)
{
	uint8_t h[16];
	if ((im.size() != CARD_32K && im.size() != CARD_64K) || !im.read(0, h, sizeof h))
		return SYFS_NONE;
	return !memcmp(h, JV80_KEY, sizeof h) ? SYFS_FORMAT_JV80 : !memcmp(h, JV1080_KEY, sizeof h) ? SYFS_FORMAT_JV1080 : SYFS_NONE;
}

void DataCard::identify()
{
	uint8_t name[16];
	if (format == SYFS_FORMAT_JV1080 && image.read(0x10, name, sizeof name))
		label = trimmed(plain(name, sizeof name));
	label_known = true;
}

void DataCard::list()
{
	// each list's records one after another, a name at the start of each: twelve characters as they are (a JV-80's),
	// or packed seven bits each (a JV-1080's).  A JV-80's rhythm set has no name.
	struct Run {
		syfs_key kind;
		uint32_t at, size, count;
	};
	static constexpr Run jv80[] = { { SYFS_TYPE_PERFORMANCE, 0x20, 204, 16 }, { SYFS_TYPE_PATCH, 0xCE0, 362, 64 },
		{ SYFS_TYPE_RHYTHM_SET, 0x6760, 0, 1 } };
	static constexpr Run jv1080[] = { { SYFS_TYPE_PERFORMANCE, 0x20, 245, 16 }, { SYFS_TYPE_PATCH, 0xF70, 401, 64 },
		{ SYFS_TYPE_RHYTHM_SET, 0x73B0, 2699, 1 } };
	const bool later = format == SYFS_FORMAT_JV1080;
	// a JV-1080's 64 KB card is two halves alike, the upper's key a byte further on; a half without its key holds
	// nothing
	uint8_t upper[17];
	const bool two = later && image.size() == CARD_64K && image.read(CARD_32K, upper, sizeof upper) && upper[0] == 0xAF
		&& !memcmp(upper + 1, JV1080_KEY, 13);
	for (const Run &r : later ? jv1080 : jv80)
		for (uint32_t half = 0, number = 0; half < (two ? 2u : 1u); half++)
			for (uint32_t k = 0; k < r.count; k++, number++) {
				const uint64_t at = half * CARD_32K + r.at + uint64_t(k) * r.size;
				uint8_t rec[12];
				if (!image.read(at, rec, sizeof rec))
					return;
				add(r.kind, format, number, syfs::NONE, !r.size ? "" : later ? packed(rec, false) : plain(rec, 12), r.size, at);
			}
}

// a JV-1080's card's performance, patch or rhythm set: the packed record a board has, at the card's place of its
// number
bool DataCard::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	const syfs::Entry &e = entries[i];
	const char *bank = e.kind == SYFS_TYPE_PERFORMANCE ? "Card Performance" : e.kind == SYFS_TYPE_PATCH ? "Card Patch"
		: e.kind == SYFS_TYPE_RHYTHM_SET ? "Card Rhythm" : nullptr;
	if (format != SYFS_FORMAT_JV1080 || !bank)
		return false;
	const auto rec = image.read(where[i], size_t(e.bytes));
	return !rec.empty() && syx::packed("jv1080", bank, e.number, 0x10, rec, out);
}

std::unique_ptr<Medium> DataCard::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<DataCard>(io, format);
}

// A JV-1080's battery memory: the system in the first 256 bytes, then the performances, the patches and the rhythm
// sets, each list from a multiple of 256, their records a DATA card's.  An XV-5080's and an XV-3080's: the system,
// then the three lists one after another, a record in a place longer than it (a performance's 926 bytes in 1064, a
// patch's 574 in 938 or 838)
namespace {
struct MemoryRun {
	syfs_key kind;
	uint32_t at, stride, size, count;
	const char *bank;
};
struct MemoryOf {
	syfs_key format;
	uint64_t size;
	const char *map;
	bool from_top;   // its records' bits
	MemoryRun runs[4];
};
constexpr uint32_t FANTOM_USER = 0x1A0400;  // its user block, in a memory of its EEPROM and half its flash
constexpr MemoryOf memories[] = {
	{ SYFS_FORMAT_JV1080, 0x10000, "jv1080", false, { { SYFS_TYPE_PERFORMANCE, 0x100, 245, 245, 32, "User Performance" },
		{ SYFS_TYPE_PATCH, 0x2000, 401, 401, 128, "User Patch" }, { SYFS_TYPE_RHYTHM_SET, 0xE900, 2699, 2699, 2, "User Rhythm" } } },
	{ SYFS_FORMAT_XV5080, 0x40000, "xv5080", true, { { SYFS_TYPE_PERFORMANCE, 0x1A8, 1064, 926, 64, "User Performance" },
		{ SYFS_TYPE_PATCH, 0x10BA8, 938, XV_PATCH, 128, "User Patch" }, { SYFS_TYPE_RHYTHM_SET, 0x2E0A8, XV_RHYTHM, XV_RHYTHM, 4, "User Rhythm" } } },
	{ SYFS_FORMAT_XV3080, 0x40000, "xv3080", true, { { SYFS_TYPE_PERFORMANCE, 0x1A4, 1064, 926, 64, "User Performance" },
		{ SYFS_TYPE_PATCH, 0x10BA4, 838, XV_PATCH, 128, "User Patch" }, { SYFS_TYPE_RHYTHM_SET, 0x2AEA4, XV_RHYTHM, XV_RHYTHM, 4, "User Rhythm" } } },
	{ SYFS_FORMAT_FANTOM, 0x200400, "fantom", true, { { SYFS_TYPE_PERFORMANCE, FANTOM_USER + 0x190, 1126, 1126, 64, "User Performance" },
		{ SYFS_TYPE_MULTI, FANTOM_USER + 0x11B10, 954, 954, 16, "User Multitimbre" },
		{ SYFS_TYPE_PATCH, FANTOM_USER + 0x156B0, 592, 592, 128, "User Patch" },
		{ SYFS_TYPE_RHYTHM_SET, FANTOM_USER + 0x27EB0, 10824, 10824, 16, "User Rhythm" } } },
};
const MemoryOf *memory_of(syfs_key format)
{
	for (const MemoryOf &m : memories)
		if (m.format == format)
			return &m;
	return nullptr;
}
}

// whether a record's first eleven bytes are a name of twelve printable characters
bool named(const uint8_t *rec, bool from_top)
{
	for (int c = 0; c < 12; c++) {
		unsigned v = 0;
		for (int b = 0; b < 7; b++) {
			const int n = 7 * c + b;
			v = from_top ? v << 1 | (rec[n / 8] >> (7 - n % 8) & 1) : v | unsigned(rec[n / 8] >> (n % 8) & 1) << b;
		}
		if (v < 32 || v >= 127)
			return false;
	}
	return true;
}

std::string xv_name(const uint8_t *rec)
{
	return packed(rec, true);
}

bool Memory::fits(const syfs_io &io, syfs_key format)
{
	const MemoryOf *m = memory_of(format);
	return m && io.size == m->size;
}

// a record whose name is twelve printable characters; a memory never written has none
void Memory::list()
{
	const MemoryOf *m = memory_of(format);
	for (const MemoryRun &r : m->runs)
		for (uint32_t k = 0; r.bank && k < r.count; k++) {
			const uint64_t at = r.at + uint64_t(k) * r.stride;
			uint8_t rec[11];
			if (!image.read(at, rec, sizeof rec))
				return;
			if (named(rec, m->from_top))
				add(r.kind, format, k, syfs::NONE, packed(rec, m->from_top), r.size, at);
		}
}

bool Memory::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	const syfs::Entry &e = entries[i];
	const MemoryOf *m = memory_of(format);
	for (const MemoryRun &r : m->runs)
		if (r.bank && r.kind == e.kind) {
			const auto rec = image.read(where[i], r.size);
			return !rec.empty() && syx::packed(m->map, r.bank, e.number, 0x10, rec, out);
		}
	return false;
}

std::unique_ptr<Medium> Memory::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Memory>(io, format);
}

}
