# The parameter address maps

A machine's sounds are reached over MIDI by address: its Data Sets (`DT1`) and Data Requests (`RQ1`) name a place in
a map its MIDI implementation prints, and each parameter has an offset there.  `maps/<model>.json` is that map for
one model, transcribed from its manual; `tools/maps.py` makes `src/maps.cpp` of all of them (`just maps`), the tables
`src/maps.hpp` declares, which is checked in.  The tables keep each parameter's place and range and the place of a
block's name, not the parameters' names or texts.

A map is the form a sound has whatever holds it: a record on a disk, in a board or in a memory is a packing of the
same parameters.

## A file

| key | what |
|---|---|
| `model` | the model's name |
| `model_id` | the bytes that name it in an exclusive message |
| `blocks` | the blocks by a short name, and `top` |

Other keys (a count of parts, lists of names) are for whoever shows the map and are not in the tables.

`blocks.top.entries` is where the blocks are: each entry an `addr` (the address's bytes, three or four), a `name` as
the manual prints it and the `block` found there.  A block that repeats (a patch for each of 128) has an entry each.
A block with `entries` of its own in place of `params` is a group: its blocks at their addresses within it (a patch's
common and tones).  An entry of the top is an area, one thing the machine keeps at a place; the areas whose names
are the same up to a bracket ("User Patch (001)") are a bank, numbered in the map's order.

A block has a `name`, a `size` and its `params`.  Sizes and offsets count the units a value is sent in, which a
parameter's `bits` gives: 7 where a byte of the message is a unit, 4 where every value goes a nibble a byte.

| a parameter's key | what |
|---|---|
| `offset`, `size` | its first unit in the block, and how many |
| `bits` | bits a unit |
| `name` | the manual's name; one name once in a block.  The parameters "... Name 1", "... Name 2" on, in a row, are the block's name, a character each |
| `min`, `max` | the range of the value |
| `display` | the values' text as the manual prints it |
| `signed` | the value is two's complement in its bits |
| `order` | `le`: the value's bytes low first; without it the high unit is first |
| `field` | `[bit, width]`: the parameter is those bits of the value at its offset, which others share |
| `frac` | the low bits that are a fraction |
| `receive_only` | taken by the machine, never sent by it |
| `absent` | the model has no such parameter, though its place in the block and in a record is kept |
| `ignored`, `labels` | for whoever shows the map: a parameter the machine takes no notice of, its values' names |

## Packed records

`maps/records/<model>.json` gives the bytes each block has in a model's packed records (`blocks`).  There a
block's parameters follow one another in the order of their offsets, each in its `bits` times its units, and the
block's bytes are filled out with bits that are no parameter.  A record is its area's blocks one after another
that have bytes there; a block with none is not in the record (an XV-5080's patch's split keys).

| key | what |
|---|---|
| `map` | the map's file's name |
| `packing` | `bits`, the one way there is |
| `first` | `low` (the default): the bits fill each byte from its lowest up, and a parameter of several units has its last unit first.  `high`: from the highest down, the first unit first |
| `blocks` | each packed block's bytes |
| `lead` | the bits of a block before its first parameter, where it has any |
| `order` | for a group, its blocks in the record's order where that is not the order of their addresses; a block's places stay together, in the order of their addresses |
| `align` | for a block, the offsets of the parameters that start on a byte |

Where `packing` is `fields`, a block's parameters do not follow one another: `blocks` gives for each block its
`bytes` and its `fields`, one for each parameter, `[offset, byte, shift, width]` or `[offset, byte, shift, width,
bias]`: the parameter at that offset is `width` bits of that byte of the block from bit `shift` up, and its value
those bits plus the bias within the width (64: a signed value's middle).  A parameter of several units is its
value's bits from the top.  The blocks follow one another as with `bits`.  A field of no width is a parameter the
record does not keep, which reads zero; a byte past the block's own is in a later block of the record (a JD-990
performance keeps each part's voice reserve, a parameter of its common block, among the part's bytes).

A parameter of one unit whose `bits` do not hold its `max` is a value about 64 kept about the middle of its bits:
what is stored is the value less 64 and plus half the bits' range (61 to 67 in three bits as 1 to 7).  `map::unpack`
and `map::pack` go between a record and its blocks' units.

## The tables

`src/maps.cpp` keeps a map's areas as runs: the areas alike one after another, a constant step apart, are one row
with their count, and the blocks of an area are a list written once.  `src/map.cpp` writes them out, an area and
an entry for each place, the first time a map is asked for.  An entry of the top whose group holds groups alone is
an area for each of those, named as the group names them (an XV's part's "Temporary Patch" and "Temporary Rhythm").

## The JV-880

`jv880`: model ID `46`, the JV-80's, JV-90's and JV-1000's too; four-byte addresses, a unit a byte of the message.
Groups `1-1` the system, `1-2` a performance (common and eight parts), `1-3` a patch (common and four tones) and
`1-4` the rhythm set (keys 36 to 96).  Its records are packed in fields: a patch's 362 bytes and the rhythm set's
2684 on an SR-JV80 board, those and a performance's 204 on a DATA card and in the battery memory (`formats.md`).

## The JD-990

`jd990`: model ID `57`, four-byte addresses, a unit a byte of the message; a value past 127 is two units, seven bits
each, the high one first (a tone's wave number, a delay's taps).  Groups `1-1` the system (38 units), `1-2` a
performance (its common, 48, and eight parts of 12), `1-3` a patch (its common, 118, and four tones of 92) and `1-4`
the rhythm setup (its common, 58, and keys 36 to 96, 106 each: the key's own 14 and then a tone's 92).  The address's
first byte is the area: `00` the system, `01` the temporary performance, `02` the performance mode's seven temporary
patches (the second byte the part), `03` the patch mode's temporary patch, `04` the temporary rhythm setup, `05` to
`07` the internal performances (16), patches (64) and rhythm setup, `08` to `0B` the card's system, performances,
patches and rhythm setup.  A patch's tones follow its common block with no gap, so tone B starts at `01 52` of the
last two bytes; the rhythm setup's keys likewise.  The banks are named as the JV-880's ("User Patch" the internal
memory's).

Its records are packed in fields (`maps/records/jd990.json`): a patch's 379 bytes are its common (87) and four
tones (73 each), a performance's 107 its common (35) and eight parts (9 each), the rhythm setup's 5293 its common
(47) and 61 keys (86 each).  Where each parameter stands was found on the machine: every bit of each record turned
over on a DATA card and the card's areas asked for.  A patch is such a record on a DATA card and on an SR-JV80
board; the battery memory keeps its patches another way (`formats.md`, "Battery memories").

## The JD-800

`jd800`: model ID `3D`, three-byte addresses, a unit a byte of the message.  Areas: `00 00 00` the temporary
patch, and from `00 10 00` the five multi-mode parts' patches, which have no effect block; `01 00 00` the temporary
special setup; `02 00 00` the system; `03 00 00` the parts; `04 00 00` the special setup in memory; `05 00 00` the
64 patches in memory, 384 units each; `07 00 00` the display.  A patch is its common (50 units), its effects (46)
and four tones (72 each); the special setup its common and equalizer (10) and keys 36 to 96, each a key's own 16
and a tone's 72.

The JD-990 was made to take a JD-800's sounds, and the two maps name alike what the machines share.  Side by side,
a JD-800 patch against a JD-990's:

| | JD-800 | JD-990 |
|---|---|---|
| the patch | 384 units: common 50, effects 46, four tones of 72 | 486 units: common and effects 118, four tones of 92 |
| key ranges | a tone's low and high together | the four lows, then the four highs |
| velocity ranges, structures, tone control sources, patch pan, analog feel, octave switch | none | in the common |
| the keyboard's own (key mode, split point, the two channels and program changes, hold mode) | in the common | none |
| portamento | its switch, mode and time | and its type; on a JD-990 it needs no solo |
| LFO waveform | TRI, SAW, SQU, S/H, RND | TRI, SIN, SAW, SQR, TRP, S&H, RND, CHS |
| a tone's LFO depths | LFO 1's and LFO 2's to the pitch; for the TVF and for the TVA one LFO chosen and its depth | each LFO's to pitch, TVF and TVA |
| lever, aftertouch | a tone's lever sens, aftertouch modulation sens, aftertouch bend switch, TVF and TVA aftertouch sens; the patch's aftertouch bend | two controllers of the patch's choosing, four destinations and depths each in every tone |
| pitch envelope | level 0, three times, levels 1 and 2 | and a sustain level and a depth; the JD-800's level 2 is its level 3 |
| FXM, sync, tone delay, tone pan | none | in the tone |
| delay taps | a unit, 0 to 125 | two units, 0 to 255, the first 126 values the JD-800's |
| wave | internal or card, a number | and the expansion board |
| the rhythm set | the special setup: a common block of the equalizer and bend, no name, no effects of its own | the rhythm setup: named, with level, pan, effect controls, chorus, delay and reverb |
| a key's pan | L30 to 30R | L50 to 50R, RND, ALT |

A JD-990 takes a JD-800's Data Sets and converts them itself, at the JD-800's temporary places alone: the
temporary patch in patch and rhythm mode, the multi-mode parts' patches (into its parts 1 to 5) in performance mode,
the temporary special setup in any; to the JD-800's memory, system, parts and display it answers an address error,
and it answers no request of the JD-800's model.  So a patch of a JD-800's bank reaches a JD-990 as its own Data
Sets at the temporary patch's place, in messages of 256 data bytes at most, which is the host's to do: the library
gives a file's patches as the JD-800's Data Sets where the file has them.  What the JD-990 makes of them is its own
conversion: what the two share is copied; key ranges are put in its order and a low limit above its high one
brought down to it; portamento stays on only where solo is on; the LFO waveforms are renumbered; the JD-800's
lever, aftertouch and LFO choices become controller destinations and depths (controller 1 the modulation,
controller 2 the aftertouch) and LFO depths; what a JD-800 has not is set to a fixed value (pan at the centre, the
structures the first type, no FXM, the pitch envelope's depth +12).  A special setup becomes a rhythm setup named
"JD-800 Drum Set", its chorus, delay and reverb those of the rhythm setup it replaces.

## The JV-1080

`jv1080`: model ID `6A`, four-byte addresses, a unit a byte of the message; a parameter's `bits` are its value's.
Groups `1-1` the system, `1-2` a performance, `1-3` a patch (the common and four tones) and `1-4` a rhythm set (the
common and keys 35 to 98).  Its records are packed: a patch's 401 bytes and a rhythm set's 2699 on an SR-JV80
board, and those and a performance's 245 on a DATA card and in the battery memory (`formats.md`).

## The XV-5080 and XV-3080

`xv5080`, `xv3080`: model ID `00 10` both, four-byte addresses, a unit a byte of the message; a parameter's `bits`
are its value's, or a nibble's where it is sent in nibbles.  Groups `1-1` the system, `1-2` a performance (its
common, MFX, chorus, reverb, sixteen channels' MIDI and its parts), `1-3-1` a patch (common, MFX, chorus, reverb,
tone mix table, four tones, and the XV-5080's 88 split keys) and `1-3-2` a rhythm set (common, MFX, chorus, reverb
and keys 21 to 108).  The XV-3080's map is the XV-5080's without the system's equalizer, parts 17 to 32 and the
split keys.  Their records are packed from the high bit (`formats.md`, "Roland wave expansion boards and cards" and
"Battery memories"): a patch's 574 bytes, a rhythm set's 10808 and a performance's 926.

## The Fantom

`fantom`: model ID `00 10`, the XVs'; four-byte addresses.  Groups `fa-setup`, `fa-system`, `fa-performance` (its
common, three MFX, chorus, reverb, sixteen channels' MIDI, sixteen parts, sixteen zones and a controller),
`fa-multitimbre` (the same without zones and controller), `fa-patch` (an XV's patch and a controller) and
`fa-rhythm` (an XV's rhythm set and a controller).  Its records are packed as the XVs' (`formats.md`, "Battery
memories"), with the three MFX together after the common, a part's parameters from the decay time offset on
starting on a byte (the 22 bytes of an XV's part, then 14), and a zone's six control switches on its last byte.

## The S-700 series

`s760`, `s770`, `s750`, `sp700`: model ID `34`, four-byte addresses, every value in nibbles, the high one first.  A
block is the start of the disk's record of the same name (`formats.md`, "The lists") a nibble a unit: the volume's
name (16 bytes, 32 units), a performance's and a patch's first 256 bytes, a partial's 128, a sample's 48.  A parameter
at offset *n* is in the record's byte *n* / 2.  The S-750 takes the S-770's map; the S-760 adds the extended
performance and partial, the SP-700 the extended performance.
