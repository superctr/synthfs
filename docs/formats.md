# The formats

What synthfs reads of each format.  Every number is little-endian unless it says otherwise.  **[seen]** marks
what the images at hand show and the library relies on without a machine's reading behind it.

Each image is also of a medium: a disk (a hard disk's, MO's or CD-ROM's image, which share their layouts), a
floppy, a card or a board; a file of exclusive messages is a file, a machine's battery memory a memory.

## Telling them apart

Tried in this order:

| Format | Medium | Test |
|---|---|---|
| S-760, S-770 | disk | four zero bytes and `S770 MR25A` at byte 0, then a zero, byte 0x0F zero, a space or `@`, five list counts within their capacities; the system line tells which |
| S-760, S-770, DJ-70 | floppy | four zero bytes, `S770 MR25A` and a zero at byte 0, a kind character at 0x0F, 2 MB at most; the kind line tells which |
| S-550 | disk | `* ROLAND S-550 *` at byte 0 |
| S-550, S-330, W-30, S-50 | floppy | four zero bytes and the system's ID at byte 0, 2 MB at most |
| SR-JV80 | card, board | `Roland JV80`, `O` at byte 0x0C and `S` at 0x0E, 1, 2, 4 or 8 MB |
| SRX | board | `Roland SRX` at byte 0, 32 MB |
| JV-80, JV-1080 | card | a DATA card's key at byte 0 (below), 32 or 64 KB |
| AKAI S1000, S3000 | disk | a partition header at byte 0 (below); S3000 where any volume is one |
| AKAI S1000, S3000 | floppy | 800 or 1600 KB, the FAT's first four halfwords (five on 2HD) 0x4000; S3000 where any file's type has its top bit set |
| AKAI S900 | floppy | whole 1024-byte blocks, 700 to 1600 KB, the FAT's first four halfwords zero, and a directory of at least one file, each with a printable name, an upper-case type and a start past block 3 |
| E-mu | disk | `EMU3` (EIII, E-IV) or `EMX2` (Emax II) at byte 0 |
| ISO 9660 | disk | `\1CD001` at byte 0x8000 |
| FAT | floppy, disk | a FAT boot sector at byte 0, or an MBR whose first FAT partition starts with one (below) |
| System exclusive | file | `F0 41`, a device, the model ID of a model with a map and `12` at byte 0, 16 MB at most |
| XV-5080, XV-3080, Fantom | file | `SVD1` at byte 2 and at byte 20 `XV88` (an XV's; a patch's record size tells which) or `NEXY` (a Fantom's), 16 MB at most (below, "XV-5080, XV-3080 and Fantom files") |

A cue sheet is told first, by its text (below, "Cue sheets").  An image is 2048-byte sectors (`.iso`, `.cdr`) or raw 2352-byte ones (`.bin`), told by the sync pattern
`00 FF×10 00` at byte 0; a raw sector's data is at +16 (mode 1) or +24 (mode 2, form 1).

An image in a VHD is read through its sectors (below, "VHD") before anything else is told, so whatever follows
holds for what a VHD contains as for a raw image.

A SmartMedia card's image is read through its logical sectors (below, "SmartMedia cards"), and what they hold is
told as any disk's is; its medium is `smartmedia` whatever the format, `none` for a card not formatted.

Each entry carries the format of the volume or area it is in, since an AKAI disc may hold S1000 and S3000
volumes side by side.

## S-760 and S-770

The S-700 series' disk layout, the same on a hard disk's, an MO's and a CD-ROM's: 512-byte sectors from the start
of the image.

**Sector 0**: four zero bytes, `S770 MR25A` at +4, at +0x0F a zero, or a space on some of Roland's L-CDP discs
(SYS-772 2.19a, 2.21, 32k-4 among them) **[seen]**, the system line at +0x20 (32 characters), the disk's name at
+0x100 (16 characters: `ID`, the SCSI ID it was formatted at, a colon, then twelve of the name itself, which is
what a machine's drive list shows; `ID0:Formatted` as formatted, or with the name `syfs_create` is given), its last
sector at +0x110 (a long), and at +0x114 five halfwords: how many slots each list
has.  A count above its list's capacity is no volume.  The system line names
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
`KEY`, `KIK`, `KIT`, `MLT`, `ORC`, `ORG`, `PAD`, `PNO`, `PRC`, `SAX`, `SFX`, `SNR`, `STR`, `TOM`, `VOX`, `WND`), then,
on every disc here **[seen]**:

| At | |
|---|---|
| +16 | the list's type: 0x40 a volume, 0x41 a performance, 0x42 a patch, 0x43 a partial, 0x44 a sample; +17 zero |
| +18 | the entry after it in the list's order, its number with 0x8000 set; 0xFFFF on the last |
| +20 | the entry before it, likewise; 0xFFFF on the first |
| +22 | its place in that order, from 0 |
| +24 | four bytes, zero but on some performances (the last 1) and one volume |
| +28 | a sample's first segment; zero on the others |
| +30 | the segments it sounds: a sample's own count, and for the others the sum over the samples they reach, each once |

Each list is one chain: slot 0 is the first (nothing before it), and following +18 reaches every entry of the
list once, each naming the one before it at +20.  The order is mostly the slots' own, and mostly +22 counts along
it; on two of the five discs a list's places have fallen behind its order, and thirteen of some seventeen thousand
sizes are not their samples' sum, so a machine leaves both as they were when entries are changed.  No disc has an
empty slot below its list's count.  Slots past the count may hold names left behind (one disc here has seven) and
are not listed.

Record *n* of a list is at *n* times its size from the list's first record sector.  What each uses:

- a volume: its performances, 64 halfwords at +32;
- a performance: its parts' patches, 32 halfwords at +0x100;
- a patch: for each of the 88 keys (21 to 108, *k* from 0) whose byte at +32 + *k* is not 255, its partial, a
  halfword at +0x100 + 2*k*;
- a partial: its four layers' samples, signed halfwords at +16, +32, +48, +64 (-1 none).

A patch's sample at a key is its key's partial's; a partial's is its first layer naming a sample.  A layer's other
fields follow its sample number (pitch key follow +2, level +3, pan +4, coarse tune +5, fine tune +6, its velocity
range's lower +7 and upper +9 with their fade widths +8 and +10), and +47 switches the layers by velocity; none of
them is read, so a host wanting velocity layers reads the record itself.

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

**The wave**: 16-bit little-endian two's complement, mono (a stereo sound is two samples), in segments of 18 sectors
(4608 frames): segment *n* (from 2) at sector 0x15AC + 18 × (*n* − 2), the first the sample's segment top (+28 of its
directory entry) and the next of each in the segment table from sector 0x404 (a halfword a segment; 0xFFF8 and above
end it).  The table's entry 0 is 0xFFFA and entry 1 the count of free segments; a free segment is 0, one past the
disk's end 0xFFFF.  A sample's frames are its segments' whole, the last padded past the wave's end.  On the library's
discs the waves run smoothly across their segments' joins and an instrument disc's sustain loops meet without a step
**[seen]**.

**Taken in from another S-700 disk** (`syfs_transfer`, a hard disk's image taking an entry of a CD-ROM's or another
disk's): the entry and, list by list down to the samples, what it and they name.  From the samples up, each is
either one already on the disk, with the same sixteen bytes of name and the same record (and, a sample, the same
segments of wave), or made in the slot after its list's last, its count one more.  A record made is the other
disk's with the numbers it names set to theirs here; a number that named no listed entry there is set to 0xFFFF,
so that it names none here.  A patch's bytes at +32 are left as they are: they are not the partials' numbers (two
discs here have them otherwise) and only tell a key in use.  A sample's wave goes into the lowest free segments,
each naming the next in the table and the last 0xFFF8, and the table's free count goes down by them.  A directory
entry made is the other's with its type, 0xFFFF after it, the list's last before it (which then names it), its
place the count of entries before, a sample's first segment and the segments it sounds; so each list stays one
chain in the order its entries were made.  On an S-760's disk a partial's extended record is the other disk's
where that is an S-760's too, else 0xFF throughout, as formatting leaves it.  Waves and records are written
first, then the directory's entries, then the table and the counts.

Checked: the lists' chains, places and sizes and the table's account on disks so made, by a reading of them apart
from the library's and the same reading of the five discs here; the waves and what each entry uses against the
disc they came from.  An S-760 (system 2.24) lists such a disk's performances with their lengths, loads them
without complaint, shows the performance's patch in its part, and sounds it.

Not written: a floppy's volume into a disk (its records are of another shape), entries removed or named anew, a
new sample from a WAV file.

**The S-760's extended records**: a disc formatted on an S-760 reserves segments 2 to 58 (0xFFFE in the segment
table, left as they were) and 59 to 115 (0xFFFD), and marks the table's entry 0xFFFE with 0xFFFE (0xFFFF on an
S-770's disc; an S-760 refuses a disc with anything else there): the reserved segments hold the extended partials, 4096 records of 128
bytes from sector 0x19AE, record *n* the partial *n*'s four sub-sample blocks (32 bytes each, a copy of a sample
record's bytes 16 to 47) and their enable bytes at +0x7C to +0x7F.  The extended performance is the last 128 bytes
of the performance record (+0x180).  None of the discs here uses a sub-sample; the library reads neither.

**As Data Sets** (`syfs_read` as `syx`): a performance, patch, partial or sample is its record's first bytes, as
many as its block of the map (`maps.md`: 256, 256, 128, 48), a nibble a byte of the message with the high one
first, at the address of the block's place for the entry's number.  Memory has fewer places than a disk's lists
(64, 128, 256 and 512), so the number is taken within them: entry 130 of the patches goes to place 2.  An S-760's
disk is read by the S-760's map and adds the extended records, each at the same place of its own block after the
record's: the performance's last 128 bytes, and the partial's of the reserved segments.  An S-770's disk is read by
the S-770's map.  The record goes as it is: on a hard disk or a CD-ROM the patches a performance's first half
names, and a patch's bytes at +32, are the numbers memory had when it was saved, not the disk's (which are in the
second half), so one entry's messages do not carry what it uses.

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
performance names its parts' patches in bytes at +0x10 (0xFF none), a patch its keys' partials in 88 bytes at +0x20, a
partial its samples as on a disk.  A sample record is the disk's, with the sample's place at +0x28 (the disk of its
set in the high byte, the segment in the low) and its segments at +0x2A.  The wave is a disk's, 16-bit little-endian,
its segments in order from that one.  Every disk of a set carries the whole volume's lists and records; a sample's
wave may lie on another disk of the set, or run off a disk's last segment (65 on 2DD, 145 on 2HD) onto segment 0 of
the next.  The disk's own number is at 0x100 of sector 0; the library reads the part of a wave a disk holds, and the
samples running over two disks here join seamlessly when the two parts are put together **[seen]**.  An S-760 disk
whose byte 0x1FF is 0xFE keeps its extended records in sectors 252 to 331 of its first disk, and its waves start at
segment 5; the library reads neither.

As Data Sets a floppy's performance, patch, partial and sample are their records as on a disk, by the format's
map; the numbers they name are the floppy's own, which are memory's.  The extended records are not read, and a
DJ-70's floppy has no map.

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
map is used where the key mode is 1 to 3.  A map's first byte is key 21: so read, the drum kits' one-key tones play
at their own original keys and the median of every mapped key's tone's original key less the key is zero on L-CD1
**[seen]**.  A patch's sample at a key is the tone its first map gives.

**A tone**: the name (8 characters), +0x0B 0 for 30 kHz else 15 kHz, +0x0C the original key, +0x0D the wave bank,
+0x0E the first segment in it, +0x0F the segments, then the start (+0x10), the end (+0x13, the last word played)
and the loop point (+0x16) as 24-bit words counted from that first segment, and the loop mode (+0x19: 0 forward,
1 alternate, the loop running from the loop point to the end; 2 one shot; 3 reverse, one shot).  The tone's wave is at word bank × 18 × 12 288 + first segment × 12 288 of the area's
waves.

**The waves** are 12-bit two's complement samples left in 16-bit little-endian words, the low four bits zero
**[seen]**, read as they stand.  A tone's frames run from its first segment for its count of segments.

### On a floppy

A 2DD floppy of 512-byte sectors.  Sector 0: four zero bytes and the system's ID (`S550 MT25B` and `S500 MT25B` an
S-550's, `S330 MT25C` and `S300 MT25C` an S-330's, `W-30` a W-30's, `S-50 MT25A` and `S-51 MT25A` an S-50's), two
bytes, and the system line at 0x20, the label.  The area is sectors 126 to 143 and the waves after them, as on a disk,
but packed: three bytes hold two 12-bit samples, `b0 b1 b2` giving `b0 << 4 | b1 >> 4` and `b2 << 4 | b1 & 0x0F`, read
as 16 bits with the low four zero: the S-550 library's floppies decode to the same words as the same tones on L-CD1,
1302 waves alike, and the S-330's system disks share theirs with it too **[seen]**.  The disk's name, at +0x33 of the
function sector (134), names the area; a disk whose patches and tones are all blank (an S-330 system or utility disk)
lists none.  The S-330's and W-30's parameters are read as the S-550's: the S-330 disks here list sensibly so
**[seen]**; no W-30 disk is at hand.  An S-50 disk is another layout and is told apart only.

## System exclusive files

A file of Roland's exclusive messages (`.syx`): `F0 41`, the device, the model ID, the command, and for a Data Set
(`DT1`, command `12`) the address, the data and a sum that brings the address's and data's bytes to a multiple of
128, then `F7`.  The first message tells the model, by the maps' model IDs (`maps.md`); the S-700 series share ID
`34` and are read by the S-760's map, which has every block of the others'; the JV-2080 shares the JV-1080's `6A`,
and what it adds lies after the JV-1080's blocks' ends, where it is passed over; the XV-5080 and XV-3080 share
`00 10`, and a file is read by the XV-5080's map where a message of it is at an address the XV-3080's map lacks
(a part past the sixteenth, the system's equalizer), else by the XV-3080's, whose system and performances are whole
with sixteen parts.  The Fantom has that ID as well: a file is read by its map where more of the file's data is
at addresses of its map than of the XV-5080's (a multitimbre, its system, a patch's controller), so a Fantom's
patch sent without its controller reads as an XV's.  Data Sets of that model with a right sum
are put where the map has their addresses, in whatever pieces and order they come; other messages, other models'
and bytes between messages are passed over.

Each area of the map that the file fills whole is an entry: an area is one block at a place (an S-700's patch) or
a group's blocks (a JV-1080's patch: its common and four tones; a rhythm set: its common and 64 keys).  Its type is
the area's (a volume, performance, patch, partial or sample; a JV-1080's or an XV's system, performance, patch or
rhythm set), its name its first block's name parameters' (a system's the area's own), its format the model's
records' (`S-760`, `JV-1080`, `XV-5080`, `XV-3080`, `Fantom`).  An area filled in part is none, but that where the area has a
packed record a block that is not in it may be missing: an XV-5080's patch's split keys, which the machine does not
send.  An S-700's entries are numbered by their places in memory.  A
JV-1080's and an XV's are under banks, one for each kind of area the file has anything of, named as the map names them
("User Patch", "Card Rhythm", "Temporary Patch"), and numbered by their place in the bank: a temporary patch's the
performance's parts in the map's order and then the patch mode's.  An S-700 performance's name may have a number with its top bit set
where the colon is (its fourth character, as the records on some floppies do **[seen]**); it reads as the colon.
The extended performance and the extended partial belong to the entry of the same number and are no entries of
their own.  What an entry uses is what its record names among the file's entries: a performance's patches (bytes
at +0x10, 0xFF none), a patch's partials (88 bytes at +0x20), a partial's samples (halfwords at +16, +32, +48, +64).

