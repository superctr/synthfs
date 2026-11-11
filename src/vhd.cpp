// Copyright 2026 Ian Karlsson
// A VHD's sectors read, and a dynamic VHD written (docs/formats.md, "VHD")
#include "store.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <random>

namespace syfs {

namespace {

constexpr uint32_t SECTOR = 512, FOOTER = 512, HEADER = 1024, BLOCK = 0x200000;
constexpr uint32_t FIXED = 2, DYNAMIC = 3;
constexpr uint32_t NO_BLOCK = 0xFFFFFFFF;
// what a BAT of 32-bit sector numbers reaches, and the specification's limit on a disk
constexpr uint64_t LARGEST = uint64_t(2040) << 30;

uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1] << 16 | p[2] << 8 | p[3]); }
uint64_t be64(const uint8_t *p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

void put32(uint8_t *p, uint32_t v)
{
	p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}

void put64(uint8_t *p, uint64_t v)
{
	put32(p, uint32_t(v >> 32)), put32(p + 4, uint32_t(v));
}

// the one's complement of the sum of a structure's bytes, its checksum field left out
uint32_t checksum(const uint8_t *p, size_t n, size_t field)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < n; i++)
		if (i < field || i >= field + 4)
			sum += p[i];
	return ~sum;
}

bool footer_at(const Store &s, uint64_t at, uint8_t *f)
{
	return s.read(at, f, FOOTER) && !memcmp(f, "conectix", 8) && be32(f + 64) == checksum(f, FOOTER, 64);
}

// a disk's cylinders, heads and sectors a track, as the specification counts them from its sectors
void geometry(uint64_t sectors, uint8_t *out)
{
	uint32_t total = uint32_t(std::min<uint64_t>(sectors, 65535u * 16 * 255)), spt, heads, cth;
	if (total >= 65535u * 16 * 63) {
		spt = 255;
		heads = 16;
		cth = total / spt;
	} else {
		spt = 17;
		cth = total / spt;
		heads = std::max<uint32_t>((cth + 1023) / 1024, 4);
		if (cth >= heads * 1024 || heads > 16) {
			spt = 31;
			heads = 16;
			cth = total / spt;
		}
		if (cth >= heads * 1024) {
			spt = 63;
			heads = 16;
			cth = total / spt;
		}
	}
	uint32_t cylinders = cth / heads;
	out[0] = uint8_t(cylinders >> 8), out[1] = uint8_t(cylinders), out[2] = uint8_t(heads), out[3] = uint8_t(spt);
}

}

std::shared_ptr<const Store> Vhd::over(std::shared_ptr<const Store> under)
{
	uint8_t f[FOOTER];
	const uint64_t whole = under->size();
	if (whole < FOOTER || !footer_at(*under, whole - FOOTER, f))
		return nullptr;
	const uint32_t type = be32(f + 60);
	const uint64_t size = be64(f + 48);
	if (!size || size % SECTOR || size > LARGEST)
		return nullptr;
	std::shared_ptr<Vhd> v(new Vhd);
	v->size_ = size;
	if (type == FIXED) {
		if (size > whole - FOOTER)
			return nullptr;
	} else if (type == DYNAMIC) {
		uint8_t h[HEADER];
		const uint64_t at = be64(f + 16);
		if (at > whole || whole - at < HEADER || !under->read(at, h, HEADER) || memcmp(h, "cxsparse", 8)
			|| be32(h + 36) != checksum(h, HEADER, 36))
			return nullptr;
		const uint64_t table = be64(h + 16);
		const uint32_t entries = be32(h + 28);
		v->block_ = be32(h + 32);
		// whole sectors a block, a power of two of them
		if (v->block_ < SECTOR || v->block_ & (v->block_ - 1) || uint64_t(entries) * v->block_ < size
			|| table > whole || (whole - table) / 4 < entries)
			return nullptr;
		v->bitmap_ = (v->block_ / SECTOR / 8 + SECTOR - 1) / SECTOR * SECTOR;
		std::vector<uint8_t> raw(size_t(entries) * 4);
		if (!under->read(table, raw.data(), raw.size()))
			return nullptr;
		v->bat_.resize(entries);
		for (uint32_t i = 0; i < entries; i++)
			v->bat_[i] = be32(&raw[size_t(i) * 4]);
	} else
		return nullptr;
	v->under_ = std::move(under);
	return v;
}

