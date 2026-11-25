// Copyright 2026 Ian Karlsson
// Roland's exclusive messages: parameters as Data Sets by a model's address map, and a file of them read
// (docs/formats.md, "System exclusive files")
#include "roland.hpp"

#include "../maps.hpp"

#include <map>

namespace syfs::roland {

namespace {

constexpr uint8_t EXCLUSIVE = 0xF0, END = 0xF7, ROLAND = 0x41, DT1 = 0x12;
constexpr uint32_t ROW = 128;
constexpr uint64_t MAX_SIZE = 16 * 1024 * 1024;

// what an area of a map is listed as, by its block or group, and the block that extends it for the same number
struct Kind {
	const char *group;
	syfs_key kind;
	const char *extended;
};
struct Family {
	const char *map;     // the map its files are read by
	syfs_key format;     // what its entries' records are
	const char *lesser;  // a model of the same ID whose map is part of this, a file of nothing more read by it
	syfs_key lesser_format;
	bool banks;          // its entries are under their areas' banks
	const Kind *kinds;
	size_t count;
};

constexpr Kind s700_kinds[] = {
	{ "volume", SYFS_TYPE_VOLUME, nullptr },
	{ "performance", SYFS_TYPE_PERFORMANCE, "ext-performance" },
	{ "patch", SYFS_TYPE_PATCH, nullptr },
	{ "partial", SYFS_TYPE_PARTIAL, "ext-partial" },
	{ "sample", SYFS_TYPE_SAMPLE, nullptr },
};
constexpr Kind jv1080_kinds[] = {
	{ "1-1", SYFS_TYPE_SYSTEM, nullptr },
	{ "1-2", SYFS_TYPE_PERFORMANCE, nullptr },
	{ "1-3", SYFS_TYPE_PATCH, nullptr },
	{ "1-4", SYFS_TYPE_RHYTHM_SET, nullptr },
};
constexpr Kind jv880_kinds[] = {
	{ "1-1", SYFS_TYPE_SYSTEM, nullptr },
	{ "1-2", SYFS_TYPE_PERFORMANCE, nullptr },
	{ "1-3", SYFS_TYPE_PATCH, nullptr },
	{ "1-4", SYFS_TYPE_RHYTHM_SET, nullptr },
};
constexpr Kind jd_kinds[] = {
	{ "1-1", SYFS_TYPE_SYSTEM, nullptr },
	{ "1-2", SYFS_TYPE_PERFORMANCE, nullptr },
	{ "1-3", SYFS_TYPE_PATCH, nullptr },
	{ "1-4", SYFS_TYPE_RHYTHM_SET, nullptr },
};
// a JD-800's patch in its patch memory and its temporary area, and one of a multi-mode part, which has no effects;
// its special setup is the rhythm set
constexpr Kind jd800_kinds[] = {
	{ "4-3", SYFS_TYPE_SYSTEM, nullptr },
	{ "4-5-1", SYFS_TYPE_PATCH, nullptr },
	{ "4-1-2", SYFS_TYPE_PATCH, nullptr },
	{ "4-2", SYFS_TYPE_RHYTHM_SET, nullptr },
};
constexpr Kind xv_kinds[] = {
	{ "1-1", SYFS_TYPE_SYSTEM, nullptr },
	{ "1-2", SYFS_TYPE_PERFORMANCE, nullptr },
	{ "1-3-1", SYFS_TYPE_PATCH, nullptr },
	{ "1-3-2", SYFS_TYPE_RHYTHM_SET, nullptr },
};
constexpr Kind fantom_kinds[] = {
	{ "fa-system", SYFS_TYPE_SYSTEM, nullptr },
	{ "fa-performance", SYFS_TYPE_PERFORMANCE, nullptr },
	{ "fa-multitimbre", SYFS_TYPE_MULTI, nullptr },
	{ "fa-patch", SYFS_TYPE_PATCH, nullptr },
	{ "fa-rhythm", SYFS_TYPE_RHYTHM_SET, nullptr },
};
// the S-700 series share a model ID: the S-760's map has every block of the others'.  The JV-2080 shares the
// JV-1080's, and what it adds lies past the JV-1080's blocks' ends.  The XV-3080 shares the XV-5080's and has fewer
// parts and no system equalizer, so its system and performances are whole by its own map alone.  The Fantom shares
// their ID too: a file is its where more of the file is in its map than in theirs (its multitimbres, its system, a
// patch's controller)
constexpr Family families[] = {
	{ "s760", SYFS_FORMAT_S760, nullptr, SYFS_NONE, false, s700_kinds, std::size(s700_kinds) },
	{ "jv1080", SYFS_FORMAT_JV1080, nullptr, SYFS_NONE, true, jv1080_kinds, std::size(jv1080_kinds) },
	{ "jv880", SYFS_FORMAT_JV80, nullptr, SYFS_NONE, true, jv880_kinds, std::size(jv880_kinds) },
	{ "jd990", SYFS_FORMAT_JD990, nullptr, SYFS_NONE, true, jd_kinds, std::size(jd_kinds) },
	{ "jd800", SYFS_FORMAT_JD800, nullptr, SYFS_NONE, true, jd800_kinds, std::size(jd800_kinds) },
	{ "xv5080", SYFS_FORMAT_XV5080, "xv3080", SYFS_FORMAT_XV3080, true, xv_kinds, std::size(xv_kinds) },
	{ "fantom", SYFS_FORMAT_FANTOM, nullptr, SYFS_NONE, true, fantom_kinds, std::size(fantom_kinds) },
};

// a Data Set of a map's model: its device and address, and its data's place in the bytes
struct Message {
	uint8_t device;
	uint32_t addr;
	size_t data, size;
};

// the Data Set that starts at bytes[at] and ends before end, where it is one of the map's and its sum is right
bool message(const std::vector<uint8_t> &bytes, size_t at, size_t end, const map::Map &m, Message &out)
{
	const size_t head = 3 + m.model_id_size + 1;
	if (end - at < head + m.addr_size + 2 || bytes[at + 1] != ROLAND || bytes[at + head - 1] != DT1
		|| memcmp(&bytes[at + 3], m.model_id, m.model_id_size))
		return false;
	unsigned sum = 0;
	for (size_t k = at + head; k < end; k++)
		sum += bytes[k];
	if (sum % 128)
		return false;
	out.device = bytes[at + 2];
	out.addr = 0;
	for (size_t k = 0; k < m.addr_size; k++)
		out.addr = out.addr * 128 + bytes[at + head + k];
	out.data = at + head + m.addr_size;
	out.size = end - 1 - out.data;
	return true;
}

// a block's units as they come in, and which have
struct Filled {
	uint8_t device = 0;
	std::vector<uint8_t> units;
	size_t have = 0;
	std::vector<bool> got;
};

}

namespace syx {

void put(std::vector<uint8_t> &out, const map::Map &m, uint8_t device, uint32_t addr, const uint8_t *units, size_t n)
{
	for (size_t done = 0; done < n;) {
		const uint32_t a = addr + uint32_t(done);
		const size_t k = std::min<size_t>(n - done, ROW - a % ROW);
		out.insert(out.end(), { EXCLUSIVE, ROLAND, device });
		out.insert(out.end(), m.model_id, m.model_id + m.model_id_size);
		out.push_back(DT1);
		unsigned sum = 0;
		for (int b = m.addr_size - 1; b >= 0; b--) {
			out.push_back(uint8_t(a >> (7 * b) & 127));
			sum += out.back();
		}
		for (size_t u = 0; u < k; u++) {
			out.push_back(units[done + u] & 127);
			sum += out.back();
		}
		out.push_back(uint8_t((128 - sum % 128) % 128));
		out.push_back(END);
		done += k;
	}
}

bool packed(std::string_view map, std::string_view bank, uint32_t number, uint8_t device, const std::vector<uint8_t> &rec,
	std::vector<uint8_t> &out)
{
	const map::Map *m = map::find(map);
	const uint32_t places = m ? m->areas_of(bank) : 0;
	const map::Area *a = places ? m->area(bank, number % places) : nullptr;
	std::vector<std::vector<uint8_t>> units;
	if (!a || !map::unpack(*m, *a, rec.data(), rec.size(), units))
		return false;
	for (uint32_t k = 0; k < a->count; k++)
		if (!units[k].empty())
			put(out, *m, device, m->entries[a->first + k].addr, units[k].data(), units[k].size());
	return true;
}

bool units(std::string_view map, std::string_view bank, uint32_t number, uint8_t device, const std::vector<uint8_t> &all,
	std::vector<uint8_t> &out)
{
	const map::Map *m = map::find(map);
	const uint32_t places = m ? m->areas_of(bank) : 0;
	const map::Area *a = places ? m->area(bank, number % places) : nullptr;
	size_t at = 0;
	for (uint32_t k = 0; a && k < a->count; k++) {
		const map::Entry &e = m->entries[a->first + k];
		const uint32_t size = m->blocks[e.block].size;
		if (at + size > all.size())
			return false;
		put(out, *m, device, e.addr, &all[at], size);
		at += size;
	}
	return a && at == all.size();
}

std::vector<uint8_t> nibbles(const uint8_t *bytes, size_t n)
{
	std::vector<uint8_t> out(2 * n);
	for (size_t k = 0; k < n; k++) {
		out[2 * k] = bytes[k] >> 4;
		out[2 * k + 1] = bytes[k] & 15;
	}
	return out;
}

}

// a Data Set of a model that has a map, first in the file
syfs_key Syx::probe(const Image &im)
{
	uint8_t head[8];
	if (im.size() > MAX_SIZE || !im.read(0, head, sizeof head) || head[0] != EXCLUSIVE || head[1] != ROLAND)
		return SYFS_NONE;
	for (const Family &f : families) {
		const map::Map *m = map::find(f.map);
		if (m && !memcmp(&head[3], m->model_id, m->model_id_size) && head[3 + m->model_id_size] == DT1)
			return SYFS_FORMAT_SYX;
	}
	return SYFS_NONE;
}

void Syx::list()
{
	const auto bytes = image.read(0, size_t(image.size()));
	const Family *family = nullptr;
	for (const Family &f : families) {
		const map::Map *m = map::find(f.map);
		if (!family && m && bytes.size() > size_t(3 + m->model_id_size) && !memcmp(&bytes[3], m->model_id, m->model_id_size))
			family = &f, map_ = m;
	}
	if (!family)
		return;
	// the Data Sets of the model
	std::vector<Message> messages;
	for (size_t at = 0; at < bytes.size();) {
		if (bytes[at] != EXCLUSIVE) {
			at++;
			continue;
		}
		size_t end = at + 1;
		while (end < bytes.size() && bytes[end] < 0x80)
			end++;
		Message msg;
		if (end < bytes.size() && bytes[end] == END && message(bytes, at, end, *map_, msg))
			messages.push_back(msg);
		at = end;
	}
	// of the families of that ID, the one whose map has the most of the file; the first where they have as much
	size_t most = 0;
	for (const Family &f : families) {
		const map::Map *m = map::find(f.map);
		if (!m || m->model_id_size != map_->model_id_size || memcmp(m->model_id, map_->model_id, m->model_id_size))
			continue;
		size_t known = 0;
		for (const Message &msg : messages)
			for (size_t u = 0; u < msg.size; u++)
				known += m->entry_of(msg.addr + uint32_t(u)) != nullptr;
		if (known > most)
			most = known, family = &f, map_ = m;
	}
	syfs_key records = family->format;
	if (const map::Map *lesser = family->lesser ? map::find(family->lesser) : nullptr) {
		bool more = false;
		for (const Message &msg : messages)
			for (size_t u = 0; u < msg.size && !more; u++)
				more = map_->entry_of(msg.addr + uint32_t(u)) && !lesser->entry_of(msg.addr + uint32_t(u));
		if (!more)
			map_ = lesser, records = family->lesser_format;
	}
	// every Data Set's units put where the map has them
	std::map<const map::Entry *, Filled> filled;
	for (const Message &msg : messages)
		for (size_t u = 0; u < msg.size; u++) {
			const map::Entry *e = map_->entry_of(msg.addr + uint32_t(u));
			if (!e)
				continue;
			Filled &f = filled[e];
			if (f.units.empty()) {
				f.device = msg.device;
				f.units.assign(map_->blocks[e->block].size, 0);
				f.got.assign(f.units.size(), false);
			}
			const uint32_t k = msg.addr + uint32_t(u) - e->addr;
			f.units[k] = bytes[msg.data + u];
			f.have += !f.got[k];
			f.got[k] = true;
		}
	// an area all of whose blocks are whole, as the pieces of a record; none where any is not.  Where the area has a
	// packed record, a block that is not in it may be missing (an XV-5080's patch's split keys, which it never sends)
	auto whole = [&](const map::Area *a, Record &r) {
		if (!a)
			return false;
		bool packs = false;
		for (uint32_t k = 0; k < a->count; k++)
			packs = packs || map_->blocks[map_->entries[a->first + k].block].packed;
		std::vector<const map::Entry *> have;
		for (uint32_t k = 0; k < a->count; k++) {
			const map::Entry *e = &map_->entries[a->first + k];
			auto f = filled.find(e);
			if (f != filled.end() && f->second.have == f->second.units.size())
				have.push_back(e);
			else if (!packs || map_->blocks[e->block].packed)
				return false;
		}
		for (const map::Entry *e : have) {
			r.device = filled[e].device;
			r.pieces.emplace_back(e->addr, filled[e].units);
		}
		return true;
	};
	std::map<std::string_view, size_t> banks;
	for (uint32_t n = 0; n < map_->area_count; n++) {
		const map::Area &area = map_->areas[n];
		const Kind *kind = nullptr;
		for (size_t k = 0; k < family->count; k++)
			if (!strcmp(family->kinds[k].group, area.group))
				kind = &family->kinds[k];
		Record r;
		if (!kind || !whole(&area, r))
			continue;
		// the name: its first block's, a character of name_units units each, the high one first; an S-700
		// performance's record may have a number with its top bit set where the colon is, the name's fourth
		const map::Block *b = &map_->blocks[map_->entries[area.first].block];
		const std::vector<uint8_t> &units = r.pieces[0].second;
		const unsigned bits = b->count ? b->params[0].bits : 7;
		std::string name;
		uint8_t first[16] = {};
		for (uint32_t c = 0; c < b->name_size; c++) {
			unsigned v = 0;
			for (uint32_t u = 0; u < b->name_units; u++)
				v = v << bits | units[b->name_at + c * b->name_units + u];
			if (family->format == SYFS_FORMAT_S760 && kind->kind == SYFS_TYPE_PERFORMANCE && c == 3 && v & 0x80)
				v = ':';
			name += v >= 32 && v < 127 ? char(v) : '?';
			if (c < sizeof first)
				first[c] = uint8_t(v);
		}
		size_t parent = syfs::NONE;
		if (family->banks) {
			auto have = banks.find(area.bank);
			if (have == banks.end()) {
				have = banks.emplace(area.bank, add(SYFS_TYPE_BANK, records, uint32_t(banks.size()), syfs::NONE, area.bank, 0, 0)).first;
				records_.emplace_back();
			}
			parent = have->second;
		}
		const size_t i = add(kind->kind, records, area.number, parent, b->name_size ? name : std::string(area.bank), 0, 0);
		if (family->format == SYFS_FORMAT_S760)
			tag(i, s700::category(first));
		if (kind->extended)
			whole(map_->area_in(kind->extended, area.number), r);
		records_.push_back(std::move(r));
	}
}

// what an S-700 record names, by the numbers in it: the entries of the type named with those numbers
std::vector<size_t> Syx::refs(size_t i) const
{
	const Record &r = records_[i];
	if (entries[i].format != SYFS_FORMAT_S760 || r.pieces.empty())
		return {};
	const std::vector<uint8_t> &units = r.pieces[0].second;
	std::vector<uint8_t> rec(units.size() / 2);
	for (size_t k = 0; k < rec.size(); k++)
		rec[k] = uint8_t(units[2 * k] << 4 | (units[2 * k + 1] & 15));
	std::vector<int32_t> nums;
	const syfs_key kind = s700::named(entries[i].kind, rec.data(), nums);
	std::vector<size_t> out;
	for (int32_t n : nums) {
		const size_t to = n < 0 ? syfs::NONE : child(syfs::NONE, kind, uint32_t(n));
		if (to != syfs::NONE && std::find(out.begin(), out.end(), to) == out.end())
			out.push_back(to);
	}
	return out;
}

bool Syx::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	const Record &r = records_[i];
	for (const auto &[addr, units] : r.pieces)
		syx::put(out, *map_, r.device, addr, units.data(), units.size());
	return !r.pieces.empty();
}

std::unique_ptr<Medium> Syx::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Syx>(io, format);
}

}
