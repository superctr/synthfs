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

A parameter of one unit whose `bits` do not hold its `max` is a value about 64 kept about the middle of its bits:
what is stored is the value less 64 and plus half the bits' range (61 to 67 in three bits as 1 to 7).  `map::unpack`
and `map::pack` go between a record and its blocks' units.

## The tables

`src/maps.cpp` keeps a map's areas as runs: the areas alike one after another, a constant step apart, are one row
with their count, and the blocks of an area are a list written once.  `src/map.cpp` writes them out, an area and
an entry for each place, the first time a map is asked for.  An entry of the top whose group holds groups alone is
an area for each of those, named as the group names them (an XV's part's "Temporary Patch" and "Temporary Rhythm").

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
