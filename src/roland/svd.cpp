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
constexpr uint32_t XV3080_PATCH = 574;

uint32_t be(const uint8_t *p, int n)
{
	uint32_t v = 0;
	for (int k = 0; k < n; k++)
		v = v << 8 | p[k];
	return v;
}

}

namespace svd {

// the length of its head less two, "SVD1", ten bytes, then an entry for each chunk: a tag, the model, the chunk's
// place and size.  The two XVs' files are alike but for a patch's record: an XV-5080's keeps 938 bytes of it, an
// XV-3080's the 574 that are read
syfs_key is(const Read &read, uint64_t size)
{
	uint8_t h[HEAD + ENTRY];
	if (size < sizeof h || size > MAX_SIZE || !read(0, h, sizeof h) || memcmp(h + 2, "SVD1", 4) || memcmp(h + HEAD + 4, MODEL, 4))
		return SYFS_NONE;
	const uint32_t count = (be(h, 2) + 2 - HEAD) / ENTRY;
	for (uint32_t n = 0; n < count && n < 64; n++) {
		uint8_t e[ENTRY], head[HEAD];
		if (HEAD + uint64_t(n + 1) * ENTRY > size || !read(HEAD + uint64_t(n) * ENTRY, e, sizeof e) || memcmp(e, "PATa", 4))
			continue;
		const uint64_t at = be(e + 8, 4);
		if (at + sizeof head <= size && read(at, head, sizeof head) && be(head + 4, 4) == XV3080_PATCH)
			return SYFS_FORMAT_XV3080;
	}
	return SYFS_FORMAT_XV5080;
}

// a chunk: a count, a record's size and the head's own length (longs), then the records
void list(const Read &read, uint64_t size, const std::function<void(const Record &)> &found)
{
	uint8_t h[HEAD];
	if (!read(0, h, sizeof h))
		return;
	const uint32_t count = (be(h, 2) + 2 - HEAD) / ENTRY;
	for (uint32_t n = 0; n < count && n < 64; n++) {
		uint8_t e[ENTRY], head[HEAD];
		if (HEAD + uint64_t(n + 1) * ENTRY > size || !read(HEAD + uint64_t(n) * ENTRY, e, sizeof e) || memcmp(e + 4, MODEL, 4))
			continue;
		const uint64_t at = be(e + 8, 4);
		for (const Chunk &c : chunks) {
			if (memcmp(e, c.tag, 4) || at + sizeof head > size || !read(at, head, sizeof head))
				continue;
			const uint32_t records = be(head, 4), stride = be(head + 4, 4), first = be(head + 8, 4);
			if (stride < c.size)
				continue;
			for (uint32_t k = 0; k < records; k++) {
				const uint64_t r = at + first + uint64_t(k) * stride;
				uint8_t name[11];
				if (r + c.size > size || !read(r, name, sizeof name))
					break;
				if (named(name, true))
					found({ c.kind, k, xv_name(name), c.size, r });
			}
		}
	}
}

bool data_sets(const Read &read, syfs_key format, syfs_key kind, uint32_t number, uint64_t at, std::vector<uint8_t> &out)
{
	for (const Chunk &c : chunks)
		if (c.kind == kind) {
			std::vector<uint8_t> rec(c.size);
			return read(at, rec.data(), rec.size()) && syx::packed(format == SYFS_FORMAT_XV3080 ? "xv3080" : "xv5080", c.bank, number, 0x10, rec, out);
		}
	return false;
}

}

syfs_key Svd::probe(const Image &im)
{
	return svd::is([&](uint64_t at, void *buf, size_t n) { return im.read(at, buf, n); }, im.size());
}

void Svd::list()
{
	svd::list([&](uint64_t at, void *buf, size_t n) { return image.read(at, buf, n); }, image.size(),
		[&](const svd::Record &r) { add(r.kind, format, r.number, syfs::NONE, r.name, r.bytes, r.at); });
}

bool Svd::data_sets(size_t i, std::vector<uint8_t> &out) const
{
	return svd::data_sets([&](uint64_t at, void *buf, size_t n) { return image.read(at, buf, n); }, format, entries[i].kind,
		entries[i].number, where[i], out);
}

std::unique_ptr<Medium> Svd::open(const syfs_io &io, syfs_key format)
{
	return std::make_unique<Svd>(io, format);
}

}
