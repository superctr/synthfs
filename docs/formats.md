# The formats

What samplerfs reads of each format.  Every number is little-endian unless it says otherwise.  **[seen]** marks
what the images at hand show and the library relies on without a machine's reading behind it.

Each image is also of a medium: a disk (a hard disk's, MO's or CD-ROM's image, which share their layouts), a
floppy, a card or a board.

## Telling them apart

Tried in this order:

| Format | Medium | Test |
|---|---|---|
| S-760, S-770 | disk | four zero bytes and `S770 MR25A` at byte 0, then a zero, byte 0x0F zero or `@`, five list counts within their capacities; the system line tells which |
| S-760, S-770, DJ-70 | floppy | four zero bytes, `S770 MR25A` and a zero at byte 0, a kind character at 0x0F, 2 MB at most; the kind line tells which |
| S-550 | disk | `* ROLAND S-550 *` at byte 0 |
| S-550, S-330, W-30, S-50 | floppy | four zero bytes and the system's ID at byte 0, 2 MB at most |
| SR-JV80 | card, board | `Roland JV80`, `O` at byte 0x0C and `S` at 0x0E, 1, 2, 4 or 8 MB |
| SRX | board | `Roland SRX` at byte 0, 32 MB |
| AKAI S1000, S3000 | disk | a partition header at byte 0 (below); S3000 where any volume is one |
| AKAI S1000, S3000 | floppy | 800 or 1600 KB, the FAT's first four halfwords (five on 2HD) 0x4000; S3000 where any file's type has its top bit set |
| AKAI S900 | floppy | whole 1024-byte blocks, 700 to 1600 KB, the FAT's first four halfwords zero, and a directory of at least one file, each with a printable name, an upper-case type and a start past block 3 |
| E-mu | disk | `EMU3` (EIII, E-IV) or `EMX2` (Emax II) at byte 0 |
| ISO 9660 | disk | `\1CD001` at byte 0x8000 |

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

**A sample's record**: the start, the sustain loop's start and end, the release loop's (+16 to +32, each a word
of the sample's own wave, counted from its first, in the upper 24 bits and 256ths in the low byte **[seen]**), the
loop mode (+36), the rate (+44: 0 48 kHz, 1 44.1, 2 24, 3 22.05, 4 30, 5 15, 6 32, 7 16) and the original key
(+45).  The loop modes, and how the library gives each:

| Mode | | Plays | Loop |
|---|---|---|---|
| 0 | Forward | start to the sustain loop's end | the sustain loop, forward |
| 1 | Fwd+Rel | start to the release loop's end | the sustain loop (the release loop left to the host) |
| 2 | One-shot | start to the sustain loop's end | none |
| 3 | Fwd+One | start to the release loop's end | the sustain loop |
| 4 | Alt | start to the sustain loop's end | the sustain loop, back and forth |
| 5 | Rev One | the sustain loop's end down to the start | none |
| 6 | Rev | the same | from the start to the sustain loop's start |
  The wave is 16-bit linear, mono (a stereo
sound is two samples), in segments of 18 sectors (4608 frames): segment *n* (from 2) at sector 0x15AC + 18 × (*n* −
2), the next of each in the segment table from sector 0x404 (a halfword a segment; 0xFFF8 and above end it).

**The S-760's extended records** **[seen]**: a disc made on an S-760 reserves segments 2 to 58 (0xFFFE in the
segment table, left zero) and 59 to 115 (0xFFFD): the latter hold the extended partials, 4096 records of 128
bytes from sector 0x19AE, record *n* the partial *n*'s four sub-sample blocks (32 bytes each, a copy of a sample
record's bytes 16 to 47) and their enable bytes at +0x7C to +0x7F.  The extended performance is the last 128 bytes
of the performance record (+0x180).  None of the discs here uses a sub-sample; the library reads neither.

### On a floppy

A volume on a floppy has the same records in another map; 512-byte sectors, 1440 of them on a 2DD disk and 2880 on
a 2HD one.

| Sectors | What |
|---|---|
| 0 | the header and the volume |
| 9-10, 11-14, 15-22, 23-38 | the names of the performances (64), patches (128), partials (256) and samples (512), 16 bytes each |
| 39-70 | performance records, 256 bytes, two to a sector |
| 71-134 | patch records, 256 bytes |
| 135-198 | partial records, 128 bytes, four to a sector |
| 199-246 | sample records, 48 bytes, ten to a sector (the last 32 bytes of each unused): 480 samples |
| 252- | the waves, segment *n* at sector 252 + 18*n* |

