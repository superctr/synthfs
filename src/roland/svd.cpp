// Copyright 2026 Ian Karlsson
// An XV-5080's .SVD file: its user memory as it saves it to a card or a disk (docs/formats.md, "XV-5080 files")
#include "roland.hpp"

namespace syfs::roland {

namespace {

constexpr uint64_t MAX_SIZE = 16 * 1024 * 1024;
constexpr uint32_t HEAD = 16, ENTRY = 16;
struct Chunk {
	const char *tag;
	syfs_key kind;
	uint32_t size;       // a record's bytes that are read
	const char *bank;
};
constexpr Chunk chunks[] = { { "PRFa", SYFS_TYPE_PERFORMANCE, 926, "User Performance" },
	{ "PATa", SYFS_TYPE_PATCH, 574, "User Patch" }, { "RHYa", SYFS_TYPE_RHYTHM_SET, 10808, "User Rhythm" } };
constexpr char MODEL[] = "XV88";

uint32_t be(const uint8_t *p, int n)
{
	uint32_t v = 0;
	for (int k = 0; k < n; k++)
		v = v << 8 | p[k];
	return v;
}

}

// the length of its head less two, "SVD1", ten bytes, then an entry for each chunk: a tag, the model, the chunk's
// place and size
syfs_key Svd::probe(const Image &im)
{
	uint8_t h[HEAD + ENTRY];
	if (im.size() > MAX_SIZE || !im.read(0, h, sizeof h) || memcmp(h + 2, "SVD1", 4) || memcmp(h + HEAD + 4, MODEL, 4))
		return SYFS_NONE;
	return SYFS_FORMAT_XV5080;
}

// a chunk: a count, a record's size and the head's own length (longs), then the records
void Svd::list()
{
	uint8_t h[HEAD];
	if (!image.read(0, h, sizeof h))
		return;
	const uint32_t count = (be(h, 2) + 2 - HEAD) / ENTRY;
	for (uint32_t n = 0; n < count && n < 64; n++) {
		uint8_t e[ENTRY], head[HEAD];
		if (!image.read(HEAD + uint64_t(n) * ENTRY, e, sizeof e) || memcmp(e + 4, MODEL, 4))
			continue;
		const uint64_t at = be(e + 8, 4);
		for (const Chunk &c : chunks) {
			if (memcmp(e, c.tag, 4) || !image.read(at, head, sizeof head))
				continue;
			const uint32_t records = be(head, 4), stride = be(head + 4, 4), first = be(head + 8, 4);
			if (stride < c.size)
				continue;
			for (uint32_t k = 0; k < records; k++) {
				const uint64_t r = at + first + uint64_t(k) * stride;
				uint8_t name[11];
				if (r + c.size > image.size() || !image.read(r, name, sizeof name))
					break;
				if (named(name, true))
					add(c.kind, format, k, syfs::NONE, xv_name(name), c.size, r);
			}
		}
	}
}

bool Svd::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	const syfs::Entry &e = entries[i];
	for (const Chunk &c : chunks)
		if (c.kind == e.kind) {
			const auto rec = image.read(where[i], c.size);
			return !rec.empty() && syx::packed("xv5080", c.bank, e.number, 0x10, rec, out);
		}
	return false;
}

std::unique_ptr<Medium> Svd::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Svd>(io, format);
}

}
