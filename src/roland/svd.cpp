// Copyright 2026 Ian Karlsson
// An XV's or a Fantom's .SVD file: its user memory as it saves it to a card or a disk (docs/formats.md, "XV-5080,
// XV-3080 and Fantom files")
#include "roland.hpp"

namespace syfs::roland {

namespace {

constexpr uint64_t MAX_SIZE = 16 * 1024 * 1024;
constexpr uint32_t HEAD = 16, ENTRY = 16;
struct Chunk {
	const char *tag, *model;
	syfs_key kind;
	uint32_t size;       // a record's bytes that are read, from its start
	uint32_t more_at, more;  // and those further on that follow them in the packed record
	const char *bank;
};
struct Whose {
	syfs_key format;
	const char *map;
	Chunk chunks[4];
};
constexpr char XV[] = "XV88", FANTOM[] = "NEXY";
constexpr uint32_t XV3080_PATCH = 574;
// a Fantom's patch is in an XV-5080's 938 bytes, its tones' 574 and then the split keys' place, with its controller's
// 18 after
constexpr Whose files[] = {
	{ SYFS_FORMAT_XV5080, "xv5080", { { "PRFa", XV, SYFS_TYPE_PERFORMANCE, 926, 0, 0, "User Performance" },
		{ "PATa", XV, SYFS_TYPE_PATCH, 574, 0, 0, "User Patch" }, { "RHYa", XV, SYFS_TYPE_RHYTHM_SET, 10808, 0, 0, "User Rhythm" } } },
	{ SYFS_FORMAT_XV3080, "xv3080", { { "PRFa", XV, SYFS_TYPE_PERFORMANCE, 926, 0, 0, "User Performance" },
		{ "PATa", XV, SYFS_TYPE_PATCH, 574, 0, 0, "User Patch" }, { "RHYa", XV, SYFS_TYPE_RHYTHM_SET, 10808, 0, 0, "User Rhythm" } } },
	{ SYFS_FORMAT_FANTOM, "fantom", { { "PRFa", FANTOM, SYFS_TYPE_PERFORMANCE, 1126, 0, 0, "User Performance" },
		{ "MLTa", FANTOM, SYFS_TYPE_MULTI, 954, 0, 0, "User Multitimbre" },
		{ "PATa", XV, SYFS_TYPE_PATCH, 574, 938, 18, "User Patch" }, { "RHYa", XV, SYFS_TYPE_RHYTHM_SET, 10824, 0, 0, "User Rhythm" } } },
};
const Whose *whose(syfs_key format)
{
	for (const Whose &w : files)
		if (w.format == format)
			return &w;
	return nullptr;
}

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
// place and size.  A Fantom's first chunk is of its own model.  The two XVs' files are alike but for a patch's
// record: an XV-5080's keeps 938 bytes of it, an XV-3080's the 574 that are read
syfs_key is(const Read &read, uint64_t size)
{
	uint8_t h[HEAD + ENTRY];
	if (size < sizeof h || size > MAX_SIZE || !read(0, h, sizeof h) || memcmp(h + 2, "SVD1", 4))
		return SYFS_NONE;
	if (!memcmp(h + HEAD + 4, FANTOM, 4))
		return SYFS_FORMAT_FANTOM;
	if (memcmp(h + HEAD + 4, XV, 4))
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
	const Whose *w = whose(is(read, size));
	if (!w || !read(0, h, sizeof h))
		return;
	const uint32_t count = (be(h, 2) + 2 - HEAD) / ENTRY;
	for (uint32_t n = 0; n < count && n < 64; n++) {
		uint8_t e[ENTRY], head[HEAD];
		if (HEAD + uint64_t(n + 1) * ENTRY > size || !read(HEAD + uint64_t(n) * ENTRY, e, sizeof e))
			continue;
		const uint64_t at = be(e + 8, 4);
		for (const Chunk &c : w->chunks) {
			if (!c.tag || memcmp(e, c.tag, 4) || memcmp(e + 4, c.model, 4) || at + sizeof head > size || !read(at, head, sizeof head))
				continue;
			const uint32_t records = be(head, 4), stride = be(head + 4, 4), first = be(head + 8, 4);
			if (stride < c.size || stride < c.more_at + c.more)
				continue;
			for (uint32_t k = 0; k < records; k++) {
				const uint64_t r = at + first + uint64_t(k) * stride;
				uint8_t name[11];
				if (r + stride > size || !read(r, name, sizeof name))
					break;
				if (named(name, true))
					found({ c.kind, k, xv_name(name), c.size + c.more, r });
			}
		}
	}
}

bool data_sets(const Read &read, syfs_key format, syfs_key kind, uint32_t number, uint64_t at, std::vector<uint8_t> &out)
{
	const Whose *w = whose(format);
	for (const Chunk &c : w ? w->chunks : files[0].chunks)
		if (w && c.tag && c.kind == kind) {
			std::vector<uint8_t> rec(c.size + c.more);
			return read(at, rec.data(), c.size) && (!c.more || read(at + c.more_at, &rec[c.size], c.more))
				&& syx::packed(w->map, c.bank, number, 0x10, rec, out);
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