**Sector 0**: four zero bytes, `S770 MR25A`, a zero, and at 0x0F the disk's kind: `2` and `3` a sound disk (2HD,
2DD), `>` a DJ-70's sound disk **[seen]**, `P` and `Q` an S-760 system disk, `:` and `;` a formatted one, `<` and
`=` a Volume Dump FD disk, `0` and `1` an S-770 system disk.  Then three 32-byte lines from 0x10, the second the
kind and the system that wrote it ("S-760 Sound Disk     Ver. 2.24", "SYS-772 Sound Disk   Ver. 2.0Q", "DJ-70
Sound Disk   Ver. 1.02"), which tells the format and is the label.  At 0x100 the disk's number in a set and at
0x101 1 where the volume runs over several disks (0xFF while a save is under way: such a disk lists nothing); the
counts of volumes (0 or 1), performances, patches, partials and samples, halfwords from 0x106; the volume's segment
total at 0x110; its name at 0x180 and its performances from 0x1A0, a byte each up to 0xFF.  Only sound disks list
anything.

**The lists**: the first *count* names of each, an entry whose first byte is not printable an empty slot.  A
performance names its parts' patches in bytes at +0x10 (0xFF none), a patch its keys' partials in 88 bytes at
+0x20, a partial its samples as on a disk.  A sample record is the disk's, with the sample's place at +0x28 (the
disk of its set in the high byte, the segment in the low) and its segments at +0x2A.  Every disk of a set carries
the whole volume's lists and records; a sample's wave may lie on another disk of the set, or run over two.  An S-760
disk whose byte 0x1FF is 0xFE keeps its extended records in sectors 252 to 331 of its first disk, and its waves start
at segment 5; the library reads neither.

A volume may be saved without its upper levels: some of Roland's S-760 disks hold a patch and its partials with no
volume or performance **[seen]**.

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

The area count is the big-endian halfword at +0x139 of block 0 (165 on L-CD1; the directory is read up to it).  A
blank name (spaces) is an empty slot.

**A patch**: the name (12 characters), the key mode at +0x0F, the first key map at +0x1A and the second at +0x87
(109 bytes each, a tone number a key: 0 to 31, anything above none; 0xDF on the discs **[seen]**); the second
map is used where the key mode is 1 to 3.

**A tone**: the name (8 characters), +0x0B 0 for 30 kHz else 15 kHz, +0x0C the original key, +0x0D the wave bank,
+0x0E the first segment in it, +0x0F the segments, then the start (+0x10), the end (+0x13, the last word played)
and the loop point (+0x16) as 24-bit words counted from that first segment, and the loop mode (+0x19: 0 forward,
1 alternate, the loop running from the loop point to the end; 2 one shot; 3 reverse, one shot).  The tone's wave is at word bank × 18 × 12 288 + first segment × 12 288 of the area's
waves.

**The waves** are 12-bit samples left in 16-bit little-endian words, the low four bits zero **[seen]**.

### On a floppy

A 2DD floppy of 512-byte sectors.  Sector 0: four zero bytes and the system's ID (`S550 MT25B` and `S500 MT25B` an
S-550's, `S330 MT25C` and `S300 MT25C` an S-330's, `W-30` a W-30's, `S-50 MT25A` and `S-51 MT25A` an S-50's), two
bytes, and the system line at 0x20, the label.  The area is sectors 126 to 143 and the waves after them, as on a
disk, but packed: three bytes hold two 12-bit samples, `b0 b1 b2` giving `b0 << 4 | b1 >> 4` and `b2 << 4 | b1 &
0x0F`.  The disk's name, at +0x33 of the function sector (134), names the area; a disk whose patches and tones are
all blank (an S-330 system or utility disk) lists none.  The S-330's and W-30's parameters are read as the S-550's:
the S-330 disks here list sensibly so **[seen]**; no W-30 disk is at hand.  An S-50 disk is another layout and is
told apart only.

## AKAI

S1000 and S3000 discs share the layout; 8192-byte blocks.

**Partitions** follow one another from byte 0, 26 at most (A to Z).  A header's first halfword is the partition's
size in blocks, 4 to 0x1E00, and the 98 halfwords after it are the same on every partition of every disc,
halfword *k* being *k* × 0x0D05 (low 16 bits); the walk stops at the first header that is not so.

**A partition's header**: 100 volume entries of 16 bytes from +0xCA: the name, 12 characters of AKAI's set
`0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.` (a code each, 10 a space); the type at +12, 0 for an empty entry, 1 an
S1000 volume, 3 an S3000 one, 7 the S3000 volumes on AKAI's own S3000 and CD3000 discs **[seen]**; at +13 a number
counting the volumes across the whole disc from 1 (or 2) on seven S1000 discs, 0 on the rest, a load number by its
look **[seen]**; the directory's first block at +14.  Then the FAT from +0x70A, a halfword a block: the next block
of its chain, 0xC000 and above the end, 0x4000 and 0x8000 the header's own blocks **[seen]**.  Over the 133 AKAI
discs here, the types are 1, 3 and 7 only **[seen]**.