An entry reads as `syx`: its blocks, and its extended block where the file had it, as Data Sets to the device the
file's had, each within a row of 128 addresses (the address's last byte), so a block of 512 units is four messages.
The library writes its own Data Sets the same way, to device 0.

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
A keygroup's keys run from its lowest at +3 to its highest at +4, and its four velocity zones name their samples at
+34, +58, +82 and +106, blank for none; a name is a sample of the same volume **[seen]**: 97 % of the programs here find all of theirs, the rest name samples of other volumes
or none (copyright notes).  A program's sample at a key is the first velocity zone's found, of the first keygroup
holding the key; the zones' velocity ranges and the keygroups' tuning are not read.

**A multi** (4096 bytes): its name at +3, then from +0x400 16 parts of 0xC0 bytes, each naming its program at +3,
blank for none.  Of the 2491 parts naming one here, 2375 name a program of the same volume; the rest name
programs of other volumes, or the default "TEST PROGRAM" **[seen]**.

**A sample's header** at the file's start: 3 at +0, the original key at +2, the name at +3, the loops in use at +0x10
(0 to 4 here), the loop mode at +0x13, the frames, start and end at +0x1A, +0x1E and +0x22 (longs), eight loops of 12
bytes from +0x26 and the rate at +0x8A (a halfword, in Hz); 150 bytes on an S1000, 192 on an S3000, the 16-bit
little-endian two's complement wave after it: every sample here is the header and two bytes a frame **[seen]**.  A
loop: the frame it ends at (a long), a fraction (+4), its length (+6, a long) and a hold time (+10, 9999 the most
seen).  Mode 2 samples hold loops that fall outside them, and the library takes it as no loop; on the others it gives
the first loop where it lies within the sample **[seen]**.

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

