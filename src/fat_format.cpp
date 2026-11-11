// Copyright 2026 Ian Karlsson
// Empty FAT volumes written: a DOS floppy, a hard disk behind an MBR (docs/formats.md, "Formatting")
#include "fat.hpp"

#include <ctime>

namespace syfs::fat {

namespace {

constexpr uint32_t SECTOR = 512;
constexpr uint32_t PARTITION_START = 63;
constexpr uint64_t FAT32_FROM = uint64_t(512) << 20;
constexpr uint32_t HEADS = 255, TRACK = 63;

struct Layout {
	int bits;
	uint32_t spc, reserved, fats = 2, root_entries;
	uint64_t total;
	uint32_t fat_sectors;
	uint8_t media;
	uint16_t track, heads;
	uint32_t hidden;
	uint8_t drive;

	uint64_t root_sectors() const { return (uint64_t(root_entries) * 32 + SECTOR - 1) / SECTOR; }
	uint64_t data_sector() const { return reserved + uint64_t(fats) * fat_sectors + root_sectors(); }
	uint64_t clusters() const { return (total - data_sector()) / spc; }
};

class Writer {
public:
	Writer(const syfs_io &io, std::string &why) : io_(io), why_(why) {}
	bool put(uint64_t at, const void *p, size_t n)
	{
		if (at > io_.size || n > io_.size - at || io_.write(io_.user, at, p, n) != n)
			return fail("the image could not be written");
		return true;
	}
	bool zero(uint64_t at, uint64_t n)
	{
		static const uint8_t zeros[65536] = {};
		while (n) {
			size_t k = size_t(std::min<uint64_t>(n, sizeof zeros));
			if (!put(at, zeros, k))
				return false;
			at += k;
			n -= k;
		}
		return true;
	}
	bool fail(const char *what)
	{
		why_ = what;
		return false;
	}

private:
	const syfs_io &io_;
	std::string &why_;
};

void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

// a label as an 11-byte short name, upper-cased; false where a character is not one a short name may hold
bool label_of(const char *label, uint8_t *out, bool &any)
{
	memset(out, ' ', 11);
	any = label && *label;
	if (!any)
		return true;
	size_t n = strlen(label);
	if (n > 11 || label[0] == ' ')
		return false;
	for (size_t k = 0; k < n; k++) {
		uint8_t c = uint8_t(label[k]);
		if (c >= 'a' && c <= 'z')
			c = uint8_t(c - 32);
		if (c < 0x20 || c >= 0x7F || strchr("\"*+,./:;<=>?[\\]|", c))
			return false;
		out[k] = c;
	}
	return true;
}

// the time now as DOS keeps it: the date (from 1980) in the high half, the time in two-second steps in the low
uint32_t dos_now()
{
	int64_t t = int64_t(std::time(nullptr));
	int64_t days = t / 86400, secs = t % 86400;
	// days since 1970 to a civil date
	int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	int64_t y = yoe + era * 400 + (m <= 2);
	if (y < 1980)
		y = 1980, m = 1, d = 1;
	uint32_t date = uint32_t((y - 1980) << 9 | m << 5 | d), time = uint32_t(secs / 3600 << 11 | secs / 60 % 60 << 5 | secs % 60 / 2);
	return date << 16 | time;
}

// the volume: its boot sector (and on FAT32 the FSInfo sector and their backups), its FATs' first entries, its root
// directory empty but for the label
bool write_volume(Writer &w, uint64_t base, const Layout &L, const char *label)
{
	uint8_t name[11];
	bool any;
	if (!label_of(label, name, any))
		return w.fail("the label is more than 11 characters or holds one a DOS name cannot");
	uint32_t now = dos_now();
	uint32_t serial = now ^ uint32_t(std::clock()) * 0x9E3779B1u;
	bool fat32 = L.bits == 32;
	uint64_t cluster = uint64_t(L.spc) * SECTOR;
	if (!w.zero(base, L.data_sector() * SECTOR + (fat32 ? cluster : 0)))
		return false;

	uint8_t bs[SECTOR] = {};
	bs[0] = 0xEB, bs[1] = fat32 ? 0x58 : 0x3C, bs[2] = 0x90;
	memcpy(bs + 3, "MSWIN4.1", 8);
	put16(bs + 11, SECTOR);
	bs[13] = uint8_t(L.spc);
	put16(bs + 14, L.reserved);
	bs[16] = uint8_t(L.fats);
	put16(bs + 17, L.root_entries);
	if (!fat32 && L.total < 65536)
		put16(bs + 19, uint32_t(L.total));
	else
		put32(bs + 32, uint32_t(L.total));
	bs[21] = L.media;
	put16(bs + 24, L.track);
	put16(bs + 26, L.heads);
	put32(bs + 28, L.hidden);
	uint8_t *ext = bs + 36;
	if (fat32) {
		put32(bs + 36, L.fat_sectors);
		put32(bs + 44, 2);
		put16(bs + 48, 1);
		put16(bs + 50, 6);
		ext = bs + 64;
	} else
		put16(bs + 22, L.fat_sectors);
	ext[0] = L.drive;
	ext[2] = 0x29;
	put32(ext + 3, serial);
	memcpy(ext + 7, any ? reinterpret_cast<const char *>(name) : "NO NAME    ", 11);
	memcpy(ext + 18, L.bits == 12 ? "FAT12   " : L.bits == 16 ? "FAT16   " : "FAT32   ", 8);
	// the code a BIOS would run: INT 18h (no system here), then halt
	uint8_t *code = ext + 26;
	code[0] = 0xCD, code[1] = 0x18, code[2] = 0xEB, code[3] = 0xFE;
	bs[510] = 0x55, bs[511] = 0xAA;
	if (!w.put(base, bs, SECTOR))
		return false;

	if (fat32) {
		uint8_t info[SECTOR] = {};
		put32(info, 0x41615252);
		put32(info + 484, 0x61417272);
		put32(info + 488, uint32_t(L.clusters() - 1));
		put32(info + 492, 3);
		put32(info + 508, 0xAA550000);
		if (!w.put(base + SECTOR, info, SECTOR) || !w.put(base + 6 * SECTOR, bs, SECTOR)
			|| !w.put(base + 7 * SECTOR, info, SECTOR))
			return false;
	}

	uint8_t head[12] = {};
	size_t n;
	switch (L.bits) {
	case 12: head[0] = L.media, head[1] = 0xFF, head[2] = 0xFF, n = 3; break;
	case 16: head[0] = L.media, head[1] = 0xFF, head[2] = 0xFF, head[3] = 0xFF, n = 4; break;
	default: put32(head, 0x0FFFFF00 | L.media), put32(head + 4, 0x0FFFFFFF), put32(head + 8, 0x0FFFFFFF), n = 12; break;
	}
	for (uint32_t f = 0; f < L.fats; f++)
		if (!w.put(base + (L.reserved + uint64_t(f) * L.fat_sectors) * SECTOR, head, n))
			return false;

	if (any) {
		uint8_t e[32] = {};
		memcpy(e, name, 11);
		e[11] = 0x08;
		put16(e + 22, now & 0xFFFF);
		put16(e + 24, now >> 16);
		uint64_t root = fat32 ? L.data_sector() : L.reserved + uint64_t(L.fats) * L.fat_sectors;
		if (!w.put(base + root * SECTOR, e, sizeof e))
			return false;
	}
	return true;
}

// the sectors a cluster for a volume's size, from the specification's tables; 0 where the type cannot have it
uint32_t cluster_sectors(int bits, uint64_t sectors)
{
	if (bits == 16)
		return sectors <= 8400 ? 0 : sectors <= 32680 ? 2 : sectors <= 262144 ? 4 : sectors <= 524288 ? 8
			: sectors <= 1048576 ? 16 : sectors <= 2097152 ? 32 : sectors <= 4194304 ? 64 : 0;
	return sectors <= 66600 ? 0 : sectors <= 532480 ? 1 : sectors <= 16777216 ? 8 : sectors <= 33554432 ? 16
		: sectors <= 67108864 ? 32 : 64;
}

void chs(uint8_t *p, uint64_t lba)
{
	uint64_t c = lba / (HEADS * TRACK), h = lba / TRACK % HEADS, s = lba % TRACK + 1;
	if (c > 1023)
		c = 1023, h = HEADS - 1, s = TRACK;
	p[0] = uint8_t(h);
	p[1] = uint8_t(s | (c >> 8) << 6);
	p[2] = uint8_t(c);
}

}

bool format_floppy(const syfs_io &io, const char *label, std::string &why)
{
	Writer w(io, why);
	Layout L {};
	L.bits = 12;
	L.reserved = 1;
	L.hidden = 0;
	L.drive = 0;
	L.heads = 2;
	if (io.size == 720 * 1024) {
		L.spc = 2, L.root_entries = 112, L.total = 1440, L.fat_sectors = 3, L.media = 0xF9, L.track = 9;
	} else if (io.size == 1440 * 1024) {
		L.spc = 1, L.root_entries = 224, L.total = 2880, L.fat_sectors = 9, L.media = 0xF0, L.track = 18;
	} else
		return w.fail("a DOS floppy is 737 280 bytes (720 KB) or 1 474 560 (1.44 MB)");
	return write_volume(w, 0, L, label);
}

bool format_disk(const syfs_io &io, const char *label, std::string &why)
{
	Writer w(io, why);
	if (io.size % SECTOR)
		return w.fail("a disk is whole 512-byte sectors");
	uint64_t sectors = io.size / SECTOR;
	const char *small = "a FAT disk is more than 4.3 MB (8 400 sectors and the MBR's 63)";
	if (sectors <= PARTITION_START)
		return w.fail(small);
	if (sectors - PARTITION_START > 0xFFFFFFFF)
		return w.fail("a disk with an MBR is 2 TB at most");
	Layout L {};
	L.total = sectors - PARTITION_START;
	L.bits = io.size < FAT32_FROM ? 16 : 32;
	L.spc = cluster_sectors(L.bits, L.total);
	if (!L.spc)
		return w.fail(small);
	L.reserved = L.bits == 32 ? 32 : 1;
	L.root_entries = L.bits == 32 ? 0 : 512;
	L.media = 0xF8;
	L.track = TRACK;
	L.heads = HEADS;
	L.hidden = PARTITION_START;
	L.drive = 0x80;
	// the FAT's size, as the specification computes it
	uint64_t t1 = L.total - (L.reserved + L.root_sectors()), t2 = 256ull * L.spc + L.fats;
	if (L.bits == 32)
		t2 /= 2;
	L.fat_sectors = uint32_t((t1 + t2 - 1) / t2);
	uint64_t clusters = L.clusters();
	if (L.bits == 16 ? clusters < 4085 || clusters >= 65525 : clusters < 65525)
		return w.fail("the disk's size gives a cluster count its FAT type cannot have");

	uint64_t end = sectors - 1;
	uint8_t mbr[SECTOR] = {};
	uint8_t *e = mbr + 446;
	chs(e + 1, PARTITION_START);
	e[4] = L.bits == 16 ? (L.total < 65536 ? 0x04 : 0x06) : end < uint64_t(1024) * HEADS * TRACK ? 0x0B : 0x0C;
	chs(e + 5, end);
	put32(e + 8, PARTITION_START);
	put32(e + 12, uint32_t(L.total));
	put32(mbr + 440, dos_now() ^ 0x5A5A5A5A);
	mbr[510] = 0x55, mbr[511] = 0xAA;
	return w.zero(0, uint64_t(PARTITION_START) * SECTOR) && w.put(0, mbr, SECTOR) && write_volume(w, uint64_t(PARTITION_START) * SECTOR, L, label);
}

}