bool Vhd::read(uint64_t offset, void *buf, size_t n) const
{
	if (offset > size_ || n > size_ - offset)
		return false;
	if (!block_)
		return under_->read(offset, buf, n);
	auto *out = static_cast<uint8_t *>(buf);
	while (n) {
		const uint64_t block = offset / block_;
		const uint32_t in = uint32_t(offset % block_);
		const size_t k = size_t(std::min<uint64_t>(n, block_ - in));
		// a block never written reads as zeros; one written is whole in the file, its bitmap aside
		if (bat_[size_t(block)] == NO_BLOCK)
			memset(out, 0, k);
		else if (!under_->read(uint64_t(bat_[size_t(block)]) * SECTOR + bitmap_ + in, out, k))
			return false;
		out += k;
		offset += k;
		n -= k;
	}
	return true;
}

uint64_t Vhd::write(const Store &from, const syfs_io &to, std::string &why)
{
	const uint64_t size = from.size();
	if (!size || size % SECTOR || size > LARGEST) {
		why = "the image is not whole 512-byte sectors, or is larger than a VHD holds";
		return 0;
	}
	auto put = [&](uint64_t at, const void *p, size_t n) {
		if (to.write(to.user, at, p, n) == n)
			return true;
		why = "the image could not be written";
		return false;
	};
	const uint32_t entries = uint32_t((size + BLOCK - 1) / BLOCK);
	const uint32_t bitmap = (BLOCK / SECTOR / 8 + SECTOR - 1) / SECTOR * SECTOR;
	const uint64_t table = FOOTER + HEADER, table_bytes = (uint64_t(entries) * 4 + SECTOR - 1) / SECTOR * SECTOR;

	uint8_t f[FOOTER] = {};
	memcpy(f, "conectix", 8);
	put32(f + 8, 2);                       // features: reserved, always set
	put32(f + 12, 0x00010000);             // file format version
	put64(f + 16, FOOTER);                 // the dynamic header
	// seconds since the first of January 2000, UTC
	put32(f + 24, uint32_t(std::max<int64_t>(int64_t(time(nullptr)) - 946684800, 0)));
	memcpy(f + 28, "syfs", 4);             // creator application
	put32(f + 32, 0x00010000);             // its version
	memcpy(f + 36, "Wi2k", 4);             // creator host
	put64(f + 40, size);
	put64(f + 48, size);
	geometry(size / SECTOR, f + 56);
	put32(f + 60, DYNAMIC);
	std::random_device random;
	for (int i = 0; i < 16; i++)
		f[68 + i] = uint8_t(random());
	put32(f + 64, checksum(f, FOOTER, 64));

	uint8_t h[HEADER] = {};
	memcpy(h, "cxsparse", 8);
	put64(h + 8, ~uint64_t(0));
	put64(h + 16, table);
	put32(h + 24, 0x00010000);
	put32(h + 28, entries);
	put32(h + 32, BLOCK);
	put32(h + 36, checksum(h, HEADER, 36));

	// the blocks that hold anything, one after another past the table, each behind a bitmap of ones
	std::vector<uint8_t> bat(size_t(table_bytes), 0xFF), block(bitmap + BLOCK);
	uint64_t at = table + table_bytes;
	for (uint32_t b = 0; b < entries; b++) {
		const uint64_t start = uint64_t(b) * BLOCK;
		const size_t n = size_t(std::min<uint64_t>(BLOCK, size - start));
		std::fill(block.begin(), block.begin() + bitmap, uint8_t(0));
		std::fill(block.begin(), block.begin() + BLOCK / SECTOR / 8, uint8_t(0xFF));
		std::fill(block.begin() + bitmap + ptrdiff_t(n), block.end(), uint8_t(0));
		if (!from.read(start, &block[bitmap], n)) {
			why = "the image could not be read";
			return 0;
		}
		if (std::all_of(block.begin() + bitmap, block.end(), [](uint8_t v) { return !v; }))
			continue;
		put32(&bat[size_t(b) * 4], uint32_t(at / SECTOR));
		if (!put(at, block.data(), block.size()))
			return 0;
		at += block.size();
	}
	if (!put(0, f, FOOTER) || !put(FOOTER, h, HEADER) || !put(table, bat.data(), bat.size()) || !put(at, f, FOOTER))
		return 0;
	return at + FOOTER;
}

}
