# The formats

What samplecd reads of each format.  Every number is little-endian unless it says otherwise.  **[seen]** marks
what the discs at hand show and the library relies on without a machine's reading behind it.

## Telling them apart

Tried in this order:

| Format | Test |
|---|---|
| S-760, S-770 | four zero bytes and `S770 MR25A` at byte 0, five list counts within their capacities; the system line tells which |
| S-550 | `* ROLAND S-550 *` at byte 0 |
| AKAI S1000, S3000 | a partition header at byte 0 (below); S3000 where any volume is one |
| E-mu | `EMU3` (EIII, E-IV) or `EMX2` (Emax II) at byte 0 |
| ISO 9660 | `\1CD001` at byte 0x8000 |

An image is 2048-byte sectors (`.iso`, `.cdr`) or raw 2352-byte ones (`.bin`), told by the sync pattern
`00 FF×10 00` at byte 0; a raw sector's data is at +16 (mode 1) or +24 (mode 2, form 1).

Each entry carries the format of the volume or area it is in, since an AKAI disc may hold S1000 and S3000
volumes side by side.

## S-760 and S-770

The S-700 series' disk layout, the same on a hard disk's, an MO's and a CD-ROM's: 512-byte sectors from the start
of the image.

**Sector 0**: four zero bytes, `S770 MR25A` at +4, the system line at +0x20 (32 characters), and at +0x114 five
halfwords: how many slots each list has.  A count above its list's capacity is no volume.  The system line names
the system the disc was made with, "S-760 System Disk Ver. 2.24" or "SYS-772 HardDisk Sys Ver. 2.25" (the S-770's
and S-750's): a disc whose line starts `S-760` is an S-760 disc **[seen]**.  Sectors 2 to 0x403 hold that system's
program where one was installed (zero on a disc without); nothing here reads them.

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

**Open**: on a floppy an S-760 volume carries extended performance and partial records (the sub-samples); where
an S-760 keeps them on a hard disk or CD-ROM is not known.

## S-550

The S-550's and W-30's SCSI format, as Roland's S-550 library CD-ROMs have it; 512-byte blocks, numbers
big-endian.

**Block 0**: `* ROLAND S-550 *`, then the disc's title at +0x20 (32 characters, e.g. "Roland Sound Library
L-CD1") and its catalogue number at +0x40.

**The directory** from block 1 to block 0xFF: 64-byte entries until one whose first byte is 0xFF.  An entry: the
area's name (32 characters), the floppy it was made from (+0x20, 16 characters, e.g. "RSB5501#3"), the area's
first block (+0x30, a long) and its length (+0x34, 1746 blocks), the type `A` (+0x38) and a category number
(+0x39) **[seen]**.  On L-CD1 the 165 areas follow one another from block 0x100, 0x6D4 blocks apart, and fill the
disc; the categories run 0 to 34 in the order of the instruments (0 acoustic pianos, 1 electric pianos, 2 organs,
… 23 to 28 drums and percussion, 34 effects) **[seen]**.

**An area** is an S-550 floppy's sound data: 18 blocks of parameters, then the waves.

| Offset in the area | What |
|---|---|
| 0x0000 | 16 patches, 256 bytes each |
| 0x1000 | the function and MIDI parameters (the disk's name at +0x33) |
| 0x1200 | 32 tones, 128 bytes each |
| 0x2200 | the tone list: each tone's first 16 bytes |
| 0x2400 | the waves: two banks of 18 segments, 12 288 words a segment |

A blank name (spaces) is an empty slot.

**A patch**: the name (12 characters), the key mode at +0x0F, the first key map at +0x1A and the second at +0x87
(109 bytes each, a tone number a key: 0 to 31, anything above none; 0xDF on the discs **[seen]**); the second
map is used where the key mode is 1 to 3.

**A tone**: the name (8 characters), +0x0B 0 for 30 kHz else 15 kHz, +0x0C the original key, +0x0D the wave bank,
+0x0E the first segment in it, +0x0F the segments, then the start (+0x10), the end (+0x13) and the loop point
(+0x16) as 24-bit words counted from that first segment, and the loop mode (+0x19: 0 forward, 1 alternate,
2 one shot, 3 reverse).  The tone's wave is at word bank × 18 × 12 288 + first segment × 12 288 of the area's
waves.

**The waves** are 12-bit samples left in 16-bit little-endian words, the low four bits zero **[seen]**.

## AKAI

S1000 and S3000 discs share the layout; 8192-byte blocks.

**Partitions** follow one another from byte 0, 26 at most (A to Z).  A header's first halfword is the partition's
size in blocks, 4 to 0x1E00, and the 196 bytes after it are the same on every partition of every disc (the table in
`src/akai.cpp`); the walk stops at the first header that is not so.

**A partition's header**: 100 volume entries of 16 bytes from +0xCA: the name, 12 characters of AKAI's set
`0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.` (a code each, 10 a space); the type at +12, 0 for an empty entry, 1 an
S1000 volume, 3 an S3000 one, 7 the S3000 volumes on AKAI's own S3000 and CD3000 discs **[seen]**; the directory's
first block at +14.  Then the FAT from +0x70A, a halfword a block: the next block of its chain, 0xC000 and above
the end **[seen]**.  Over the 133 AKAI discs here, the types are 1, 3 and 7 only **[seen]**.

**A volume's directory**: 24-byte entries: the name (12), the type at +16, the size at +17 (three bytes) and the
file's first block at +20.  An S1000 volume has 126 entries in its block (its own settings follow); an S3000 one has
510, running on into the block the FAT names after the first **[seen]**: no disc here fills more than the first.

**The file types** are letters: `p` program, `s` sample, and the others seen, `d`, `q`, `t`, `x`, `m`, `c`
**[seen]**.  An S3000 sets the top bit of `p`, `s` and `m` (0xF0, 0xF3, 0xED); the rest stay plain on both.  An
entry's tag is the letter without the top bit.

**A program**: a header (1 at +0, the name at +3, the keygroups' count at +42), then its keygroups, each 2 at +0;
header and keygroups are 150 bytes on an S1000, 192 on an S3000, so the file's size is (count + 1) × that **[seen]**.
A keygroup's four velocity zones name their samples at +34, +58, +82 and +106, blank for none; a name is a sample
of the same volume **[seen]**: 97 % of the programs here find all of theirs, the rest name samples of other volumes
or none (copyright notes).

**A sample's header** at the file's start: 3 at +0, the original key at +2, the frames at +0x1A (a long), the
rate at +0x8A (a halfword, in Hz); 150 bytes on an S1000, 192 on an S3000, the 16-bit wave after it.

## ISO 9660

The primary volume descriptor at sector 16: the volume's ID at +40, the root directory's record at +156.  The tree
is walked eight deep; files ending `.wav`, `.aif`, `.aiff` or `.aifc` are listed with their sizes, and a directory
only where it holds some, at any depth.  Rock Ridge and Joliet names are not read.

## Not read yet

- The S-760's extended records on a hard disk or CD-ROM.
- S-550: the patches' and tones' other parameters; how many areas a W-30 hard disk holds.
- AKAI: the multi, drum and effects files; what the volume entry's byte +13 holds (0, or 2 to 10 on a few S1000
  discs).
- E-mu discs: told apart only.
