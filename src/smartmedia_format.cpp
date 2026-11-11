// Copyright 2026 Ian Karlsson
// A SmartMedia card formatted: its card information block and a FAT volume in its first logical blocks, as an XV-5080
// leaves a card it has formatted (docs/formats.md, "SmartMedia cards")
#include "store.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace syfs {

namespace {

constexpr uint64_t HEADER = 1024;
constexpr uint32_t PAGE = 512, SPARE = 16, SECTOR = 512;

// a card of 512-byte pages by its megabytes: its device code and pages a block, the volume's geometry, where its
// partition starts and what the partition's entry says of its ends, and its FAT
struct Card {
	unsigned mb;
	uint8_t code;
	uint32_t pages;
	uint8_t heads, track;
	uint32_t start;
	uint8_t type, from[3], to[3];
	uint8_t cluster;
	uint16_t fat;
	int bits;
};

constexpr Card cards[] = {
	{ 8, 0xE6, 16, 4, 16, 25, 0x01, { 0x01, 0x0A, 0x00 }, { 0x03, 0x10, 0xF9 }, 16, 3, 12 },
	{ 16, 0x73, 32, 4, 16, 41, 0x01, { 0x02, 0x0A, 0x00 }, { 0x03, 0x10, 0xF3 }, 32, 3, 12 },
	{ 32, 0x75, 32, 8, 16, 35, 0x01, { 0x02, 0x04, 0x00 }, { 0x07, 0x10, 0xF3 }, 32, 6, 12 },
	{ 64, 0x76, 32, 8, 32, 55, 0x01, { 0x01, 0x18, 0x00 }, { 0x07, 0x20, 0xF3 }, 32, 12, 12 },
	{ 128, 0x79, 32, 16, 32, 47, 0x06, { 0x01, 0x10, 0x00 }, { 0x0F, 0x20, 0xF3 }, 32, 32, 16 },
};

// the card information structure every card's first block starts with, twice in its first page
constexpr uint8_t cis[] = {
	0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02, 0xDF, 0x01, 0x20, 0x04, 0x00, 0x00, 0x00, 0x00, 0x21,
	0x02, 0x04, 0x01, 0x22, 0x02, 0x01, 0x01, 0x22, 0x03, 0x02, 0x04, 0x07, 0x1A, 0x05, 0x01, 0x03,
	0x00, 0x02, 0x0F, 0x1B, 0x08, 0xC0, 0xC0, 0xA1, 0x01, 0x55, 0x08, 0x00, 0x20, 0x1B, 0x0A, 0xC1,
	0x41, 0x99, 0x01, 0x55, 0x64, 0xF0, 0xFF, 0xFF, 0x20, 0x1B, 0x0C, 0x82, 0x41, 0x18, 0xEA, 0x61,
	0xF0, 0x01, 0x07, 0xF6, 0x03, 0x01, 0xEE, 0x1B, 0x0C, 0x83, 0x41, 0x18, 0xEA, 0x61, 0x70, 0x01,
	0x07, 0x76, 0x03, 0x01, 0xEE, 0x15, 0x14, 0x05, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x00, 0x20, 0x20, 0x20, 0x20, 0x00, 0x30, 0x2E, 0x30, 0x00, 0xFF, 0x14, 0x00, 0xFF,
};

void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

// the three bytes that guard 256 of a page: the parity of every byte's bits by their place in the byte (six, in the
// third's upper bits, the lower two set) and by each bit of the byte's place (sixteen, in the first two), inverted
void guard(const uint8_t *data, uint8_t *out)
{
	unsigned columns = 0, lines = 0;
	for (unsigned i = 0; i < 256; i++) {
		const uint8_t v = data[i];
		auto bit = [&](int k) { return unsigned(v >> k & 1); };
		columns ^= (bit(0) ^ bit(2) ^ bit(4) ^ bit(6)) | (bit(1) ^ bit(3) ^ bit(5) ^ bit(7)) << 1
			| (bit(0) ^ bit(1) ^ bit(4) ^ bit(5)) << 2 | (bit(2) ^ bit(3) ^ bit(6) ^ bit(7)) << 3
			| (bit(0) ^ bit(1) ^ bit(2) ^ bit(3)) << 4 | (bit(4) ^ bit(5) ^ bit(6) ^ bit(7)) << 5;
		unsigned odd = v;
		odd ^= odd >> 4, odd ^= odd >> 2, odd ^= odd >> 1;
		if (odd & 1)
			for (int k = 0; k < 8; k++)
				lines ^= 1u << (2 * k + (i >> k & 1));
	}
	out[0] = uint8_t(~lines);
	out[1] = uint8_t(~(lines >> 8));
	out[2] = uint8_t(~(columns << 2) | 3);
}

// a page's spare area: ones, the block's address twice, and the guards of the page's second half and its first
void spare_of(const uint8_t *page, uint32_t address, uint8_t *out)
{
	memset(out, 0xFF, SPARE);
	out[6] = out[11] = uint8_t(address >> 8);
	out[7] = out[12] = uint8_t(address);
	guard(page + 256, out + 8);
	guard(page, out + 13);
}

// a logical block's address: 00010, its number in its zone, and a bit that makes the ones even
uint32_t address_of(uint32_t logical)
{
	uint32_t a = 0x1000 | logical << 1, ones = a;
	ones ^= ones >> 8, ones ^= ones >> 4, ones ^= ones >> 2, ones ^= ones >> 1;
	return a | (ones & 1);
}

}

