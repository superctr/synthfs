# Nodes and properties

What the interface (`include/synthfs/synthfs.h`) shows of an image, whatever its format.  The names here are the
strings of `src/keys.def`; a host uses their constants (`SYFS_PROP_NAME`) or hashes the strings itself.

## Keys

Every name is a key: the low 31 bits of the 32-bit FNV-1a hash of its string.  `tools/keys.py` makes
`include/synthfs/syfs_keys.h` of `src/keys.def` (`just keys`) and refuses two strings of one hash, so a new name is
a line in that list and no constant ever changes.  In another language:

```js
function syfsKey(name) {
	let h = 0x811c9dc5;
	for (const b of new TextEncoder().encode(name))
		h = Math.imul(h ^ b, 0x01000193);
	return h & 0x7fffffff;
}
```

## Nodes

Node 0 is the image.  The others are numbered from 1 in the image's order, a node after the one that holds it, and
keep their numbers while the image is open.

A node has one parent (`children`: a partition's volumes, a volume's files, a bank's patches, a wave's samples) and
any number of nodes it uses (`uses`: what it needs to sound, read from its record).  The second is many to many: two
patches may use one partial, two partials one sample.

| type | in |
|---|---|
| `partition` | AKAI: a partition, named A to Z |
| `volume` | S-760/S-770 and AKAI (an S1000/S3000 floppy is one) |
| `performance`, `partial` | S-760/S-770 |
| `patch` | S-760/S-770 and S-550; a board's or card's, under its bank |
| `sample` | S-760/S-770 and AKAI; a board's or card's wave's split, under the wave |
| `program` | AKAI |
| `multi` | AKAI S3000: a multi, its parts' programs |
| `other` | AKAI: a file of another type (drum inputs, effects, cue or take list, an operating system) |
| `area`, `tone` | S-550: an area, a disk's worth of patches and tones; a tone, a wave and its settings |
| `directory`, `file` | ISO 9660: a directory holding audio files, a WAV or AIFF file; FAT: any |
| `bank` | a board or card: its patches or rhythm sets in one record shape, named for the machines the shape is for; a system exclusive file: the areas of one kind ("User Patch") |
| `rhythm-set` | a board or card: a rhythm set (named only in the later shapes) |
| `wave` | a board or card: the sound a patch's tone names; its samples, one a split of its keys, under it |
| `track` | a cue sheet's disc: a track, audio (with a wave) or data |
| `system` | a system exclusive file: a machine's system settings |

## The image's properties

| key | kind | |
|---|---|---|
| `format` | key | the format told, `none` where the library tells none |
| `container` | key | `vhd` where the image is in a VHD, read through its sectors; `cue` where it is a cue sheet's disc; else `raw` |
| `sectors` | int | a cue sheet's disc: its sectors, where the lead-out starts |
| `medium` | key | `disk` (a hard disk's, MO's or CD-ROM's image), `floppy`, `card` (a PCM card's ROM, a DATA card's RAM), `board`, `smartmedia` (a SmartMedia card's pages), `file` (a file of exclusive messages) |
| `label` | string | the image's own: an S-760/S-770 disk's system line, a floppy's kind line, an S-550 disk's title, an ISO 9660 volume's ID, a FAT volume's label, a board's name; else empty |
| `volume` | string | an S-760/S-770 disk's name, the twelve characters a machine's drive list shows; the label there is the system line |
| `model` | string | a board or card of Roland's: the model it was sold as ("SR-JV80-08", "SO-PCM1-01", "SRX-07") |
| `title` | string | a board or card of Roland's: its title ("Keyboards of the 60s and 70s") |
| `number` | int | an SRX board: the number its header carries (1 to 12 the boards; from 31, a later software set of one, which has no patches) |
| `short-name` | string | an SRX board: the short name its header carries, which a machine shows after the board's number ("Strng") |
| `id`, `date` | string | a PCM card: the identity and the date its header carries ("CRD01010", "1991-11-12"), which together tell cards of one name apart |

## A node's properties

