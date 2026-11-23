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

// what a block of a map is listed as, and the block that extends it for the same number
struct Area {
	const char *block;
	syfs_key kind;
	const char *extended;
};
struct Family {
	const char *map;     // the map its files are read by
	syfs_key format;     // what its entries' records are
	const Area *areas;
	size_t count;
};

constexpr Area s700_areas[] = {
	{ "volume", SYFS_TYPE_VOLUME, nullptr },
	{ "performance", SYFS_TYPE_PERFORMANCE, "ext-performance" },
	{ "patch", SYFS_TYPE_PATCH, nullptr },
	{ "partial", SYFS_TYPE_PARTIAL, "ext-partial" },
	{ "sample", SYFS_TYPE_SAMPLE, nullptr },
};
// the S-700 series share a model ID: the S-760's map has every block of the others'
constexpr Family families[] = {
	{ "s760", SYFS_FORMAT_S760, s700_areas, std::size(s700_areas) },
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
		if (m && bytes.size() > size_t(3 + m->model_id_size) && !memcmp(&bytes[3], m->model_id, m->model_id_size))
			family = &f, map_ = m;
	}
	if (!family)
		return;
	// every Data Set's units put where the map has them
	std::map<const map::Entry *, Filled> filled;
	for (size_t at = 0; at < bytes.size();) {
		if (bytes[at] != EXCLUSIVE) {
			at++;
			continue;
		}
		size_t end = at + 1;
		while (end < bytes.size() && bytes[end] < 0x80)
			end++;
		Message msg;
		if (end < bytes.size() && bytes[end] == END && message(bytes, at, end, *map_, msg)) {
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
		}
		at = end;
	}
	auto whole = [&](const map::Entry *e) {
		auto f = filled.find(e);
		return e && f != filled.end() && f->second.have == f->second.units.size() ? &f->second : nullptr;
	};
	for (size_t a = 0; a < family->count; a++) {
		const Area &area = family->areas[a];
		const map::Block *b = map_->block(area.block), *eb = area.extended ? map_->block(area.extended) : nullptr;
		uint32_t n = 0;
		for (uint32_t k = 0; b && k < map_->entry_count; k++) {
			const map::Entry *e = &map_->entries[k];
			if (&map_->blocks[e->block] != b)
				continue;
			const uint32_t number = n++;
			const Filled *f = whole(e);
			if (!f)
				continue;
			// the name: a character of name_units units each, the high one first; an S-700 performance's record may
			// have a number with its top bit set where the colon is, the name's fourth
			const unsigned bits = b->count ? b->params[0].bits : 7;
			std::string name;
			uint8_t first[16] = {};
			for (uint32_t c = 0; c < b->name_size; c++) {
				unsigned v = 0;
				for (uint32_t u = 0; u < b->name_units; u++)
					v = v << bits | f->units[b->name_at + c * b->name_units + u];
				if (family->format == SYFS_FORMAT_S760 && area.kind == SYFS_TYPE_PERFORMANCE && c == 3 && v & 0x80)
					v = ':';
				name += v >= 32 && v < 127 ? char(v) : '?';
				if (c < sizeof first)
					first[c] = uint8_t(v);
			}
			const size_t i = add(area.kind, family->format, number, syfs::NONE, name, 0, 0);
			if (family->format == SYFS_FORMAT_S760)
				tag(i, s700::category(first));
			Record r { f->device, e, nullptr, f->units, {} };
			if (const Filled *x = whole(eb ? map_->at(eb, number) : nullptr)) {
				r.ext_at = map_->at(eb, number);
				r.ext = x->units;
			}
			records_.push_back(std::move(r));
		}
	}
}

// what an S-700 record names, by the numbers in it: the entries of the type named with those numbers
std::vector<size_t> Syx::refs(size_t i) const
{
	const Record &r = records_[i];
	std::vector<uint8_t> rec(r.units.size() / 2);
	for (size_t k = 0; k < rec.size(); k++)
		rec[k] = uint8_t(r.units[2 * k] << 4 | (r.units[2 * k + 1] & 15));
	std::vector<int32_t> nums;
	const syfs_key kind = entries[i].format == SYFS_FORMAT_S760 ? s700::named(entries[i].kind, rec.data(), nums) : syfs_key(SYFS_NONE);
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
	syx::put(out, *map_, r.device, r.at->addr, r.units.data(), r.units.size());
	if (r.ext_at)
		syx::put(out, *map_, r.device, r.ext_at->addr, r.ext.data(), r.ext.size());
	return true;
}

std::unique_ptr<Medium> Syx::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Syx>(io, format);
}

}