bool SmartMedia::format(const syfs_io &io, const char *label, std::string &why)
{
	const Card *c = nullptr;
	for (const Card &k : cards)
		if (io.size == HEADER + uint64_t(k.mb) * 1048576 / PAGE * (PAGE + SPARE))
			c = &k;
	if (!c)
		return why = "a formatted card is one of 8, 16, 32, 64 or 128 MB", false;
	// the label, a DOS name's eleven characters, its letters upper-cased
	uint8_t name[11];
	const size_t named = label ? strlen(label) : 0;
	memset(name, ' ', sizeof name);
	for (size_t k = 0; k < named; k++) {
		const uint8_t ch = uint8_t(label[k]);
		if (named > 11 || ch < 0x20 || ch > 0x7E || strchr("\"*+,./:;<=>?[\\]|", ch))
			return why = "the label is more than 11 characters or holds one a DOS name cannot", false;
		name[k] = ch >= 'a' && ch <= 'z' ? uint8_t(ch - 32) : ch;
	}

	// the volume: the partition's sector, the boot sector, two FATs, a root of 256 entries, then the clusters, of
	// which the first is the directory ROLAND; all in the card's first logical blocks
	const uint32_t per_block = c->pages, total = c->mb * 2000u, volume = total - c->start;
	const uint32_t fat_at = c->start + 1, root_at = fat_at + 2u * c->fat, data_at = root_at + 16;
	const uint32_t blocks = (data_at + c->cluster + per_block - 1) / per_block;
	std::vector<uint8_t> v(size_t(blocks) * per_block * SECTOR, 0);
	uint8_t *mbr = &v[0], *boot = &v[size_t(c->start) * SECTOR];
	mbr[446] = 0x80;
	memcpy(mbr + 447, c->from, 3);
	mbr[450] = c->type;
	memcpy(mbr + 451, c->to, 3);
	put32(mbr + 454, c->start), put32(mbr + 458, volume);
	mbr[510] = 0x55, mbr[511] = 0xAA;
	boot[0] = 0xE9;
	memcpy(boot + 3, "RJA2.00 ", 8);
	put16(boot + 11, SECTOR);
	boot[13] = c->cluster;
	put16(boot + 14, 1);
	boot[16] = 2;
	put16(boot + 17, 256);
	put16(boot + 19, volume < 0x10000 ? volume : 0);
	boot[21] = 0xF8;
	put16(boot + 22, c->fat);
	put16(boot + 24, c->track), put16(boot + 26, c->heads);
	put32(boot + 28, c->start), put32(boot + 32, volume < 0x10000 ? 0 : volume);
	boot[38] = 0x29;
	put16(boot + 39, uint32_t(time(nullptr)));
	// the label here ends in zeros, in the root in blanks; without one it is the name that says so
	if (named)
		memcpy(boot + 43, name, named);
	else
		memcpy(boot + 43, "NO NAME    ", 11);
	memcpy(boot + 54, c->bits == 16 ? "FAT16   " : "FAT12   ", 8);
	boot[510] = 0x55, boot[511] = 0xAA;
	// each FAT: the media byte and ones, and cluster 2, the directory's, its chain's end
	static const uint8_t fat12[] = { 0xF8, 0xFF, 0xFF, 0xFF, 0x0F }, fat16[] = { 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	for (uint32_t f = 0; f < 2; f++)
		memcpy(&v[size_t(fat_at + f * c->fat) * SECTOR], c->bits == 16 ? fat16 : fat12, c->bits == 16 ? sizeof fat16 : sizeof fat12);
	// the root: the label, and ROLAND; every entry's time 15:20 on the first of April 2003
	auto entry = [&](uint8_t *e, const void *short_name, uint8_t attr, uint32_t first) {
		memcpy(e, short_name, 11);
		e[11] = attr;
		put16(e + 22, 0x7A80), put16(e + 24, 0x2E81);
		put16(e + 26, first);
	};
	uint8_t *root = &v[size_t(root_at) * SECTOR], *dir = &v[size_t(data_at) * SECTOR];
	if (named) {
		entry(root, name, 0x08, 0);
		root += 32;
	}
	entry(root, "ROLAND     ", 0x10, 2);
	entry(dir, ".          ", 0x10, 2);
	entry(dir + 32, "..         ", 0x10, 0);

	// the card: its header; block 0, the card information in its first page and no logical address; each logical
	// block n in block n + 1, every page with its address and guards; the rest erased
	auto put = [&](uint64_t at, const void *p, size_t n) {
		if (io.write(io.user, at, p, n) == n)
			return true;
		why = "the image could not be written";
		return false;
	};
	const size_t row = PAGE + SPARE;
	std::vector<uint8_t> block(size_t(per_block) * row);
	uint8_t head[HEADER] = { 0xEC, c->code, 0xA5 };
	if (!put(0, head, sizeof head))
		return false;
	const uint32_t physical = uint32_t((io.size - HEADER) / row / per_block);
	for (uint32_t b = 0; b < physical; b++) {
		std::fill(block.begin(), block.end(), uint8_t(0xFF));
		if (b == 0) {
			memset(&block[0], 0, PAGE);
			memcpy(&block[0], cis, sizeof cis);
			memcpy(&block[256], cis, sizeof cis);
			spare_of(&block[0], 0, &block[PAGE]);
		} else if (b <= blocks)
			for (uint32_t p = 0; p < per_block; p++) {
				memcpy(&block[p * row], &v[(size_t(b - 1) * per_block + p) * SECTOR], PAGE);
				spare_of(&block[p * row], address_of(b - 1), &block[p * row + PAGE]);
			}
		if (!put(HEADER + uint64_t(b) * block.size(), block.data(), block.size()))
			return false;
	}
	return true;
}

}
