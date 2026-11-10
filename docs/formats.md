# The formats

What samplecd reads of each format, and which machines load it.  Every number is little-endian.  **[seen]** marks
what the discs at hand show and the library relies on without a machine's reading behind it.

## Telling them apart

The XV-5080 tries a CD-ROM as an S-7xx volume, then as AKAI, then as ISO 9660.  samplecd does the same, with the
formats no machine here loads put in between:

| Format | Test | Machines |
|---|---|---|
| S-7xx | four zero bytes and `S770 MR25A` at byte 0, five list counts within their capacities | S-760, S-770, S-750, SP-700, XV-5080 |
| S-550 | `* ROLAND S-550 *` at byte 0 | S-760 and S-770 (Convert Load) |
| AKAI | a partition header at byte 0 (below) | S1000 volumes: S-760 (Convert Load) and XV-5080; S3000 volumes: XV-5080 |
| E-mu | `EMU3` (EIII, E-IV) or `EMX2` (Emax II) at byte 0 | none |
| ISO 9660 | `\1CD001` at byte 0x8000 | XV-5080 (WAV and AIFF files) |

An image is 2048-byte sectors (`.iso`, `.cdr`) or raw 2352-byte ones (`.bin`), told by the sync pattern
`00 FF×10 00` at byte 0; a raw sector's data is at +16 (mode 1) or +24 (mode 2, form 1).

## S-7xx

The S-700 series' disk layout, the same on a floppy's, a hard disk's and a CD-ROM's: 512-byte sectors from the
start of the image.

**Sector 0**: four zero bytes, `S770 MR25A` at +4, the system's line at +0x20 (32 characters, e.g. "S-760 System
Disk Ver. 2.24"), and at +0x114 five halfwords: how many slots each list has.  A count above its list's
capacity is no volume.

**The lists**: volumes, performances, patches, partials, samples.

| List | Directory | Capacity | Records | Record |
|---|---|---|---|---|
| Volume | 0x504 | 128 | 0x86C | 256 bytes |
| Performance | 0x50C | 512 | 0x8AC | 512 |
| Patch | 0x52C | 1024 | 0xAAC | 512 |
| Partial | 0x56C | 4096 | 0xEAC | 128 |
| Sample | 0x66C | 8192 | 0x12AC | 48 |

A directory is 32-byte entries, sixteen to a sector; a list is the first *count* of them, and an entry whose
first byte is not printable (32 to 126) is an empty slot.  An entry: the name (16 characters; its first three a
category code where it is one of `---`, `BEL`, `BRS`, `BS `, `BTS`, `CMB`, `CYM`, `DRM`, `FLT`, `GTR`, `HAT`, `HIT`,
`KEY`, `KIK`, `KIT`, `MLT`, `ORC`, `ORG`, `PAD`, `PNO`, `PRC`, `SAX`, `SFX`, `SNR`, `STR`, `TOM`, `VOX`, `WND`), a
flag byte at +16 (bit 6 set on every listed entry **[seen]**), and a sample's segment top at +28 and segment count
at +30.  Slots past the count may hold names left behind (one disc here has seven) and are not listed.

Record *n* of a list is at *n* times its size from the list's first record sector.  What each uses:

- a volume: its performances, 64 halfwords at +32;
- a performance: its parts' patches, 32 halfwords at +0x100;
- a patch: for each of the 88 keys whose byte at +32 + key is not 255, its partial, a halfword at +0x100 + 2 × key;
- a partial: its four layers' samples, signed halfwords at +16, +32, +48, +64 (-1 none).

A number outside its list, or naming an empty slot, uses nothing.

**A sample's record**: the start, the sustain loop's start and end, the release loop's (+16 to +32, each an
address in the upper 24 bits and 256ths in the low byte), the loop mode (+36), the rate (+44: 0 48 kHz, 1 44.1,
2 24, 3 22.05, 4 30, 5 15, 6 32, 7 16) and the original key (+45).  The wave is 16-bit linear, mono (a stereo
sound is two samples), in segments of 18 sectors (4608 frames): segment *n* (from 2) at sector 0x15AC + 18 × (*n* −
2), the next of each in the segment table from sector 0x404 (a halfword a segment; 0xFFF8 and above end it).

## AKAI

S1000 and S3000 discs share the layout; 8192-byte blocks.

**Partitions** follow one another from byte 0, 26 at most (A to Z).  A header's first halfword is the partition's
size in blocks, 4 to 0x1E00, and the 196 bytes after it are the same on every partition of every disc (the table in
`src/akai.cpp`); the walk stops at the first header that is not so.

**A partition's header**: 100 volume entries of 16 bytes from +0xCA (the name, 12 characters of AKAI's set
`0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.`, a code each; the type at +12, 0 for an empty entry, 1 an S1000
volume, 3 or 7 an S3000 one; the directory's first block at +14), then the FAT from +0x70A, a halfword a block: the
next block of its chain, 0xC000 and above the end **[seen]**.

**A volume's directory**: 24-byte entries: the name (12), the type at +16 (`p` program, `s` sample, `x` effects, `m`
multi, `d` drum, `c` cue list, `q` take list; an S3000 sets the top bit), the size at +17 (three bytes) and the file's
first block at +20.  An S1000 volume has 126 entries in its block (its own settings follow); an S3000 one has 510,
running on into the block the FAT names after the first **[seen]**: no disc here fills more than the first.

**A sample's header** at the file's start: 3 at +0, the original key at +2, the frames at +0x1A (a long), the
rate at +0x8A (a halfword, in Hz); 150 bytes on an S1000, 192 on an S3000, the 16-bit wave after it.

The S-760 converts S1000 volumes only.  Whether the XV-5080 takes every S3000 type (3 and 7) is not known yet; both
are marked as its.

## ISO 9660

The primary volume descriptor at sector 16: the volume's ID at +40, the root directory's record at +156.  The tree
is walked eight deep; files ending `.wav`, `.aif`, `.aiff` or `.aifc` are listed with their sizes, and a directory
only where it holds some, at any depth.  Rock Ridge and Joliet names are not read.

## Not read yet

- S-550 (`* ROLAND S-550 *`): its areas and samples.
- AKAI programs' keygroups: which samples a program uses.
- E-mu discs: told apart only; no machine here loads them.