**SR-JV80 boards and SO-PCM1 cards** share the format.  An image of 1 MB, or of 2 MB named `SO-PCM1` or `Card`, is a
card; the rest are boards (8 MB, or 2 MB for the Experience boards).  Each 1 MB block starts with a header (the
board's ID and date at 0x20 and 0x30), whose slots *n* give a type byte at 0x50 + *n*, a count at 0x60 + 2*n* and the
address of their records at 0x80 + 4*n*:

| Slot | Type | What | Record | Bank |
|---|---|---|---|---|
| 0 | 2 | samples | 18 bytes (below) | |
| 1 | 1 | waves | 60 bytes: the name (12 characters), sixteen top keys, sixteen sample numbers (halfwords) | |
| 3 | 2 | patches, the JV-880's shape (the JV-80's format, the JV-880 its rack version) | 362 bytes, the name first | JV-880 Patches |
| 3 | 3 | patches, the JD-990's | 379 bytes, a byte and then the name, 16 characters **[seen]** | JD-990 Patches |
| 3 | 5 | patches, the JV-1080's | 401 bytes, bit-packed: the name twelve 7-bit characters from bit 0 up | JV-1080 Patches |
| 4 | 2 | rhythm sets, the JV-880's | 2684 bytes, unnamed: keys 36 to 96, 44 bytes each | JV-880 Rhythm Sets |
| 4 | 4 | rhythm sets, the JV-1080's | 2699 bytes, the name packed as a patch's: 11 bytes, then keys 35 to 98, 42 bytes each | JV-1080 Rhythm Sets |

A board carrying both shapes keeps one in block 0 and the other in a later block; a block repeating a slot of
another lists it once.  Each slot of patches or rhythm sets is a bank, named as the table says and tagged with its
type.  A wave's sample numbers count in the samples of its own block's header.

Which machine each shape is for: with SR-JV80-06 fitted, whose two patch banks differ in order (the first patch
`Dance Again` in type 2, `All night!` in type 5), the JV-880 loads `Dance Again` from the board, and the JV-1080, the
XV-3080 and the XV-5080 offer `All night!` as the board's first; the JV-1080 names the board's rhythm sets from type
4.  The JV-1080 and the XVs take type 2 where a board has no type 5 (SR-JV80-01's `770 Grand 1`), converting it.  The
JV-1080's own preset patches and rhythm sets are records of 401 and 2699 bytes in the same layouts.  Type 3 is on
SR-JV80-04 alone, whose manual gives it 255 patches each for the JV series and for the JD-990; the JD-990 loads
them one at a time into its temporary patch, and takes the JV series' with a conversion.  The cards (SO-PCM1)
carry type 2 only, and SR-JV80-01 to -03, -05 and -99 likewise; SR-JV80-97 and -98 carry an empty type 2 slot,
which is not listed **[seen]**.

**The JV-1080's records** (types 5 and 4) are packed: a patch its common in 49 bytes and four tones in 88 each, a
rhythm set its common in 11 and keys 35 to 98 in 42 each.  Within a block every parameter of the JV-1080's address
map (`maps.md`) follows the one before in the order of their addresses, each in the bits the map gives it (seven a
character of the name, six the EFX type, one a switch), from bit 0 of the block's first byte up; a parameter sent
as two nibbles has the low one first.  The bits left in a block are no parameter of the JV-1080's (a patch's
common has two fields more, a bit and seven bits, as a JV-2080 keeps them).  Read as `syx` a patch or rhythm set
is its parameters as Data Sets to device 0x10, at the user patch (128 of them) or user rhythm set (two) its number
has, taken within those; a tone's 129 bytes are two messages.  Checked: every record of the sixteen
boards here with these shapes (3693 of them) has every value within its parameter's range, and a JV-1080 with the
board fitted, the sound selected and its temporary patch or rhythm set asked back, answers the same bytes for the
five patches and three rhythm sets tried, of five boards.  The JV-880's and
JD-990's shapes are not read as Data Sets.

An SR-JV80 sample:

| Offset | Size | What |
|---|---|---|
| 0 | 1 | a level, 127 on most (not read) |
| 1 | 3 | the start, a byte address on the board |
| 4 | 3 | the loop's start |
| 7 | 3 | the end, the last sample played |
| 10 | 2 | zero but on SR-JV80-01's back and forth loops, where it is the loop's offset from the start less one (not read) |
| 12 | 1 | the flags (below) |
| 13 | 1 | the original key |
| 14 | 2 | the fine tune, thousandths of a semitone from 1024 |
| 16 | 2 | near 1024 (not read) |

The rate is 32 kHz for every SR-JV80 sample: the single-cycle loops of SR-JV80-04, -06, -08, -11 and SO-PCM1-05
are one period of the original key at 32 kHz, less the fine tune **[seen]**.

**SRX boards**: the name at descrambled 0x20 (16 characters), the short name at 0x3A, the board's number at 0x40 and
the directory's count at 0x42; the directory's 16-byte entries from 0x80, each a class and a type (halfwords), the
address of its table and its length.  A table starts with a count (a halfword), the offset of its records from the
table (a long, +4) and their stride (+8); a bank's table has the bank select it is chosen by at +12 (MSB, LSB:
`bank-select`).

| Class | Type | What | Bank |
|---|---|---|---|
| 1 | 4 | samples, 24 bytes (below) | |
| 2 | 3 | waves: 8-byte records, the offset of the wave's definition from the table and its length | |
| 4 | 6 | patches, 574 bytes each | XV-3080 Patches |
| 4 | 7 | patches, 574 bytes | XV-5080 Patches |
| 4 | 8 | patches, 574 bytes | Global Patches |
| 4 | 9 | patches, 646 bytes | Fantom-S Patches |
| 5 | 5 | rhythm sets, 512 bytes each | XV-3080 Rhythm Sets |
| 5 | 6 | rhythm sets | XV-5080 Rhythm Sets |
| 5 | 7 | rhythm sets | Global Rhythm Sets |
| 5 | 8 | rhythm sets (on no board at hand) | Fantom-S Rhythm Sets |

