# The formats

What samplerfs reads of each format.  Every number is little-endian unless it says otherwise.  **[seen]** marks
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

## ISO 9660

The primary volume descriptor at sector 16: the volume's ID at +40, the root directory's record at +156.  The tree
is walked eight deep; files ending `.wav`, `.aif`, `.aiff` or `.aifc` are listed with their sizes, and a directory
only where it holds some, at any depth.  Rock Ridge and Joliet names are not read.

## Not read yet

- S-550: the patches' and tones' other parameters; a W-30 hard disk's layout.
- AKAI: the drum, effects, cue and take files' contents; the multis' and samples' other parameters.
- E-mu discs: told apart only.