| key | kind | |
|---|---|---|
| `name` | string | UTF-8, trailing blanks dropped; an ISO 9660 file's without its version; a FAT file's long name where it has one |
| `type` | key | as above |
| `number` | int | its place in its list in the image, from 0 (the slot the machine numbers from 1) |
| `size` | int | its bytes in the image: a sample's or tone's wave room, a file's length; 0 where unknown |
| `format` | key | the format of the volume or area it is in (an AKAI disk mixes S1000 and S3000) |
| `tag` | string | the image's own code, where it has one: an S-760/S-770 name's category (STR, PAD...), an AKAI file's type letter (p, s, x, d, q, t, m, c; an `other`'s type in hex), an S-550 area's category number, a bank's record shape (an SR-JV80's type, 2 to 5; an SRX's class and type, "4:6"), a wave's split's keys ("60-64", the lowest and the highest it plays) |
| `bank-select` | int | an SRX board's bank of patches or rhythm sets: the bank select a machine chooses it by, MSB × 128 + LSB |
| `category` | int | an SRX board's patch: its category, the number the machines' category lists give (0 none) |
| `exports` | string | the conversions its contents are read in (`syfs_read`), a name each with a blank between; none where there are none |

A track of a cue sheet's disc has these too:

| key | kind | |
|---|---|---|
| `sector` | int | where it starts on the disc, its index 1: sector 0 is two seconds into the disc's time |
| `sectors` | int | its sectors, to the next track's start |
| `pregap` | int | the sectors before its start that count down to it (its index 0), in a file or not |
| `emphasis` | int | an audio track: 1 where it is recorded with pre-emphasis |

A node with a wave (a sample, a tone, a WAV or AIFF file, an audio track, a board's wave, which answers as the split it plays at key
60) has these too.  Points are frames from the first frame stored, the ends exclusive.

| key | kind | |
|---|---|---|
| `rate` | int | Hz |
| `channels` | int | values a frame: 1 for the samplers' own formats, a WAV's or AIFF's own count |
| `bits` | int | each value's bits as stored (12 or 16 on the samplers; 16 for a board's sample, which is coded); read as 16 whatever they are |
| `key` | int | the original key, MIDI number; -1 unknown (a board's sample's fine tune is in the rate) |
| `frames` | int | the frames stored |
| `start`, `end` | int | the frames played |
| `loop` | key | `none`, `forward` (`loop-start` to `loop-end` over and over) or `alternate` (back and forth between them) |
| `loop-start`, `loop-end` | int | equal where `loop` is `none` |
| `reverse` | int | 1 where it is played from the end down to the start (and the loop, where one, run there) |
| `mode` | int | the format's own loop mode code: its finer behaviour (an S-760/S-770 release loop, an AKAI sample's other seven loops) is left to it |

## Conversions

What `syfs_read` gives of a node, by the names `exports` lists.

| name | of | |
|---|---|---|
| `raw` | a track: an audio track's sectors, a data track's user data; a FAT's or CD-ROM's file; an AKAI program, sample, multi or file of another type | the file's bytes as its volume keeps them, as far as its chain reads |
| `wav` | a node with a wave | a WAV file of 16-bit values, the channels as stored, with a `smpl` chunk for the original key and the loop where the sample has either; zeros where the wave is cut short in the image |
| `s16` | a node with a wave | the values alone, in the host's byte order |
| `syx` | an S-760/S-770 disk's or floppy's performance, patch, partial or sample; a board's or DATA card's patch or rhythm set of the JV-1080's shape; an entry of a system exclusive file | its parameters as Roland Data Sets by its model's address map (`maps.md`, `formats.md`), ready to send or to keep as a `.syx` file |

Not yet: an S-760/S-770 or S-550 record's bytes; a board's patch of another shape than the JV-1080's (the JV-880's, the JD-990's, an SRX's), a DATA card's performance or an S-550's as system exclusive.

## Changes

Where the image was opened with a writer and its format is one written (FAT, in a raw image or a VHD), `name` is
writable, and nodes are made (`syfs_import`: a file of the host's bytes, an empty directory), removed
(`syfs_remove`, with what they hold), given another parent (`syfs_move`) and copied in from another image
(`syfs_transfer`: a file as its `raw` bytes, a directory or volume as a directory with what it holds).  A node
keeps its number through every change; a removed one's is not used again.

An S-760's or S-770's disk takes another's entries by `syfs_transfer`: a volume, performance, patch, partial or
sample with all it uses, what is already on the disk used and not made again.  Nothing else of such a disk is
changed yet, and its names are not writable.