A patch's or rhythm set's name is twelve 7-bit characters packed from the first byte's top bit down; a patch's
category is the next seven bits (`category`).  A board holds
the same sounds in each of its banks of a class, one bank for each family of machines, mostly under the same names
but with some parameters of their own; each bank is listed, named as the table says and tagged `class:type`, and a
class and type of no family above is listed unnamed.  The names are the ones the Fantom-X's wave information page
gives the banks: `XV-3080`, `XV-5080`, `Global` and `FantomS`.  With each bank's names marked apart, the XV-3080
offers the board's patches and rhythm sets from 4:6 and 5:5, the XV-5080 from 4:7 and 5:6, or from 4:6 where a
board has no 4:7 (SRX-02's `PremierGrand`); neither reads 4:8, 4:9 or 5:7.  The Global patches are read by the
RD-700 and the Fantom-X, the Global rhythm sets and the Fantom-S banks by the Fantom-X.  SRX-01 and SRX-02 carry
4:6 and 5:5 only (SRX-02 no rhythm sets), SRX-03 lacks 5:6, SRX-04 and SRX-10 to -12 carry no rhythm sets, and only
SRX-10 to -12 carry 4:9 **[seen]**.  Roland's later plugin rebuilds of the boards carry waves only **[seen]**.

**The XVs' records** (4:6 and 5:5 the XV-3080's, 4:7 and 5:6 the XV-5080's) are packed by the machine's address
map (`maps.md`): within a block every parameter follows the one before in the order of their addresses, each in
the bits the map gives it times its units, from the top bit of the block's first byte down, and each block starts
on a byte.  A patch's 574 bytes are its common (56 bytes), MFX (78), chorus (26), reverb (42), tone mix table (32)
and four tones (85 each).  A rhythm set's 512 are its common (14), MFX, chorus and reverb, 160 bytes in all, and
then 88 longs, one for each key from 21: the offset from the bank's table of that key's tone, 121 bytes, which
sets share.  A value sent about 64 in fewer bits than hold it is kept about the middle of its bits (an octave shift
of 61 to 67 in three bits as 1 to 7).  Read as `syx` a patch or rhythm set is its parameters as Data Sets to device
0x10, at the user patch (128) or user rhythm set (four) its number has, taken within those.  Checked: an XV-5080
and an XV-3080 with SRX-05 fitted, the sound selected and the temporary patch or rhythm set asked back, answer the
same bytes for the patches and rhythm sets tried of their own banks.  The Global and Fantom-S banks are not read
as Data Sets.

A wave's definition: the name (12 characters), four bytes (the first 1 on a few drum waves, which play the split
of key 60 when a tone asks for the wave's own pitch; not read), then four bytes a split to the definition's end: the
top key, a zero byte and the sample's number (a halfword); up to 72 splits **[seen]**.  An SRX sample, big-endian:

| Offset | Size | What |
|---|---|---|
| 0 | 1 | a level (not read) |
| 1 | 1 | the flags (below) |
| 2 | 1 | the original key |
| 3 | 1 | zero on most boards, 52 to 190 on SRX-12's (not read) |
| 4 | 2 | 0x0200 (not read) |
| 6 | 2 | the fine tune, thousandths of a semitone from 1024 |
| 8 | 2 | a gain, 1024 for none (not read) |
| 10 | 2 | the rate in hundreds of Hz (320, 441, 480; 300 on a few) |
| 12 | 4 | the start, a byte address on the board |
| 16 | 4 | the loop's start |
| 20 | 4 | the end, the last sample played |

**A wave and its splits**, both families: the splits run in order of their top keys, each from the key after the
one before's top (the first from 0); a key plays the first split whose top is at or above it, else the last.  On
an SR-JV80 the list ends at the first top of 127, the numbers after it 0xFFFF.  A sample number of 0xFFFF is a split
that sounds nothing (an SRX drum wave's keys between its drums) **[seen]**.  Each split that sounds is listed under
its wave as a sample, numbered by its place among the splits, named as the wave and tagged with its keys
(`60-64`); a sample several waves use is listed under each.  A wave reads as the split it plays at key 60, or where
that sounds nothing the nearest that does, and uses its splits' samples.

The original keys are the pitches the samples were recorded at, checked against their spectra across the boards
(the median distance under a twentieth of a semitone on most melodic boards, under a fifth on all) **[seen]**.  A board places its
samples above the keys they serve: a piano's split for keys 59 to 61 holds the sample of key 64, a drum menu's key
60 a sample of key 66, so the machine plays them lowered.  The basses of SR-JV80-06, -10 and -19 carry an original
key an octave above their pitch, and sound an octave below the key played, as on the machine **[seen]**.  The fine
tune is what the machine adds to the original key's pitch; synthfs gives it in the rate, rate × 2^((fine −
1024) / 12000), so that the rate at the original key plays as the machine does.

The flags: bit 0 a back and forth loop, bit 1 played once (its loop's start then equals its end on nearly every
sample), bit 2 played from the end down (the `REV` waves).  A sample loops where neither bit 1 is set nor its loop's
start is its end; its loop runs from the loop's start to the end inclusive, the mode the flags byte.  A few samples'
loops start before their start (two on SR-JV80-17 and on SRX-09): such a sample is given as it plays, the frames from
the start to the end, then the loop's from its start to the end again, the loop over those **[seen]**.

**The sample encoding**: one byte a sample, a signed delta, scaled by a power of two and summed.  The first 32 KB of
each 1 MB block (each region of an SRX board alike) are the shifts, a nibble for each 16 samples of the block: the
byte at the sample's offset in the block divided by 32, its low nibble where bit 4 of the offset is clear, its high
one where set; sample data starts at 0x8000 of a block, and a sample stays within one.  A sample's value is the sum
of its deltas, each times 2^shift, from its start up to it inclusive, the sum starting from zero.  Shifts above 10
are not used by any board (the table's header bytes aside) and count as zero.  The sums stay within 18 bits (the
largest ±0x11E30, on SRX-05) and are read as their top 16 bits, the sum shifted right by two; an 18-bit sum is taken
as the tone generators keep it, wrapping.  The deltas over a loop, from its start to the end inclusive, sum to zero
on every sample but four (two on SR-JV80-12, two on SRX-08), so the sum comes back to where it was each time round
and a loop neither steps nor drifts **[seen]**.

A sample played backward, a back and forth loop's way back or a reversed sample, is given as its frames forward:
the host plays it backward.  What is not read: the levels, gains and the fields marked so above, a sample's second
fine tune where it has one, and the tones' own pitch and key settings in the patches.

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

**A sample** (`S`): a 60-byte header, then the wave, 12-bit two's complement samples in 1.5 bytes a frame (the file is
60 + 1.5 bytes a frame on all 3472 samples here) in two parts: the first half's samples as 16-bit little-endian words,
each its sample's top eight bits in the high byte, its low four bits in the low byte's high nibble and the low four
bits of its partner half the frames on in the low nibble; then the second half's top eight bits, a byte each.  Every
sample here has an even count of frames **[seen]**; an odd one is read with the first half the larger.  The S900
samples AKAI's CD-ROMs carry over keep these same values (TABLA 8 alike in every frame's top twelve bits) **[seen]**.
The name (10), the frames at +0x10 (a long), the rate in Hz at +0x14, the pitch at +0x16 in sixteenths of a semitone
(C1 576, AKAI numbering the octaves from C1 = 36), the loop mode at +0x1A (`O` one shot, `L` loop, `A` alternating),
the end at +0x1C, the start at +0x20 and the loop's length at +0x24 (longs), the loop running up to the end
**[seen]**: the S20 conversions of the same disks keep the frames and starts.

**A program** (`P`): a 0x26-byte header with the keygroups' count at +0x17, then the keygroups, 70 bytes each: the
highest key at +0, the lowest at +1, and two sample names at +0x18 and +0x2E (the second for loud notes; often an
unused default such as "SAMPLE 2") **[seen]**: the first name is a sample of the disk in 96 % of the keygroups here.
A program's sample at a key is the first found of the first keygroup holding it.

## ISO 9660

