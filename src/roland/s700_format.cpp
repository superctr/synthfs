// Copyright 2026 Ian Karlsson
// Empty S-700 series media written as the machines' own format leaves them: an S-760's and an S-770's hard disk, their
// floppies (docs/formats.md, "Formatting")
#include "roland.hpp"

#include <algorithm>

namespace sfs::roland::s700 {

namespace {

constexpr uint32_t SEGMENT_SECTORS = 18;
constexpr uint32_t TABLE = 0x404, TABLE_SECTORS = 0x100;          // a halfword a segment, 65 536 of them
constexpr uint32_t DIRECTORIES = 0x504, DIRECTORY_SECTORS = 0x368;  // zero
constexpr uint32_t RECORDS = 0x86C, RECORD_SECTORS = 0xD40;         // 0xFF
constexpr uint32_t WAVES = 0x15AC;                                  // segment 2's first sector
constexpr uint32_t LAST_SECTOR = 0x1214D4;                          // the furthest last sector a disk is given
constexpr uint32_t RESERVED = 57;                                   // the S-760's two runs of reserved segments
constexpr uint32_t EXTENDED = 0x19AE, EXTENDED_SECTORS = 0x400;     // the S-760's extended partials, 0xFF

constexpr uint64_t FLOPPY_2DD = 1440 * SECTOR, FLOPPY_2HD = 2880 * SECTOR;
constexpr uint8_t FLOPPY_FILL = 0x0F;

class Writer {
public:
	Writer(const samplerfs_write_io &io, std::string &why) : io_(io), why_(why) {}
	bool put(uint64_t at, const void *p, size_t n)
	{
		if (at > io_.size || n > io_.size - at || io_.write(io_.user, at, p, n) != n)
			return fail("the image could not be written");
		return true;
	}
	bool fill(uint64_t sector, uint64_t sectors, uint8_t byte)
	{
		uint8_t run[16 * SECTOR];
		memset(run, byte, sizeof run);
		while (sectors) {
			uint64_t k = std::min<uint64_t>(sectors, 16);
			if (!put(sector * SECTOR, run, size_t(k * SECTOR)))
				return false;
			sector += k;
			sectors -= k;
		}
		return true;
	}
	bool fail(const char *what)
	{
		why_ = what;
		return false;
	}

private:
	const samplerfs_write_io &io_;
	std::string &why_;
};

void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }

// the 96 bytes every S-700 series sector 0 starts with: the ID, the kind at 0x0F, three 32-byte lines (spaces, the
// kind and system, the copyright), each ending in a zero
void header(uint8_t *s0, uint8_t kind, const char *line)
{
	memset(s0, 0xFF, SECTOR);
	memset(s0, 0, 0x60);
	memcpy(s0 + 4, "S770 MR25A", 10);
	s0[0x0F] = kind;
	memset(s0 + 0x10, ' ', 15);
	memcpy(s0 + 0x20, line, 31);
	memcpy(s0 + 0x40, "       Copyright   Roland      ", 31);
}

bool format_disk(const samplerfs_write_io &io, bool s760, std::string &why)
{
	Writer w(io, why);
	if (io.size % SECTOR)
		return w.fail("a disk is whole 512-byte sectors");
	// the disk's last sector, as far as the segment table reaches; its segments the whole ones from there down
	uint64_t last = std::min<uint64_t>(io.size / SECTOR, uint64_t(LAST_SECTOR) + 1) - 1;
	uint32_t segments = io.size / SECTOR > WAVES ? uint32_t((last - WAVES) / SEGMENT_SECTORS) : 0;
	uint32_t reserved = s760 ? 2 * RESERVED : 0;
	if (segments <= reserved)
		return w.fail(s760 ? "an S-760 disk is more than 3.9 MB (7 619 sectors)" : "an S-770 disk is more than 2.9 MB (5 567 sectors)");

	uint8_t s[2 * SECTOR];
	header(s, 0, s760 ? " S-760 System Disk    Ver. 2.24" : " SYS-772 HardDisk Sys Ver. 2.25");
	memcpy(s + 0x100, "ID0:Formatted   ", 16);
	s[0x110] = uint8_t(last), s[0x111] = uint8_t(last >> 8), s[0x112] = uint8_t(last >> 16), s[0x113] = uint8_t(last >> 24);
	memset(s + 0x114, 0, 10);
	memset(s + SECTOR, 0xFF, SECTOR);
	if (!w.put(0, s, sizeof s))
		return false;

	// the segment table: 0xFFFA, the free count, a zero for each segment the disk has and 0xFFFF past them; the
	// S-760's two runs of reserved segments and its mark in the last sector
	std::vector<uint8_t> table(size_t(TABLE_SECTORS) * SECTOR, 0xFF);
	put16(&table[0], 0xFFFA);
	put16(&table[2], segments - reserved);
	for (uint32_t n = 2; n < 2 + segments; n++)
		put16(&table[2 * n], n - 2 < reserved ? (n - 2 < RESERVED ? 0xFFFE : 0xFFFD) : 0);
	if (s760)
		put16(&table[2 * 0xFFFE], 0xFFFE);
	if (!w.put(uint64_t(TABLE) * SECTOR, table.data(), table.size()))
		return false;
	if (!w.fill(DIRECTORIES, DIRECTORY_SECTORS, 0) || !w.fill(RECORDS, RECORD_SECTORS, 0xFF))
		return false;
	return !s760 || w.fill(EXTENDED, EXTENDED_SECTORS, 0xFF);
}

bool format_floppy(const samplerfs_write_io &io, bool s760, std::string &why)
{
	Writer w(io, why);
	if (io.size != FLOPPY_2DD && io.size != FLOPPY_2HD)
		return w.fail("an S-700 series floppy is 737 280 bytes (2DD) or 1 474 560 (2HD)");
	uint8_t s0[SECTOR];
	header(s0, io.size == FLOPPY_2HD ? ':' : ';', s760 ? " S-760 Formatted Disk Ver. 2.24" : " SYS-772 Formatted    Ver. 2.25");
	return w.put(0, s0, SECTOR) && w.fill(1, io.size / SECTOR - 1, FLOPPY_FILL);
}

}

bool format_s760_disk(const samplerfs_write_io &io, std::string &why) { return format_disk(io, true, why); }
bool format_s770_disk(const samplerfs_write_io &io, std::string &why) { return format_disk(io, false, why); }
bool format_s760_floppy(const samplerfs_write_io &io, std::string &why) { return format_floppy(io, true, why); }
bool format_s770_floppy(const samplerfs_write_io &io, std::string &why) { return format_floppy(io, false, why); }

}
