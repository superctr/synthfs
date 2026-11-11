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
| `bank` | a board or card: its patches or rhythm sets in one record shape, named for the machines the shape is for |
| `rhythm-set` | a board or card: a rhythm set (named only in the later shapes) |
| `wave` | a board or card: the sound a patch's tone names; its samples, one a split of its keys, under it |

## The image's properties

| key | kind | |
|---|---|---|
| `format` | key | the format told, `none` where the library tells none |
| `medium` | key | `disk` (a hard disk's, MO's or CD-ROM's image), `floppy`, `card` (a PCM card's ROM), `board`, `smartmedia` (a SmartMedia card's pages) |
| `label` | string | the image's own: an S-760/S-770 disk's system line, a floppy's kind line, an S-550 disk's title, an ISO 9660 volume's ID, a FAT volume's label, a board's name; else empty |
| `model` | string | a board or card of Roland's: the model it was sold as ("SR-JV80-08", "SO-PCM1-01", "SRX-07") |
| `title` | string | a board or card of Roland's: its title ("Keyboards of the 60s and 70s") |
| `number` | int | an SRX board: the number its header carries (1 to 12 the boards; from 31, a later software set of one, which has no patches) |
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

A node with a wave (a sample, a tone, a WAV or AIFF file, a board's wave, which answers as the split it plays at key
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

No property is writable yet.