**A file** is read block by block along the FAT from its first block: 663 of the 350 000 files here (samples
nearly all, five programs) do not lie in order **[seen]**.

**A volume's directory**: 24-byte entries: the name (12), the type at +16, the size at +17 (three bytes) and the
file's first block at +20.  An S1000 volume has 126 entries in its block (its own settings follow); an S3000 one has
510, running on into the block the FAT names after the first **[seen]**: no disc here fills more than the first.

**The file types** are letters: `p` program, `s` sample, `m` multi (S3000 only), and the others seen, `d`, `q`,
`t`, `x`, `c` **[seen]**.  An S3000 sets the top bit of `p`, `s` and `m` (0xF0, 0xF3, 0xED); the rest stay plain on
both.  An entry's tag is the letter without the top bit.  A file of any type but `c` starts with three bytes and
its own name (+3, 12 characters): `d` files are named "DRUM INPUTS" (162 bytes), `x` "EFFECTS FILE" (7312 bytes),
`q` "QL1" and `t` "TL1" (160 bytes, cue and take lists by their names) **[seen]**; the one `c` file here holds PC
code.

**A program**: a header (1 at +0, the name at +3, the keygroups' count at +42), then its keygroups, each 2 at +0;
header and keygroups are 150 bytes on an S1000, 192 on an S3000, so the file's size is (count + 1) × that **[seen]**.
A keygroup's four velocity zones name their samples at +34, +58, +82 and +106, blank for none; a name is a sample
of the same volume **[seen]**: 97 % of the programs here find all of theirs, the rest name samples of other volumes
or none (copyright notes).

**A multi** (4096 bytes): its name at +3, then from +0x400 16 parts of 0xC0 bytes, each naming its program at +3,
blank for none.  Of the 2491 parts naming one here, 2375 name a program of the same volume; the rest name
programs of other volumes, or the default "TEST PROGRAM" **[seen]**.

**A sample's header** at the file's start: 3 at +0, the original key at +2, the name at +3, the loops in use at
+0x10 (0 to 4 here), the loop mode at +0x13, the frames, start and end at +0x1A, +0x1E and +0x22 (longs), eight
loops of 12 bytes from +0x26 and the rate at +0x8A (a halfword, in Hz); 150 bytes on an S1000, 192 on an S3000, the
16-bit wave after it: every sample here is the header and two bytes a frame **[seen]**.  A loop: the frame it
ends at (a long), a fraction (+4), its length (+6, a long) and a hold time (+10, 9999 the most seen).  Mode 2
samples hold loops that fall outside them, and the library takes it as no loop; on the others it gives the first
loop where it lies within the sample **[seen]**.

## Roland wave expansion boards and cards

The ROMs as dumped: the socket rewires the address and data lines, so the library reads each byte through the
wiring.  Within each block of 2^19 bytes (an SR-JV80's) or 2^18 (an SRX's), descrambled address bit *k* is the
dump's bit *L*[*k*], and data bit *k* is the dump's bit *D*[*k*]:

| | Lines |
|---|---|
| SR-JV80 address | 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14 |
| SRX address | 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15 |
| data, both | 2, 0, 4, 5, 7, 6, 3, 1 |

The first 32 bytes are plain in the dump: `Roland JV80 ` or `Roland SRX`, then from 0x10 the board's name ("SR-JV80-06
Dance", "Card01   Ver0.10"), the label.  Numbers are big-endian.

**SR-JV80 boards and SO-PCM1 cards** share the format.  An image of 1 MB, or of 2 MB named `SO-PCM1` or `Card`, is
a card; the rest are boards (8 MB, or 2 MB for the Experience boards).  Each 1 MB block starts with a header (the
board's ID and date at 0x20 and 0x30), whose slots *n* give a type byte at 0x50 + *n*, a count at 0x60 + 2*n* and
the address of their records at 0x80 + 4*n*:

| Slot | Type | What | Record |
|---|---|---|---|
| 0 | 2 | samples | 18 bytes (not listed) |
| 1 | 1 | waves | 60 bytes, the name first (12 characters) |
| 3 | 2 | patches, the JV-880's shape | 362 bytes, the name first |
| 3 | 5 | patches, the JV-1080's | 401 bytes, bit-packed: the name twelve 7-bit characters from bit 0 up |
| 4 | 2 | rhythm sets, the JV-880's | 2684 bytes, unnamed |
| 4 | 4 | rhythm sets, the JV-1080's | 2699 bytes, the name packed as a patch's |

A board carrying both shapes keeps one in block 0 and the other in a later block; a block repeating a slot of
another lists it once.  Each slot of patches or rhythm sets is a bank, tagged with its type **[seen]**.

**SRX boards**: the name at descrambled 0x20 (16 characters), the short name at 0x3A, the board's number at 0x40 and
the directory's count at 0x42; the directory's 16-byte entries from 0x80, each a class and a type (halfwords), the
address of its table and its length.  A table starts with a count (a halfword), the offset of its records from the
table (a long, +4) and their stride (+8).

| Class | Type | What |
|---|---|---|
| 1 | 4 | samples, 24 bytes (not listed) |
| 2 | 3 | waves: 8-byte records, the offset of the wave's definition from the table and its length; the definition starts with the name (12 characters) |
| 4 | 6, 7, 8, 9 | patch banks for four host families (574 bytes a patch; 646 on type 9) |
| 5 | 5, 6, 7 | rhythm set banks likewise |

A patch's or rhythm set's name is twelve 7-bit characters packed from the first byte's top bit down.  A board holds
the same sounds in each of its banks, one per host family; each bank is listed and tagged `class:type`, and the
host picks the one its machine reads **[seen]**.  Roland's later plugin rebuilds of the boards carry waves only
**[seen]**.

### On a floppy

An S1000 or S3000 floppy is one volume in 1024-byte blocks (800 on 2DD, 1600 on 2HD): its directory's 64 entries
from byte 0 (a volume directory's 24-byte entries), the FAT from 0x600, a halfword a block as on a disk (0x4000 the
system's blocks, 0xC000 and above the end), and the volume's name (12 characters, AKAI's set) straight after the FAT.
Files are a disk's: the same programs, samples and others.  The system's blocks are the first four on 2DD and the first
five on 2HD; an operating system disk may mark every block so (a CD3000's) **[seen]**.  Besides the disk's types,
floppies hold `a` and `b` files (on the S20's) and type 0xFF (an operating system), tagged in hex **[seen]**.

## AKAI S900

The S900's and S950's floppy, 2DD (800 KB) or 2HD (1600 KB), in 1024-byte blocks **[seen]**.  The directory: 64
entries of 24 bytes from byte 0, each a name (10 ASCII characters, spaces after), its type at +16 (`S` sample, `P`
program, and `O`, `D`, `F`, listed as others), its size at +17 (three bytes) and its first block at +20.  The FAT from
0x600, a halfword a block: the next block, 0x8000 the end; the first four blocks (directory and FAT) stay zero.

**A sample** (`S`): a 60-byte header, then the wave, 12-bit samples packed three bytes to two (the file is 60 + 1.5
bytes a frame on all 3472 samples here).  The name (10), the frames at +0x10 (a long), the rate in Hz at +0x14, the
pitch at +0x16 in sixteenths of a semitone (C1 576, AKAI numbering the octaves from C1 = 36), the loop mode at +0x1A
(`O` one shot, `L` loop, `A` alternating), the end at +0x1C, the start at +0x20 and the loop's length at +0x24 (longs),
the loop running up to the end **[seen]**: the S20 conversions of the same disks keep the frames and starts.

**A program** (`P`): a 0x26-byte header with the keygroups' count at +0x17, then the keygroups, 70 bytes each: the
highest key at +0, the lowest at +1, and two sample names at +0x18 and +0x2E (the second for loud notes; often an
unused default such as "SAMPLE 2") **[seen]**: the first name is a sample of the disk in 96 % of the keygroups here.

## ISO 9660

The primary volume descriptor at sector 16: the volume's ID at +40, the root directory's record at +156.  The tree
is walked eight deep; files ending `.wav`, `.aif`, `.aiff` or `.aifc` are listed with their sizes, and a directory
only where it holds some, at any depth.  Rock Ridge and Joliet names are not read.

## Not read yet

- S-550: the patches' and tones' other parameters; a W-30 hard disk's layout; a W-30 floppy (none at hand).
- S-50 floppies: told apart only.
- S-760, S-770 floppies: the Volume Dump FD disk's slots; a set's waves across its disks.
- Boards and cards: the samples' and waves' parameters and the patches' other fields; the SO-JD80 cards (JD-800,
  JD-990), another format.
- AKAI S900: the `O`, `D` and `F` files; the programs' other parameters.
- AKAI: the drum, effects, cue and take files' contents; the multis' and samples' other parameters.
- E-mu discs: told apart only.
