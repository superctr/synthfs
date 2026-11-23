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

## Packed records

`maps/records/<model>.json` gives the bytes each block has in a model's packed records (`packing`: `bits`): there a
block's parameters follow one another in the order of their offsets, each in its `bits` from the lowest bit of the
block's first byte up, a parameter of several units its last unit first, and the block's bytes are filled out with
bits that are no parameter.  A record is its area's blocks one after another.  `map::unpack` and `map::pack` go
between a record and its blocks' units.

## The JV-1080

`jv1080`: model ID `6A`, four-byte addresses, a unit a byte of the message; a parameter's `bits` are its value's.
Groups `1-1` the system, `1-2` a performance, `1-3` a patch (the common and four tones) and `1-4` a rhythm set (the
common and keys 35 to 98).  Its records are packed: a patch's 401 bytes and a rhythm set's 2699 on an SR-JV80
board and a DATA card (`formats.md`).

## The S-700 series

`s760`, `s770`, `s750`, `sp700`: model ID `34`, four-byte addresses, every value in nibbles, the high one first.  A
block is the start of the disk's record of the same name (`formats.md`, "The lists") a nibble a unit: the volume's
name (16 bytes, 32 units), a performance's and a patch's first 256 bytes, a partial's 128, a sample's 48.  A parameter
at offset *n* is in the record's byte *n* / 2.  The S-750 takes the S-770's map; the S-760 adds the extended
performance and partial, the SP-700 the extended performance.