The primary volume descriptor at sector 16: the volume's ID at +40, the root directory's record at +156.  The tree
is walked eight deep; files ending `.wav`, `.aif`, `.aiff` or `.aifc` are listed with their sizes, and a directory
only where it holds some, at any depth.  Rock Ridge and Joliet names are not read.  A file's header is read as
below ("WAV and AIFF files").

## Cue sheets

A compact disc kept as a cue sheet and the files it names: opened by the sheet's path (`syfs_open_path`), a text
file of at most 1 MB with a `FILE` line and a `TRACK` line after it.  The files are looked for beside the sheet, by
the name the sheet gives and then by that name's last part (sheets carry the paths of where they were written).

| Line | Read as |
|---|---|
| `FILE "name" BINARY` | the sectors that follow, as they lie; audio little-endian |
| `FILE "name" MOTOROLA` | the same, audio big-endian |
| `FILE "name" WAVE`, `AIFF` | a WAV's or AIFF's frames: 16 bits, two channels, 44.1 kHz, or the sheet is not read |
| `TRACK nn AUDIO` | an audio track, 2352 bytes a sector (588 frames) |
| `TRACK nn MODE1/2352`, `MODE2/2352` | a data track of raw sectors |
| `TRACK nn MODE1/2048` | a data track of user data alone |
| `INDEX 00 mm:ss:ff`, `INDEX 01 mm:ss:ff` | where the track's gap and the track start in its file, in sectors (75 a second) |
| `PREGAP mm:ss:ff` | silence before the track that no file holds |
| `FLAGS ... PRE` | the track's pre-emphasis |
| `TITLE "..."` | before any track the disc's, the image's label where no volume gives one; after one, the track's name |

Any other line is passed over; another kind of file or mode of track (`MP3`, `MODE2/2336`, `CDG`) fails the sheet.
A file's tracks are of one sector size.

**The disc** is the files' sectors in the sheet's order, each `PREGAP` put in where its track's first index falls.
Sector 0 is the first file's first: the lead-in's two seconds are before it, as in every image.  A track starts at
its index 1 and runs to the next track's index 1 (the disc's end for the last), so the tracks follow one another
with nothing between, and a track's last sectors are the next one's gap.  Sectors a first track's index 1 leaves
before it belong to no track.

**The nodes**: every track a `track` at the top, numbered from the sheet's track number less one, named by its
`TITLE` or `Track nn`.  An audio track has a wave (stereo, 44.1 kHz, 588 frames a sector) and its format is `cdda`;
read `raw` it is its sectors' bytes, little-endian whatever the file.  The first data track is read `raw` as its
2048-byte user data, as far as its own file holds it before the next track.

**The volume**: where a format here reads the first data track, the image is that volume in a container (`cue`):
its format, medium, label, properties and nodes are the volume's, exactly as from an image of the track alone, and
the tracks' nodes come after them.  With no data track the format is `cdda`; with one that no format reads, `cdda`
where there is audio too and `none` otherwise.

## FAT

FAT12, FAT16 and FAT32 volumes as Microsoft's FAT specification (fatgen103) lays them out: a floppy's, or a hard
disk's or memory card's behind an MBR.  Tried last, so that no sampler's floppy of a DOS floppy's size is taken
for one.

**Telling it**: a boot sector whose first byte is a jump (`EB xx 90` or `E9`) and whose BPB is a FAT's: 512 to 4096
bytes a sector (a power of two), a power of two sectors a cluster, at least one reserved sector, one to four FATs,
the media byte 0xF0 or 0xF8 and above, a sector count and a FAT size, the data area starting inside the volume
and the FATs room for every cluster.  The type is the cluster count's, as the specification has it: below 4085
FAT12, below 65525 FAT16, else FAT32 (whose BPB must have a zero 16-bit FAT size and no root entries; the
others a non-zero one).  The 0x55 0xAA signature is not asked of a bare volume.  Where sector 0 is no such boot
sector but carries 0x55 0xAA, it is read as an MBR: of the four entries at 0x1BE, the first in table order with
a FAT partition type (0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E, or one of them with 0x10 set, hidden), a start and a
length, whose first sector is such a boot sector, is the volume.  Further partitions, and extended ones (0x05,
0x0F), are not read.  A bare volume of a floppy's size (160, 180, 320, 360, 720, 1200, 1440, 1680, 1722 or 2880
KB, by its BPB) is a floppy; every other volume is a disk.

**What is read**: the label, from the root directory's volume label entry, else the BPB's (+0x2B, or +0x47 on
FAT32) unless it is `NO NAME`; then every directory and file at any depth (32 deep at most), each a directory or a
file entry under the directory holding it, its size the directory entry's.  The root directory is the fixed area
after the FATs on FAT12 and FAT16, the cluster chain from the BPB's root cluster (+0x2C) on FAT32.  A name is the
entry's long name (VFAT) where the long-name entries before it run in order and their checksum is its short
name's, from UTF-16 into UTF-8 and cut at a character to 63 bytes; else its 8.3 name, its base and extension
lower-cased where byte +0x0C says so (0x08, 0x10), a first byte 0x05 read as 0xE5, and bytes outside ASCII shown
as `?` (the code page is not known).  Deleted entries (0xE5), the volume label, `.` and `..` are skipped; a zero
first byte ends a directory.  A file's or directory's first cluster is +0x1A, with the high half at +0x14 on
FAT32.

**Damage**: a cluster chain is followed only through clusters of the volume, ends at the first cluster already in
it (a loop), and is no longer than the volume's clusters or than the file needs; a directory is read 65 536 entries
at most.  A directory whose first cluster is outside the volume, or is one already walked (a directory pointing at
itself or an ancestor), is listed but not walked again.  A file whose chain is shorter than its size gives no
sample header.

**Written**: files and directories made, removed, named anew and moved, on a volume in a raw image or a VHD.

A name is entered as a short name where it is one as it stands: eight characters and three, upper-case letters,
digits and ``!#$%&'()-@^_`{}~``.  Any other is a long name: UTF-16 in slots of 13 characters, the last part first
with 0x40 in its order byte, a zero after the name and 0xFFFF to the slot's end, each carrying the short name's
checksum; under it a short name made for it, its letters upper-cased and the rest left out, six characters of
the base at most, `~` and the first number from 1 that no entry of the directory has, and three of the extension.
A name is refused that is empty, `.` or `..`, holds a control character or one of `"*/:<>?\|`, is longer than 63
bytes of UTF-8, or that the directory has already, whatever the case of its letters.

A file's clusters are taken first, the first free ones from where the last were taken, and linked from the last
back; then its bytes are written, the last cluster's rest zeros; then its slots, the long name's before its own.
So a file is in no directory until it is whole, and a write that fails gives its clusters back.  Every FAT is
written alike.  A directory is one cluster: `.` naming itself and `..` the directory it is in (zero for the
root), the rest zeros.  The slots go in the first run of free ones (a deleted entry's, or those past the
directory's end); where there is none, a directory that is a chain is given another cluster of zeros.  The root of
FAT12 and FAT16 is of a fixed size, and full when it is full; a directory holds 65 536 slots at most.  Times are
the host's clock; the attribute is 0x20 for a file, 0x10 for a directory.

Removing marks its slots 0xE5, its own first, and frees its chain; a directory's entries are removed before it.
Naming anew and moving enter the same entry again, under the new name or in the other directory, and then give up
the old slots; a directory moved has its `..` set to where it now is.  On FAT32 the FSInfo sector's free count and
next free cluster are set to 0xFFFFFFFF, not known, at the first change.

Not written: a volume on a SmartMedia card (its pages are only read) or in raw CD-ROM sectors, the volume's label,
attributes other than the two, the dirty bits.

Not read: the second FAT (the first is trusted), timestamps and attributes, FAT32's FSInfo, exFAT, short names in
code pages other than ASCII.

## VHD

Microsoft's virtual hard disk (the Virtual Hard Disk Image Format Specification, 2006), a container: the image is
the disk's sectors, 512 bytes each, behind it.  Every number is big-endian.

**Telling it**: the file's last 512 bytes are a footer: `conectix` at +0, and at +64 a checksum, the one's
complement of the sum of the footer's other bytes, that holds.  A dynamic disk is told first by the footer's copy
in its first 512 bytes, which never moves, since the one at its end moves whenever a block is added and another
host may be adding one as the file is opened.  The disk's size in bytes is at +48 (whole sectors,
2040 GB at most) and its type at +60: 2 fixed, 3 dynamic.  A differencing disk (4), whose sectors are partly
another file's, is not read: the file is then taken as it lies, as is one whose checksum does not hold.

