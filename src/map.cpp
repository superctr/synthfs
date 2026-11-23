// Copyright 2026 Ian Karlsson
// The parameter address maps' tables read: blocks, places and packed records (docs/maps.md)
#include "maps.hpp"

namespace syfs::map {

namespace {

// every map with its areas and their blocks' places written out
struct Built {
	std::vector<std::vector<Entry>> entries;
	std::vector<std::vector<Area>> areas;
	std::vector<Map> maps;
};

Built make()
{
	Built b;
	size_t count;
	const Source *src = sources(count);
	b.entries.resize(count);
	b.areas.resize(count);
	for (size_t n = 0; n < count; n++) {
		const Source &s = src[n];
		for (uint32_t r = 0; r < s.run_count; r++) {
			const Run &run = s.runs[r];
			for (uint32_t k = 0; k < run.count; k++) {
				const uint32_t addr = run.addr + k * run.stride;
				b.areas[n].push_back({ run.group, run.bank, run.number + k, addr, uint32_t(b.entries[n].size()), run.member_count });
				for (uint32_t i = 0; i < run.member_count; i++)
					b.entries[n].push_back({ addr + run.members[i].addr, run.members[i].block, uint16_t(b.areas[n].size() - 1), run.members[i].order });
			}
		}
		Map m {};
		m.id = s.id, m.model = s.model;
		for (int i = 0; i < 4; i++)
			m.model_id[i] = s.model_id[i];
		m.model_id_size = s.model_id_size, m.addr_size = s.addr_size, m.high_first = s.high_first;
		m.blocks = s.blocks, m.block_count = s.block_count;
		m.entries = b.entries[n].data(), m.entry_count = uint32_t(b.entries[n].size());
		m.areas = b.areas[n].data(), m.area_count = uint32_t(b.areas[n].size());
		b.maps.push_back(m);
	}
	return b;
}

}

const Map *all(size_t &count)
{
	static const Built built = make();
	count = built.maps.size();
	return built.maps.data();
}

const Map *find(std::string_view id)
{
	size_t count;
	const Map *maps = all(count);
	for (size_t n = 0; n < count; n++)
		if (id == maps[n].id)
			return &maps[n];
	return nullptr;
}

const Block *Map::block(std::string_view id) const
{
	for (uint32_t n = 0; n < block_count; n++)
		if (id == blocks[n].id)
			return &blocks[n];
	return nullptr;
}

const Entry *Map::at(const Block *b, uint32_t n) const
{
	for (uint32_t k = 0; k < entry_count; k++)
		if (&blocks[entries[k].block] == b && !n--)
			return &entries[k];
	return nullptr;
}

uint32_t Map::places(const Block *b) const
{
	uint32_t n = 0;
	for (uint32_t k = 0; k < entry_count; k++)
		n += &blocks[entries[k].block] == b;
	return n;
}

// the entries are in the order of their addresses
const Entry *Map::entry_of(uint32_t addr) const
{
	uint32_t lo = 0, hi = entry_count;
	while (lo < hi) {
		const uint32_t mid = (lo + hi) / 2;
		if (entries[mid].addr <= addr)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo && addr - entries[lo - 1].addr < blocks[entries[lo - 1].block].size ? &entries[lo - 1] : nullptr;
}

const Area *Map::area(std::string_view bank, uint32_t n) const
{
	for (uint32_t k = 0; k < area_count; k++)
		if (bank == areas[k].bank && areas[k].number == n)
			return &areas[k];
	return nullptr;
}

uint32_t Map::areas_of(std::string_view bank) const
{
	uint32_t n = 0;
	for (uint32_t k = 0; k < area_count; k++)
		n += bank == areas[k].bank;
	return n;
}

const Area *Map::area_in(std::string_view group, uint32_t n) const
{
	for (uint32_t k = 0; k < area_count; k++)
		if (group == areas[k].group && areas[k].number == n)
			return &areas[k];
	return nullptr;
}

namespace {

constexpr size_t ABSENT = ~size_t(0);

// each block of the area and where its bytes start in the record (ABSENT where it has none); false where none has
bool laid(const Map &m, const Area &a, std::vector<size_t> &starts, size_t &size)
{
	size = 0;
	std::vector<uint32_t> by(a.count);
	for (uint32_t k = 0; k < a.count; k++)
		by[m.entries[a.first + k].order] = k;
	starts.assign(a.count, ABSENT);
	for (uint32_t k : by) {
		const Block &b = m.blocks[m.entries[a.first + k].block];
		if (b.packed)
			starts[k] = size;
		size += b.packed;
	}
	return size != 0;
}

unsigned centre(const Param &q)
{
	return q.size == 1 && q.bits < 7 && q.max >= int64_t(1) << q.bits ? 64u - (1u << (q.bits - 1)) : 0;
}

// a record's bits one after another, in the map's order within a byte
struct Bits {
	bool high;
	size_t at;
	unsigned get(const uint8_t *rec)
	{
		const unsigned v = rec[at >> 3] >> (high ? 7 - (at & 7) : at & 7) & 1;
		at++;
		return v;
	}
	void put(uint8_t *rec, unsigned v)
	{
		rec[at >> 3] |= uint8_t((v & 1) << (high ? 7 - (at & 7) : at & 7));
		at++;
	}
};

}

bool unpack(const Map &m, const Area &a, const uint8_t *rec, size_t size, std::vector<std::vector<uint8_t>> &units)
{
	std::vector<size_t> starts;
	size_t need;
	if (!laid(m, a, starts, need) || size < need)
		return false;
	for (uint32_t k = 0; k < a.count; k++) {
		const Block &b = m.blocks[m.entries[a.first + k].block];
		if (starts[k] == ABSENT) {
			units.emplace_back();
			continue;
		}
		std::vector<uint8_t> u(b.size, 0);
		Bits bits { m.high_first, 8 * starts[k] + b.lead };
		for (uint32_t p = 0; p < b.count; p++) {
			const Param &q = b.params[p];
			if (q.flags & ALIGNED)
				bits.at = (bits.at + 7) / 8 * 8;
			for (uint32_t n = 0; n < q.size; n++) {
				unsigned v = 0;
				for (unsigned i = 0; i < q.bits; i++)
					v |= bits.get(rec) << (m.high_first ? q.bits - 1 - i : i);
				u[q.offset + (m.high_first ? n : q.size - 1 - n)] = uint8_t(v + centre(q));
			}
		}
		units.push_back(std::move(u));
	}
	return true;
}

bool pack(const Map &m, const Area &a, const std::vector<std::vector<uint8_t>> &units, std::vector<uint8_t> &rec)
{
	std::vector<size_t> starts;
	size_t need;
	if (!laid(m, a, starts, need) || units.size() != a.count)
		return false;
	rec.assign(need, 0);
	for (uint32_t k = 0; k < a.count; k++) {
		const Block &b = m.blocks[m.entries[a.first + k].block];
		if (starts[k] == ABSENT)
			continue;
		if (units[k].size() != b.size)
			return false;
		Bits bits { m.high_first, 8 * starts[k] + b.lead };
		for (uint32_t p = 0; p < b.count; p++) {
			const Param &q = b.params[p];
			if (q.flags & ALIGNED)
				bits.at = (bits.at + 7) / 8 * 8;
			for (uint32_t n = 0; n < q.size; n++) {
				const unsigned v = units[k][q.offset + (m.high_first ? n : q.size - 1 - n)] - centre(q);
				for (unsigned i = 0; i < q.bits; i++)
					bits.put(rec.data(), v >> (m.high_first ? q.bits - 1 - i : i));
			}
		}
	}
	return true;
}

}
