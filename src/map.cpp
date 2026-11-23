// Copyright 2026 Ian Karlsson
// The parameter address maps' tables read: blocks, places and packed records (docs/maps.md)
#include "maps.hpp"

namespace syfs::map {

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

// each block of the area and where its bytes start in the record; false where one is in no packed record
bool laid(const Map &m, const Area &a, std::vector<size_t> &starts, size_t &size)
{
	size = 0;
	for (uint32_t k = 0; k < a.count; k++) {
		const Block &b = m.blocks[m.entries[a.first + k].block];
		if (!b.packed)
			return false;
		starts.push_back(size);
		size += b.packed;
	}
	return a.count != 0;
}

}

bool unpack(const Map &m, const Area &a, const uint8_t *rec, size_t size, std::vector<std::vector<uint8_t>> &units)
{
	std::vector<size_t> starts;
	size_t need;
	if (!laid(m, a, starts, need) || size < need)
		return false;
	for (uint32_t k = 0; k < a.count; k++) {
		const Block &b = m.blocks[m.entries[a.first + k].block];
		std::vector<uint8_t> u(b.size, 0);
		size_t bit = 8 * starts[k];
		for (uint32_t p = 0; p < b.count; p++) {
			const Param &q = b.params[p];
			for (uint32_t n = q.size; n--;) {
				unsigned v = 0;
				for (unsigned i = 0; i < q.bits; i++, bit++)
					v |= unsigned(rec[bit >> 3] >> (bit & 7) & 1) << i;
				u[q.offset + n] = uint8_t(v);
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
		if (units[k].size() != b.size)
			return false;
		size_t bit = 8 * starts[k];
		for (uint32_t p = 0; p < b.count; p++) {
			const Param &q = b.params[p];
			for (uint32_t n = q.size; n--;)
				for (unsigned i = 0; i < q.bits; i++, bit++)
					rec[bit >> 3] |= uint8_t((units[k][q.offset + n] >> i & 1) << (bit & 7));
		}
	}
	return true;
}

}