**A fixed disk** is the sectors from byte 0, then the footer.

**A dynamic disk** starts with a copy of the footer, whose +16 gives the header's byte: `cxsparse` at +0, the block
allocation table's byte at +16, its entries at +28, a block's bytes at +32 (a power of two of sectors; 2 MB as
made) and a checksum at +36, counted as the footer's.  The table is one 32-bit entry a block: the sector of the
file the block starts at, or 0xFFFFFFFF for a block never written, which reads as zeros.  A block in the file is a
bitmap of its sectors, a bit each, padded to a whole sector (one sector for 2 MB), then its data; the bitmap is
not looked at, since a dynamic disk's block is whole once it is there.

**Written** (`syfs_convert` with the container `vhd`): a dynamic disk of 2 MB blocks.  The footer's copy, the
header at 512, the table at 1536 padded to a sector with 0xFF, then each block that holds any byte but zero in the
image's order, behind a bitmap of ones, then the footer.  The footer: features 2, version 1.0, the time in seconds
since 2000 (UTC), the creator `syfs` 1.0 on `Wi2k`, the size twice, the geometry the specification counts from the
sectors (its appendix), type 3, a random ID.  The image must be whole 512-byte sectors.  A SmartMedia card's image is
written complemented, in 64 KB blocks (below, "SmartMedia cards").

**Written through** (`syfs_image_io`, a host writing the disk's sectors): a fixed disk's sectors are written where
they lie.  A dynamic disk's block that is in the file likewise; one that is not is added when it is first given a
byte that is not zero: at the file's end, over the footer, go its bitmap of ones and its data (zeros but for what
is written), then the footer again, and last its entry in the table, so that a write cut short leaves the disk as
it read before.  Zeros written to a block not in the file add nothing.  A block once added stays, whatever is
written to it after; `syfs_convert` to a raw image and back makes the file small again.

**Several hosts on one file** (two machines on one disk; the io's `length` and `hold`): what a host holds of a
dynamic disk in memory, the table and the file's end, grows old as another adds blocks.  So a block's table entry
is read from the file again whenever it was last seen as absent, before a read gives zeros for it or a write adds
it; and a block is added with the file held, its end asked of the host and the entry read once more, since the
other may have added blocks, or this very one, meanwhile.  The entry being written last, a reader never follows
one to a block not yet whole.  Writes into blocks that are there go where they go on a raw image, held by no one.

Not read: differencing disks, the 511-byte footer of the oldest files, VHDX.

## Roland DATA cards

A JV's DATA card is a RAM card, an M-256E of 32 KB or an M-512E of 64: the image is its cells from the first.
Sixteen bytes of key tell whose it is, sixteen of text follow, then the card's memory as the machine keeps it, each
list's records one after another with a name at the start of each.  **[seen]**

| Key | Card | Text | Lists |
|---|---|---|---|
| `Roland JV-80D`, 0xBB, `P`, 0xAF | a JV-80's or JV-880's | `Use M-256E only!` | 16 performances of 204 bytes from 0x20; 64 patches of 362 from 0xCE0; a rhythm set from 0x6760 |
| `RolandJV-990D`, 0xBB, `P`, 0xAF | a JV-1080's | the card's name, which is the label | 16 performances of 245 bytes from 0x20; 64 patches of 401 from 0xF70; a rhythm set of 2699 from 0x73B0 |

A JV-80's names are twelve characters as they are, and its rhythm set has none.  A JV-1080's are twelve characters
of seven bits each, packed from bit 0 of the record's first byte up, as on a wave board's later patches.  A
JV-1080's 64 KB card is two halves alike, the second from 0x8000 with 0xAF and then the key's text, a byte
further on than the first's: its performances are numbered on from the first half's (17 to 32), its patches 65 to
128, its rhythm set the second.  A half without that key holds nothing, and a 32 KB card is the first half alone.

A JV-1080's card's patches and rhythm sets are a board's packed records (above) and read as `syx` the same way,
at the card's patch or rhythm set of their number; a performance is packed likewise, its common in 53 bytes and
sixteen parts in 12 each, and reads at the card's performance of its number.  Checked: a JV-1080's first user
performance, as its memory keeps it, read so is what the machine answers for it.

Not read: a JV-80's records beyond their names and its rhythm set, the cards of other machines a JV-1080 also takes.

## Battery memories

A machine's battery memory as a file: its bytes as the machine reads them.  Nothing in one tells whose it is, so
it is opened by the host that knows (`syfs_open_as`, the format and the medium `memory`), not told.

**A JV-1080's** (`JV-1080`): 64 KB.  The first 256 bytes are the system's; then each list from a multiple of 256,
its records a DATA card's: 32 performances of 245 bytes from 0x100, 128 patches of 401 from 0x2000, two rhythm sets
of 2699 from 0xE900 **[seen]**.  A record is listed where its name is twelve printable characters, so a memory the
machine has not written lists nothing.  Each reads as `syx` at the user performance, patch or rhythm set of its
number.  The system is not read.

**An XV-5080's** (`XV-5080`) and **an XV-3080's** (`XV-3080`): 256 KB.  After the system, three lists one after
another, each record in a place longer than it: 64 performances 1064 bytes apart, 128 patches 938 apart (an
XV-3080's 838) and four rhythm sets of 10808 bytes; the performances start at 0x1A8 in an XV-5080's and 0x1A4 in
an XV-3080's **[seen]**.  The records are packed as an SRX board's (above): a performance's first 926 bytes are its
common (44), MFX (78), chorus (26), reverb (42), sixteen channels' MIDI (2 each) and its parts (22 each, the first
two bits no parameter; 32 parts, of which an XV-3080 uses sixteen), a patch's first 574 a board's, and a rhythm set
its 160 bytes and then the 88 keys' tones themselves, 121 bytes each.  Listed and read as a JV-1080's, at the user
places of their numbers.  Checked: each machine, started with the memory, answers the same bytes for the user
patches, rhythm sets and performances tried (five or six patches, two rhythm sets and three performances of
each).  What follows a patch's 574
bytes in its place (the split keys of a patch made of samples) is not read.

**A Fantom's** (`Fantom`): 2 MB and 1 KB, the 1 KB of its EEPROM and then the upper half of its flash memory.
Its user sounds are one block at 0x1A0400 **[seen]**: sixteen bytes that start `Roland MIGT4`, two lists of 64
favorites of three bytes, then 64 performances of 1126 bytes (from 0x190 of the block), sixteen multitimbres of 954
(0x11B10), 128 patches of 592 (0x156B0) and sixteen rhythm sets of 10824 (0x27EB0).  The records are packed as
the XVs', by the Fantom's map (`maps.md`): a patch is an XV's 574 bytes and its controller's 18, a rhythm set an
XV's 10808 and its controller's 16; a performance its common (44), three MFX (78 each), chorus (26), reverb (42),
sixteen channels' MIDI (2), sixteen parts (36), sixteen zones (9) and controller (28); a multitimbre the same to
its parts.  A flash never stored to lists nothing: the machine then plays its factory sounds, which are elsewhere.
Checked: the machine started with a memory answers the same bytes for the user patches, rhythm sets, performances
and multitimbres tried (four, three, three and three).

## XV-5080, XV-3080 and Fantom files

The file an XV-5080 or XV-3080 saves its user memory in (`\ROLAND\SOUND_nn.SVD`, on a card or a disk): a halfword,
the length of the head less two; `SVD1`; ten zero bytes; then sixteen bytes for each chunk, its tag, the model
(`XV88`, in both machines' files) and the chunk's place and size (longs), all big-endian.  A chunk starts with its
records' count, a record's size, the length of this head (16) and a zero (longs), and the records follow.  `PRFa`
holds 64 performances of 926 bytes, `PATa` 128 patches and `RHYa` four rhythm sets of 10808: the battery memory's
records (above).  A patch's record is 938 bytes in an XV-5080's file and 574 in an XV-3080's (176 890 bytes in
all, to the XV-5080's 223 482) **[seen]**, which is what tells whose the file is: the format is `XV-5080` or
`XV-3080` by it.  Each record whose name is twelve printable characters is an entry, numbered by its place, and
reads as `syx` by its machine's map at the user place of that number.  The other chunks (`SYSa` the system, `FVRa`,
`CTGa`) are not read.

A Fantom's file (on a floppy, in the root) has the same frame with twelve chunks, its own under the model `NEXY`:
`PRFa` 64 performances of 1126 bytes and `MLTa` sixteen multitimbres of 954, the first of them, which tells the
file; and under `XV88` `PATa`, 128 patches of 956 bytes, and `RHYa`, sixteen rhythm sets of 10824 **[seen]**.  A
patch there is in an XV-5080's 938 bytes, the split keys' place zero, with its controller's 18 after: the memory's
592 are its first 574 and those 18.  Its entries are the format `Fantom`, a multitimbre the type `multi`, and read
as `syx` by the Fantom's map.  Checked: a file the machine saved gives the Data Sets of the memory it saved from,
each of its 224 sounds.  A file with another model's tag is not told.

Such a file is an image of its own (medium `file`), and on a FAT volume a file named `.SVD` that is one has its
entries under it, read from the file's bytes: a card or disk the machine saved to opens down to its sounds.
There they go with their file and are not removed, named or moved by themselves.  Checked on a card an XV-3080
saved to: a patch read from the file on the card is the Data Sets of the same patch in the memory it was saved
from.

## SmartMedia cards

The image is the one emulators keep of a card (MAME's second card format): a 1024-byte header, then every page
followed by its spare area.  The header's first byte is the maker's code (not 0x00 or 0xFF), its second the
device's, which tells the card:

| Device | Card | Page | Pages a block |
|---|---|---|---|
| 0xEA, 0x64 | 2 MB | 256 | 16 |
| 0xE3, 0xE5, 0x6B | 4 MB | 512 | 16 |
| 0xE6 | 8 MB | 512 | 16 |
| 0x73 | 16 MB | 512 | 32 |
| 0x75 | 32 MB | 512 | 32 |
| 0x76 | 64 MB | 512 | 32 |
| 0x79 | 128 MB | 512 | 32 |

A page's spare area is a thirty-second of it (8 bytes, or 16), so the image is 1024 + megabytes × 1 048 576 ÷ 32 ×
33 bytes; an image of another size, or of another device code, is no card's.

**Logical sectors**: the card is zones of 1024 physical blocks, each holding 1000 logical ones (a card of fewer
than 1024 blocks, 1000 in every 1024 of them).  A block's logical number is the 16-bit big-endian field at +6 of
its first page's spare area: its top five bits `00010`, then ten bits of number within the zone, then a parity bit.
The first physical block claiming a number has it; a logical block no physical block claims reads as erased (0xFF).
The logical blocks end to end, 512-byte sectors, are what a format is told from: on the cards seen, an MBR and a
FAT volume.  **[seen]**

Not read: the second address field (+11), the parity bit, the ECC bytes and the CIS block, which no logical number
is asked of.

**In a VHD** a card's image is kept complemented, every byte inverted, and is told by that: sectors of a card's
size whose first two bytes are the complements of a maker's and that size's device code, the third 0x5A and the
header's rest ones.  An erased page, all ones, is then zeros, which a dynamic VHD leaves out, so a card takes the
room of what was written to it; read back, the image is the card's own, byte for byte.  `syfs_convert` makes such
a VHD of a card's raw image, in 64 KB blocks (a card in use is written here and there, and a block holds what a 16 KB
card block does four times over), and the raw image of such a VHD.

**A page's spare area** (512-byte pages; sixteen bytes), on a card a machine has written **[seen]**: six of 0xFF;
the block's address at +6 and again at +11; three bytes at +8 guarding the page's second 256 bytes and three at
+13 guarding its first.  The address is 0x1000, the logical number shifted up one, and a low bit that makes its
ones even; the first block's is zero.  A guard is the parity of the half's bits taken three ways by each bit's
place in its byte (six parities, in the third byte's upper six bits, its lower two set) and eight ways by each bit
of its byte's place among the 256 (sixteen parities, a pair a bit, in the first two bytes), all inverted: 256 zeros
are guarded by three 0xFF.

**The first block** holds the card information structure, 110 bytes then zeros, twice in its first page; its other
pages are erased.  The logical blocks follow from the second.

**A formatted card** (`syfs_create` with the format FAT and the medium `smartmedia`; 8, 16, 32, 64 and 128 MB): as
an XV-5080 formats one, whichever of Full and Quick, which leave the same card.  Its volume, in the card's first
logical blocks:

| Card | Sectors | Heads, a track | Partition from | Type | Its ends | A cluster | A FAT | FAT |
|---|---|---|---|---|---|---|---|---|
| 8 MB | 16 000 | 4, 16 | 25 | 01 | `01 0A 00`, `03 10 F9` | 16 | 3 | 12 |
| 16 MB | 32 000 | 4, 16 | 41 | 01 | `02 0A 00`, `03 10 F3` | 32 | 3 | 12 |
| 32 MB | 64 000 | 8, 16 | 35 | 01 | `02 04 00`, `07 10 F3` | 32 | 6 | 12 |
| 64 MB | 128 000 | 8, 32 | 55 | 01 | `01 18 00`, `07 20 F3` | 32 | 12 | 12 |
| 128 MB | 256 000 | 16, 32 | 47 | 06 | `0F 20 F3` after `01 10 00` | 32 | 32 | 16 |

Sector 0 is zeros but for one active partition's entry and 0x55 0xAA.  The boot sector: `E9 00 00`, `RJA2.00 `, one
reserved sector, two FATs, 256 root entries, the media byte 0xF8, the drive 0, 0x29, a serial of two bytes of the
clock and two zeros, the label ending in zeros, `FAT12   ` or `FAT16   `, 0x55 0xAA.  Each FAT has the media byte
and ones, and cluster 2 as its chain's end; the root holds the label and the directory `ROLAND`, which is cluster
2: `.` and `..`.  Every entry's time is 15:20 on 1 April 2003.  The blocks up to that cluster's are written, each
page with its spare area; the rest of the card is erased.  A card so made is the machine's own byte for byte, the
serial and the three bytes guarding it aside.  The machine formats cards of 2 and 4 MB too, writing every block;
those are not made here.

**A blank card** (`syfs_create` with no format and the medium `smartmedia`): the header zero but for the maker's
code 0xEC, the device's (the first of the table for the size) and 0xA5 at byte 2, then every page and spare area
erased.

## WAV and AIFF files

A file of a CD-ROM or a FAT volume is read as a WAV or an AIFF where its header says so, whatever its name.  **WAV**:
`RIFF` … `WAVE`, then chunks (padded to even sizes): `fmt ` gives the rate (+4) and the bytes a frame (+12), `data`
the frames (its size over the bytes a frame, no more than the file holds), and `smpl` the original key (MIDI unity
note, +12, where at most 127) and, where its loop count (+28) is not zero, the first loop from +36: its type (+4; 0
forward, 1 alternating, 2 backward, given as forward; the type is the mode), its start (+8) and its last frame (+12).
The format tag (`fmt ` +0) tells the values: 1 integers (8-bit unsigned, 16, 24 and 32-bit signed, the channels (+2)
side by side, little-endian), 3 floating point (32 and 64-bit); 0xFFFE (extensible) names one of them in the first
halfword of its subformat (+24).  The sample size (+14) is given as the bits; each value takes the bytes a frame over
the channels.  **AIFF** (and AIFC): `FORM` … `AIFF`, chunks big-endian: `COMM` the channels (+0), the frames (+2), the
sample size (+6), the rate as an 80-bit extended number (+8) and, on an AIFC, the compression type (+18), `SSND` the
frames from its offset (+0) past its 8-byte head, `INST` the original key (+0) and the sustain loop (+8: its play
mode, 1 forward, 2 forward-backward, the mode; its start and end markers' IDs), and `MARK` the markers' positions.  An
AIFF's values are signed big-endian integers in the sample size rounded up to bytes; an AIFC's compression type `NONE`
or `twos` the same, `sowt` little-endian ones, `fl32` and `fl64` floating point (whose sample size is given as 32 and
64).  A sample plays the whole file, start to end, and gives no more frames than the file holds.  Read as 16 bits:
8-bit values shifted up (an unsigned one's 128 taken off), wider ones cut to their top 16 bits, floating point times
32 768, rounded and clipped; the files of a WAV CD-ROM here and files in each of these encodings decode to what
another reader makes of them, bit for bit **[seen]**.  Compressed encodings (µ-law, A-law, ADPCM, an AIFC's other
types) give a header but no frames.

## Formatting

A formatter writes an empty volume over a blank image of the size the host gives, writing the volume's structures
only (a FAT volume's reserved area, FATs and root directory zeroed first) and leaving the data area as it was; an
S-700 series floppy is written whole, as the machines' format writes it.  A size the
layout cannot have, or a label it cannot hold, fails before anything is written.

**A DOS floppy**: FAT12 with Microsoft's standard geometry, 512-byte sectors, one reserved sector, two FATs:

| Size | Sectors | A cluster | Root entries | A FAT | Media | A track, heads |
|---|---|---|---|---|---|---|
| 720 KB | 1440 | 2 sectors | 112 | 3 sectors | 0xF9 | 9, 2 |
| 1.44 MB | 2880 | 1 | 224 | 9 | 0xF0 | 18, 2 |

**A FAT hard disk**: an MBR with one partition, from sector 63 (a track's alignment, the classic DOS convention) to
the image's end, not active, CHS fields for 255 heads and 63 sectors a track (1023, 254, 63 past their reach).
The type is chosen by the image's size, in one place: FAT16 below 512 MB, FAT32 from 512 MB.  This is the common
convention, not something seen of a sampler: one sampler formats its hard disks as FAT and reads FAT32, but which
type it writes at which size is not known here.  FAT16: one reserved sector, 512 root entries, partition type
0x04 below 32 MB and 0x06 above, sectors a cluster from the specification's table (2 up to 16 MB, 4 to 128 MB, 8
to 256 MB, 16 to 512 MB); a partition of 8400 sectors or fewer cannot be FAT16, so the smallest disk is 8464
sectors.  FAT32: 32 reserved sectors, the FSInfo sector at 1 (the free count, the next free cluster 3), a backup
of the boot and FSInfo sectors at 6 and 7, the root directory at cluster 2, 4 KB clusters up to 8 GB and
the specification's larger ones above, partition type 0x0B where the partition ends within CHS reach and 0x0C
past it; at most 2 TB, the MBR's limit.  The FATs' size is the specification's computation, and a cluster count
outside the chosen type's range fails.

Both write a boot sector with the OEM name `MSWIN4.1`, the extended BPB's signature 0x29, a serial number from the
time, the label (or `NO NAME`) and the type's name, a stub that hands the BIOS back (INT 18h), and 0x55 0xAA; the
FATs' first entries (the media byte, end marks; on FAT32 cluster 2's end mark too), each copy the same; and a
volume label entry in the root directory where a label is given (up to 11 characters a short name may hold, letters
upper-cased), dated now.

**An S-760 or S-770 hard disk** (or MO), as the machine's own format leaves it.  The two differ only where said:

| Sectors | What the format writes |
|---|---|
| 0 | the header below |
| 1 | 0xFF |
| 2 to 0x403 | nothing: the system's room, left as it was |
| 0x404 to 0x503 | the segment table, 65 536 halfwords, below |
| 0x504 to 0x86B | the five directories, zero |
| 0x86C to 0x15AB | the five record areas, 0xFF |
| 0x19AE to 0x1DAD | S-760 only: the extended partial table (segments 59 to 115), 0xFF |
| the rest | nothing |

Sector 0: four zero bytes, `S770 MR25A`, zero at 0x0E and 0x0F; at 0x10 fifteen spaces and a zero; at 0x20 the system
line and a zero (" S-760 System Disk    Ver. 2.24" on an S-760, " SYS-772 HardDisk Sys Ver. 2.25" on an S-770 or
S-750, each with its leading space); at 0x40 "       Copyright   Roland      " and a zero; 0xFF from 0x60 to 0xFF; the
name "ID0:Formatted   " at 0x100, whatever the drive's SCSI ID; at 0x110 the last sector (a long, below); the five
counts zero; 0xFF from 0x11E to the end.  Byte 0x0F stays zero: an S-760 sets it to `@` only when its system is saved
to the disk.

The size: the last sector kept at 0x110 is the disk's last (its sector count less one), at most 0x1214D4; the segments
are the whole ones from sector 0x15AC up to that sector, (last − 0x15AC) ÷ 18 rounded down, at most 65 524
(segments 2 to 65 525).  A disk with a whole segment ending exactly on its last sector does not use that segment.  A
disk larger than about 579 MB has the same table as one of that size, and its rest is never used.  The segment table:
entry 0 0xFFFA; entry 1 the free segments (all of them on an S-770; 114 fewer on an S-760); a zero for each segment
the disk has (on an S-760, 0xFFFE for segments 2 to 58 and 0xFFFD for 59 to 115 instead); 0xFFFF for the rest, but for
entry 0xFFFE, which an S-760 sets to 0xFFFE.  A disk needs at least one free segment: more than 7 618 sectors on an
S-760 (3.9 MB), 5 566 on an S-770.  The machines' own format writes these bytes and no others whatever the disk's size
(tried from 10 MB to 2 GB) and SCSI ID, whether or not it is asked to format the medium first.

When it mounts a disk, an S-760 checks sector 0's first 15 bytes (the zeros, the ID and the zero at 0x0E) and the
segment table's entry 0xFFFE (0xFFFF or 0xFFFE, else it calls the disk unformatted); an S-770 checks the 15 bytes.

**An S-760 or S-770 floppy**: the machines format a floppy as their formatted disk, the kind a sound volume is saved
to (neither makes an empty sound disk: a sound disk is what a save leaves).  Every sector is written: sector 0 the
header, every other sector the format's fill, 0x0F.  Sector 0 as on a hard disk to 0x60, with the kind at 0x0F, `:`
on a 2HD disk and `;` on a 2DD one, and the line " S-760 Formatted Disk Ver. 2.24" or " SYS-772 Formatted    Ver.
2.25" at 0x20; 0xFF from 0x60 to the end.  Each machine saves to either's formatted disk.

**What is not formatted**: an S-760's Volume Dump FD disk (kind `<`, `=`: the format's floppy with a header of its
own) and the system disks; a DJ-70's floppy, whose format was not read.

## Not read yet

- S-550: the patches' and tones' other parameters; a W-30 hard disk's layout; a W-30 floppy (none at hand).
- S-50 floppies: told apart only.
- S-760, S-770 floppies: the Volume Dump FD disk's slots; a set's waves as one (each disk gives its own part).
- Boards and cards: the samples' and waves' parameters and sample data, and the patches' other fields; the SO-JD80
  cards (JD-800, JD-990), another format.
- AKAI S900: the `O`, `D` and `F` files; the programs' other parameters.
- AKAI: the drum, effects, cue and take files' contents; the multis' and samples' other parameters.
- E-mu discs: told apart only.
- WAV and AIFF files: compressed encodings.
